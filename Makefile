# Inspired by https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p1204r0.html

programs = yardb yarsh yarproxy yarexport

submoduledir = deps

###############################################################################

# Parallel build support
# GNU make automatically passes jobserver to sub-makes via $(MAKE)
# Extract explicit -j flag from MAKEFLAGS to pass to sub-makes for compatibility
SUBMAKE_FLAGS := $(filter -j%,$(MAKEFLAGS))

# Include shared compiler configuration (must be before submodules definition)
include config/compiler.mk

# Base submodules list
# Note: std module is always provided by libc++, so we don't build deps/std
base_submodules = net xson cryptic
test_only_submodules = tester
submodules = $(base_submodules) $(test_only_submodules)
# Submodules needed for build target (excludes tester)
build_submodules = $(base_submodules)
test_submodules = $(test_only_submodules)

lowercase_os := $(if $(OS),$(shell echo $(OS) | tr '[:upper:]' '[:lower:]'),unknown)
BUILD_DIR ?= build-$(lowercase_os)
export BUILD_DIR

ifndef PREFIX
PREFIX := $(BUILD_DIR)
endif

# Detect spaces in the absolute path; GNU make targets/prereqs break on unescaped spaces.
empty :=
space := $(empty) $(empty)

# Use implicit modules - Clang will automatically build missing modules (including std)
# Modules are cached in moduledir for reuse across builds
# Note: -fmodules-cache-path is only used during precompile, not during -c compilation
PCMFLAGS_COMMON = -fimplicit-modules
PCMFLAGS_COMMON += $(foreach M, $(modules) $(example-modules), -fmodule-file=$(subst -,:,$(basename $(notdir $(M))))=$(moduledir)/$(basename $(notdir $(M))).pcm)
PCMFLAGS_COMMON += -fprebuilt-module-path=$(moduledir)/
PCMFLAGS_PRECOMPILE = -fmodules-cache-path=$(moduledir)
PCMFLAGS_BUILD = $(PCMFLAGS_COMMON)
PCMFLAGS_BUILD += $(foreach P, $(build_submodules),-fmodule-file=$(subst -,:,$(P))=$(moduledir)/$(P).pcm)
PCMFLAGS_TEST = $(PCMFLAGS_BUILD)
PCMFLAGS_TEST += $(foreach P, $(test_submodules),-fmodule-file=$(subst -,:,$(P))=$(moduledir)/$(P).pcm)

# Export compiler settings for submodules (they may not include config/compiler.mk)
# Note: config/compiler.mk uses 'override', so these are already set, but we export for submodules
export CC CXX CXXFLAGS LDFLAGS LLVM_PREFIX
ifneq ($(findstring $(space),$(CURDIR)),)
# Repo path contains spaces: keep PREFIX relative for targets/prereqs and pass it explicitly to sub-makes.
SUBMAKE_PREFIX_ARG := PREFIX=../../$(PREFIX)
else
# No spaces: safe to export absolute PREFIX to sub-makes.
PREFIX := $(abspath $(PREFIX))
export PREFIX
SUBMAKE_PREFIX_ARG :=
endif

stamproot = $(PREFIX)/stamps
submodule_module_stamps = $(submodules:%=$(stamproot)/module-%)
submodule_deps_stamps = $(submodules:%=$(stamproot)/deps-%)
build_submodule_module_stamps = $(build_submodules:%=$(stamproot)/module-%)
test_submodule_module_stamps = $(test_submodules:%=$(stamproot)/module-%)

submodule_deps_prev :=
$(foreach s,$(submodule_deps_stamps),$(if $(submodule_deps_prev),$(eval $(s): $(submodule_deps_prev)))$(eval submodule_deps_prev := $(s)))
submodule_module_prev :=
$(foreach s,$(submodule_module_stamps),$(if $(submodule_module_prev),$(eval $(s): $(submodule_module_prev)))$(eval submodule_module_prev := $(s)))

###############################################################################

sourcedir = $(project)
testdir = tests
moduledir = $(PREFIX)/pcm
objectdir = $(PREFIX)/obj
librarydir = $(PREFIX)/lib
binarydir = $(PREFIX)/bin

# Separate flags for submodules - use same module cache as main project
# Submodules shouldn't reference main project modules that don't exist yet
PCMFLAGS_SUBMODULE = -fimplicit-modules -fprebuilt-module-path=$(PREFIX)/pcm/
export PCMFLAGS_SUBMODULE

project = $(lastword $(notdir $(CURDIR)))
library = $(addprefix $(librarydir)/, lib$(project).a)

# Add sourcedir to include path after it's defined
CXXFLAGS += -I$(sourcedir)

targets = $(programs:%=$(binarydir)/%)
modules = $(wildcard $(sourcedir)/*.c++m)
sources = $(filter-out $(programs:%=$(sourcedir)/%.c++) $(test-program:%=$(sourcedir)/%.c++) $(test-sources), $(wildcard $(sourcedir)/*.c++))
impl-sources = $(wildcard $(sourcedir)/*.impl.c++)
objects = $(modules:$(sourcedir)%.c++m=$(objectdir)%.o) $(sources:$(sourcedir)%.c++=$(objectdir)%.o) $(impl-sources:$(sourcedir)%.impl.c++=$(objectdir)%.impl.o)

test-program = test_runner
test-target = $(test-program:%=$(binarydir)/%)
test-sources = $(wildcard $(sourcedir)/*.test.c++)
test-objects = $(test-sources:$(sourcedir)%.c++=$(objectdir)%.o)
example-modules = $(wildcard $(testdir)/*.c++m)
example-objects = $(example-modules:$(testdir)/%.c++m=$(objectdir)/$(testdir)/%.o)

# Dependency files: header deps (.d) and module deps
header_deps = $(objects:.o=.d) $(test-objects:.o=.d)

# clang-scan-deps is required (comes with Clang 20+)
# Use the same directory as the compiler
clang_scan_deps := $(shell dirname "$(CXX)" 2>/dev/null)/clang-scan-deps

# One big dependency file for the whole project (module dependencies)
module_depfile = $(moduledir)/modules.dep

# All source files for dependency scanning
all_sources = $(modules) $(sources) $(impl-sources) $(test-sources) $(example-modules) $(wildcard $(testdir)/*.impl.c++) $(wildcard $(testdir)/*.c++)

###############################################################################

# Exclude cryptic from libraries (it's header-only)
libraries = $(filter-out $(librarydir)/libcryptic.a, $(build_submodules:%=$(librarydir)/lib%.a))
test_libraries = $(test_submodules:%=$(librarydir)/lib%.a)

# Directory creation targets
dirs = $(moduledir) $(objectdir) $(librarydir) $(binarydir) $(stamproot)
$(dirs):
	@mkdir -p $@

###############################################################################

.SUFFIXES:
.SUFFIXES:  .deps .c++m .c++ .impl.c++ .test.c++ .pcm .o .impl.o .test.o .a
.PRECIOUS: $(moduledir)/%.pcm $(objectdir)/%.d $(module_depfile)

###############################################################################

# Rule for module interface units (.c++m) → produces .pcm
$(moduledir)/%.pcm: $(sourcedir)/%.c++m $(submodule_module_stamps) $(module_depfile) | $(moduledir)
	@mkdir -p $(@D) $(objectdir)
	$(CXX) $(CXXFLAGS) $(PCMFLAGS_BUILD) $(PCMFLAGS_PRECOMPILE) $< --precompile -o $@

# Rule to compile .pcm to .o (module interface object file)
$(objectdir)/%.o: $(moduledir)/%.pcm | $(objectdir)
	@mkdir -p $(@D)
	$(CXX) -fPIC $(PCMFLAGS_BUILD) $< -c -o $@

# Rule for implementation partitions (.impl.c++) → produces .o
$(objectdir)/%.impl.o: $(sourcedir)/%.impl.c++ $(module_depfile) | $(objectdir) $(moduledir)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(PCMFLAGS_BUILD) $< -fmodule-file=$(basename $(basename $(@F)))=$(moduledir)/$(basename $(basename $(@F))).pcm -c -MD -MF $(@:.o=.d) -o $@

# Rule for test units (.test.c++) → produces .o
$(objectdir)/%.test.o: $(sourcedir)/%.test.c++ $(module_depfile) | $(objectdir) $(moduledir)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(PCMFLAGS_TEST) -c $< -MD -MF $(@:.o=.d) -o $@

# Rule for implementation units (.c++) → produces .o
$(objectdir)/%.o: $(sourcedir)/%.c++ $(module_depfile) | $(objectdir) $(moduledir)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(PCMFLAGS_BUILD) $< -c -MD -MF $(@:.o=.d) -o $@

$(binarydir)/%: $(sourcedir)/%.c++ $(objects) $(libraries) | $(binarydir)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(PCMFLAGS_BUILD) $^ -o $@

$(library) : $(objects) | $(librarydir)
	@mkdir -p $(@D)
	$(AR) $(ARFLAGS) $@ $^

$(test-target): $(objects) $(test-objects) $(libraries) $(test_libraries) | $(binarydir)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(PCMFLAGS_TEST) $^ -o $@

###############################################################################

# Rule for test module interface units
$(moduledir)/%.pcm: $(testdir)/%.c++m $(submodule_module_stamps) $(module_depfile) | $(moduledir)
	@mkdir -p $(@D) $(objectdir)/$(testdir)
	$(CXX) $(CXXFLAGS) $(PCMFLAGS_TEST) $(PCMFLAGS_PRECOMPILE) $< --precompile -o $@

# Rule for test implementation partitions
$(objectdir)/%.impl.o: $(testdir)/%.impl.c++ $(module_depfile) | $(objectdir) $(moduledir)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(PCMFLAGS_TEST) $< -fmodule-file=$(basename $(basename $(@F)))=$(moduledir)/$(basename $(basename $(@F))).pcm -c -MD -MF $(@:.o=.d) -o $@

$(objectdir)/%.test.o: $(testdir)/%.test.c++ | $(objectdir)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(PCMFLAGS_TEST) $< -c -o $@

# Rule for test source files
$(objectdir)/%.o: $(testdir)/%.c++ $(module_depfile) | $(objectdir) $(moduledir)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(PCMFLAGS_TEST) $< -c -MD -MF $(@:.o=.d) -o $@

$(binarydir)/%: $(testdir)/%.c++ $(example-objects) $(library) $(libraries) $(test_libraries) | $(binarydir)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(PCMFLAGS_TEST) $(LDFLAGS) $^ -o $@

###############################################################################

$(objectdir)/$(testdir)/%.o: $(moduledir)/$(testdir)/%.pcm | $(objectdir)
	@mkdir -p $(@D)
	$(CXX) -fPIC $(PCMFLAGS_TEST) $< -c -o $@

$(objectdir)/%.o: $(moduledir)/%.pcm | $(objectdir)
	@mkdir -p $(@D)
	$(CXX) -fPIC $(PCMFLAGS_BUILD) $< -c -o $@

###############################################################################

# First pass: generate the complete module dependency graph
# Generate one big dependency file for all sources using p1689 format
# Parse JSON output to extract module dependencies and create Makefile rules
$(module_depfile): $(all_sources) scripts/parse_module_deps.py | $(objectdir) $(moduledir)
	@echo "Generating module dependency graph..."
	@$(clang_scan_deps) -format=p1689 \
	    -module-files-dir $(moduledir) \
	    -- $(CXX) $(CXXFLAGS) $(PCMFLAGS_BUILD) $(all_sources) 2>/dev/null | \
	python3 scripts/parse_module_deps.py $(moduledir) $(objectdir) > $@

# Include it so Make knows about all .pcm rules
-include $(module_depfile)

# Include generated header dependencies
-include $(header_deps)

###############################################################################

.PHONY: submodule-deps
submodule-deps: $(submodule_deps_stamps)

$(stamproot)/deps-%: | $(stamproot)
	@mkdir -p $(@D)
	$(MAKE) $(SUBMAKE_FLAGS) -C $(submoduledir)/$* deps $(SUBMAKE_PREFIX_ARG)
	@touch $@

.PHONY: submodule-modules
submodule-modules: $(submodule_module_stamps)

# Submodules build into PREFIX/pcm via SUBMAKE_PREFIX_ARG
# Since moduledir = $(PREFIX)/pcm, no copy needed - file is already in the right place
$(submodules:%=$(moduledir)/%.pcm): $(moduledir)/%.pcm: $(stamproot)/module-% | $(moduledir)
	@:

$(stamproot)/module-%: | $(stamproot)
	@mkdir -p $(@D)
	$(MAKE) $(SUBMAKE_FLAGS) -C $(submoduledir)/$* module $(SUBMAKE_PREFIX_ARG)
	@touch $@

# Submodules build into PREFIX/lib via SUBMAKE_PREFIX_ARG
# Since librarydir = $(PREFIX)/lib, no copy needed - file is already in the right place
# Depend on module stamp to ensure submodule is built, then trigger lib build
$(librarydir)/lib%.a: $(stamproot)/module-% | $(librarydir)
	@mkdir -p $(librarydir)
	$(MAKE) $(SUBMAKE_FLAGS) -C $(submoduledir)/$* lib $(SUBMAKE_PREFIX_ARG)


###############################################################################

.DEFAULT_GOAL = all

# Targets that must run sequentially (not in parallel)
.NOTPARALLEL: clean mostlyclean submodule-init submodule-update

###############################################################################
# High-level user targets

.PHONY: help all build tests run_tests clean mostlyclean

help:
	@echo "YarDB - Yet Another Relational Database"
	@echo ""
	@echo "Available targets:"
	@echo "  make help              - Show this help message"
	@echo "  make all               - Build every program and unit test binary"
	@echo "  make build             - Build all programs (yardb, yarsh, yarproxy, yarexport)"
	@echo "  make tests             - Build unit test binary only"
	@echo "  make run_tests         - Build and run unit tests (use TEST_TAGS='...' for filtering)"
	@echo "  make clean             - Remove all build artifacts (modules rebuilt automatically)"
	@echo "  make mostlyclean       - Remove object files only"
	@echo ""
	@echo "Build configuration:"
	@echo "  BUILD_DIR              - Build directory (default: build-<os>)"
	@echo "  V=1                    - Verbose build output"
	@echo "  DEBUG=1                - Build with debug flags (-O0, -g)"
	@echo "  STATIC=1               - Build static binaries"
	@echo ""
	@echo "Parallel builds:"
	@echo "  make -jN               - Build with N parallel jobs (e.g., make -j4)"
	@echo "  make -j                - Build with unlimited parallel jobs"
	@echo "  Note: Parallel builds are automatically passed to sub-makes"
	@echo ""
	@echo "Examples:"
	@echo "  make build                    # Build all programs"
	@echo "  make -j4 build                 # Build with 4 parallel jobs"
	@echo "  make tests                    # Build unit tests without running them"
	@echo "  make run_tests                # Build and run all unit tests"
	@echo "  make run_tests TEST_TAGS='yar'  # Run unit tests with tag filter"

all: $(dirs) $(submodule_module_stamps) $(library) $(targets) $(test-target)

build: $(dirs) $(build_submodule_module_stamps) $(targets)

tests: $(dirs) $(build_submodule_module_stamps) $(test_submodule_module_stamps) $(library) $(test-target)

run_tests: tests
	$(test-target) $(TEST_TAGS)

clean: mostlyclean
	rm -rf $(binarydir) $(librarydir) $(stamproot)
	@if [ -d $(moduledir) ]; then \
		rm -rf $(moduledir)/*; \
	fi
	@find $(objectdir) -name "*.d" 2>/dev/null | xargs rm -f 2>/dev/null || true

mostlyclean:
	rm -rf $(objectdir)

###############################################################################
# Submodule and dependency management

.PHONY: submodule-init submodule-update deps

submodule-init:
	git submodule init
	git submodule update --init --recursive

submodule-update:
	@echo "git submodule update skipped"
#	git submodule update --recursive

deps: $(header_deps) $(module_depfile)

###############################################################################
# Utility targets

.PHONY: dump

dump:
	$(foreach v, $(sort $(.VARIABLES)), $(if $(filter file,$(origin $(v))), $(info $(v)=$($(v)))))
	@echo ''
