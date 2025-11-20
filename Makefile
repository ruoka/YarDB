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
# Always use built-in std module from libc++
# We build std.pcm from libc++ source, so we need to reference it explicitly
PCMFLAGS_COMMON = -fno-implicit-modules -fno-implicit-module-maps
PCMFLAGS_COMMON += -fmodule-file=std=$(moduledir)/std.pcm
PCMFLAGS_COMMON += $(foreach M, $(modules) $(example-modules), -fmodule-file=$(subst -,:,$(basename $(notdir $(M))))=$(moduledir)/$(basename $(notdir $(M))).pcm)
PCMFLAGS_COMMON += -fprebuilt-module-path=$(moduledir)/
PCMFLAGS_BUILD = $(PCMFLAGS_COMMON)
PCMFLAGS_BUILD += $(foreach P, $(build_submodules),-fmodule-file=$(subst -,:,$(P))=$(moduledir)/$(P).pcm)
PCMFLAGS_TEST = $(PCMFLAGS_BUILD)
PCMFLAGS_TEST += $(foreach P, $(test_submodules),-fmodule-file=$(subst -,:,$(P))=$(moduledir)/$(P).pcm)

# Export compiler settings for submodules (they may not include config/compiler.mk)
# Note: config/compiler.mk uses 'override', so these are already set, but we export for submodules
# Also export LLVM_PREFIX for std module building
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

# Always use built-in std module from libc++
# We build std.pcm from libc++ source, so we need to reference it explicitly
PCMFLAGS_COMMON = -fno-implicit-modules -fno-implicit-module-maps
PCMFLAGS_COMMON += -fmodule-file=std=$(moduledir)/std.pcm
PCMFLAGS_COMMON += $(foreach M, $(modules) $(example-modules), -fmodule-file=$(subst -,:,$(basename $(notdir $(M))))=$(moduledir)/$(basename $(notdir $(M))).pcm)
PCMFLAGS_COMMON += -fprebuilt-module-path=$(moduledir)/
PCMFLAGS_BUILD = $(PCMFLAGS_COMMON)
PCMFLAGS_BUILD += $(foreach P, $(build_submodules),-fmodule-file=$(subst -,:,$(P))=$(moduledir)/$(P).pcm)
PCMFLAGS_TEST = $(PCMFLAGS_BUILD)
PCMFLAGS_TEST += $(foreach P, $(test_submodules),-fmodule-file=$(subst -,:,$(P))=$(moduledir)/$(P).pcm)

###############################################################################

sourcedir = $(project)
testdir = tests
sourcedirs = $(sourcedir) $(testdir)
moduledir = $(PREFIX)/pcm
objectdir = $(PREFIX)/obj
librarydir = $(PREFIX)/lib
binarydir = $(PREFIX)/bin

# Separate flags for submodules - only include std and prebuilt-module-path
# Submodules shouldn't reference main project modules that don't exist yet
# Use PREFIX directly since moduledir = $(PREFIX)/pcm
PCMFLAGS_SUBMODULE = -fno-implicit-modules -fno-implicit-module-maps
PCMFLAGS_SUBMODULE += -fmodule-file=std=$(PREFIX)/pcm/std.pcm
PCMFLAGS_SUBMODULE += -fprebuilt-module-path=$(PREFIX)/pcm/
export PCMFLAGS_SUBMODULE

project = $(lastword $(notdir $(CURDIR)))
library = $(addprefix $(librarydir)/, lib$(project).a)

# Add sourcedir to include path after it's defined
CXXFLAGS += -I$(sourcedir)

targets = $(programs:%=$(binarydir)/%)
modules = $(wildcard $(sourcedir)/*.c++m)
sources = $(filter-out $(programs:%=$(sourcedir)/%.c++) $(test-program:%=$(sourcedir)/%.c++) $(test-sources), $(wildcard $(sourcedir)/*.c++))
objects = $(modules:$(sourcedir)%.c++m=$(objectdir)%.o) $(sources:$(sourcedir)%.c++=$(objectdir)%.o)

test-program = test_runner
test-target = $(test-program:%=$(binarydir)/%)
test-sources = $(wildcard $(sourcedir)/*.test.c++)
test-objects = $(test-sources:$(sourcedir)%.c++=$(objectdir)%.o)
example-modules = $(wildcard $(testdir)/*.c++m)
example-objects = $(example-modules:$(testdir)/%.c++m=$(objectdir)/$(testdir)/%.o)

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
.PRECIOUS: $(objectdir)/%.deps $(moduledir)/%.pcm

###############################################################################

$(moduledir)/%.pcm: $(sourcedir)/%.c++m $(moduledir)/std.pcm | $(moduledir)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(PCMFLAGS_BUILD) $< --precompile -o $@

$(objectdir)/%.impl.o: $(sourcedir)/%.impl.c++ | $(objectdir)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(PCMFLAGS_BUILD) $< -fmodule-file=$(basename $(basename $(@F)))=$(moduledir)/$(basename $(basename $(@F))).pcm -c -o $@

$(objectdir)/%.test.o: $(sourcedir)/%.test.c++ | $(objectdir)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(PCMFLAGS_TEST) -c $< -o $@

$(objectdir)/%.o: $(sourcedir)/%.c++ $(moduledir)/std.pcm | $(objectdir)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(PCMFLAGS_BUILD) $< -c -o $@

# Always use built-in std module from libc++
BUILTIN_STD_OBJECT = $(objectdir)/std.o

$(binarydir)/%: $(sourcedir)/%.c++ $(objects) $(BUILTIN_STD_OBJECT) $(libraries) | $(binarydir)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(PCMFLAGS_BUILD) $^ -o $@

$(library) : $(objects) | $(librarydir)
	@mkdir -p $(@D)
	$(AR) $(ARFLAGS) $@ $^

$(test-target): $(objects) $(test-objects) $(BUILTIN_STD_OBJECT) $(libraries) $(test_libraries) | $(binarydir)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(PCMFLAGS_TEST) $^ -o $@

###############################################################################

$(moduledir)/%.pcm: $(testdir)/%.c++m $(moduledir)/std.pcm | $(moduledir)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(PCMFLAGS_TEST) $< --precompile -o $@

$(objectdir)/%.impl.o: $(testdir)/%.impl.c++ | $(objectdir)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(PCMFLAGS_TEST) $< -fmodule-file=$(basename $(basename $(@F)))=$(moduledir)/$(basename $(basename $(@F))).pcm -c -o $@

$(objectdir)/%.test.o: $(testdir)/%.test.c++ | $(objectdir)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(PCMFLAGS_TEST) $< -c -o $@

$(objectdir)/%.o: $(testdir)/%.c++ $(moduledir)/std.pcm | $(objectdir)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(PCMFLAGS_TEST) $< -c -o $@

$(binarydir)/%: $(testdir)/%.c++ $(example-objects) $(library) $(BUILTIN_STD_OBJECT) $(libraries) $(test_libraries) | $(binarydir)
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

dependencies = $(foreach D, $(sourcedirs), $(objectdir)/$(D).deps)

# Generate dependencies for modules and sources
# Filter out std.pcm dependencies since we always use built-in std module
define create_dependency_hierarchy
	grep -HE '^[ ]*module[ ]+([a-z_0-9][a-z_0-9:]*)' $(1)/*.c++ 2>/dev/null | sed -nE 's|.+/([a-z_0-9\.\-]+)\.c\+\+:[ ]*module[ ]+([a-z_0-9][a-z_0-9:]*)[ ]*;|$(objectdir)/\1.o: $(moduledir)/\2.pcm|p' >> $(2) || true; \
	grep -HE '^[ ]*export[ ]+module' $(1)/*.c++m 2>/dev/null | sed -nE 's|.+/([a-z_0-9\-]+)\.c\+\+m.+|$(objectdir)/\1.o: $(moduledir)/\1.pcm|p' >> $(2) || true; \
	grep -HE '^[ ]*export[ ]+import[ ]+([a-z_0-9][a-z_0-9:]*)' $(1)/*.c++m 2>/dev/null | sed -nE 's|.+/([a-z_0-9\-]+)\.c\+\+m:[ ]*import[ ]+([a-z_0-9][a-z_0-9:]*)[ ]*;|$(moduledir)/\1.pcm: $(moduledir)/\2.pcm|p' | grep -v ':.*std\.pcm' >> $(2) || true; \
	grep -HE '^[ ]*export[ ]+[ ]*import[ ]+:([a-z_0-9:]+)' $(1)/*.c++m 2>/dev/null | sed -nE 's|.+/([a-z_0-9]+)(\-*)([a-z_0-9]*)\.c\+\+m:.*import[ ]+:([a-z_0-9:]+)[ ]*;|$(moduledir)/\1\2\3.pcm: $(moduledir)/\1\-\4.pcm|p' >> $(2) || true; \
	grep -HE '^[ ]*import[ ]+([a-z_0-9][a-z_0-9:]*)' $(1)/*.c++m 2>/dev/null | sed -nE 's|.+/([a-z_0-9\-]+)\.c\+\+m:[ ]*import[ ]+([a-z_0-9][a-z_0-9:]*)[ ]*;|$(moduledir)/\1.pcm: $(moduledir)/\2.pcm|p' | grep -v ':.*std\.pcm' >> $(2) || true; \
	grep -HE '^[ ]*import[ ]+:([a-z_0-9:]+)' $(1)/*.c++m 2>/dev/null | sed -nE 's|.+/([a-z_0-9]+)(\-*)([a-z_0-9]*)\.c\+\+m:.*import[ ]+:([a-z_0-9:]+)[ ]*;|$(moduledir)/\1\2\3.pcm: $(moduledir)/\1\-\4.pcm|p' >> $(2) || true; \
	grep -HE '^[ ]*import[ ]+([a-z_0-9][a-z_0-9:]*)' $(1)/*.c++ 2>/dev/null | sed -nE 's|.+/([a-z_0-9\.\-]+)\.c\+\+:[ ]*import[ ]+([a-z_0-9][a-z_0-9:]*)[ ]*;|$(objectdir)/\1.o: $(moduledir)/\2.pcm|p' | grep -v 'std\.pcm' >> $(2) || true; \
	grep -HE '^[ ]*import[ ]+:([a-z_0-9:]+)' $(1)/*.c++ 2>/dev/null | sed -nE 's|.+/([a-z_0-9]+)(\-*)([a-z_0-9\.]*)\.c\+\+:.*import[ ]+:([a-z_0-9:]+)[ ]*;|$(objectdir)/\1\2\3.o: $(moduledir)/\1\-\4.pcm|p' >> $(2) || true;
endef

$(dependencies): $(modules) $(sources) | $(objectdir)
	@mkdir -p $(@D)
	$(call create_dependency_hierarchy, ./$(basename $(@F)), $@)

-include $(dependencies)

###############################################################################

.PHONY: submodule-deps
submodule-deps: $(submodule_deps_stamps)

$(stamproot)/deps-%: | $(stamproot)
	@mkdir -p $(@D)
	$(MAKE) $(SUBMAKE_FLAGS) -C $(submoduledir)/$* deps $(SUBMAKE_PREFIX_ARG)
	@touch $@

.PHONY: submodule-modules
submodule-modules: $(moduledir)/std.pcm $(submodule_module_stamps)

# Always use built-in std module from libc++
# std.pcm is built from libc++ source (rule defined below), not from deps/std
$(submodules:%=$(moduledir)/%.pcm): $(moduledir)/%.pcm: $(stamproot)/module-% | $(moduledir)
	@if [ -f $(PREFIX)/pcm/$*.pcm ] && [ "$(PREFIX)/pcm/$*.pcm" != "$(moduledir)/$*.pcm" ]; then \
		cp $(PREFIX)/pcm/$*.pcm $(moduledir)/$*.pcm; \
	elif [ -f $(submoduledir)/$*/pcm/$*.pcm ] && [ "$(submoduledir)/$*/pcm/$*.pcm" != "$(moduledir)/$*.pcm" ]; then \
		cp $(submoduledir)/$*/pcm/$*.pcm $(moduledir)/$*.pcm; \
	fi

$(stamproot)/module-%: | $(stamproot)
	@mkdir -p $(@D)
	@if [ "$*" = "std" ]; then \
		echo "Building std module from libc++ source with matching flags"; \
		$(MAKE) $(SUBMAKE_FLAGS) build-builtin-std-module PREFIX=$(PREFIX); \
		touch $@; \
	else \
		$(MAKE) $(SUBMAKE_FLAGS) -C $(submoduledir)/$* module $(SUBMAKE_PREFIX_ARG); \
		touch $@; \
	fi

$(librarydir)/lib%.a: $(moduledir)/%.pcm | $(librarydir)
	@mkdir -p $(librarydir)
	@if [ -f $(submoduledir)/$*/lib/lib$*.a ]; then \
		cp $(submoduledir)/$*/lib/lib$*.a $(librarydir)/lib$*.a; \
	elif [ -f $(PREFIX)/lib/lib$*.a ]; then \
		cp $(PREFIX)/lib/lib$*.a $(librarydir)/lib$*.a; \
	else \
		echo "Warning: lib$*.a not found in submodule, building library target"; \
		$(MAKE) $(SUBMAKE_FLAGS) -C $(submoduledir)/$* lib $(SUBMAKE_PREFIX_ARG); \
		if [ -f $(submoduledir)/$*/lib/lib$*.a ]; then \
			cp $(submoduledir)/$*/lib/lib$*.a $(librarydir)/lib$*.a; \
		elif [ -f $(PREFIX)/lib/lib$*.a ]; then \
			cp $(PREFIX)/lib/lib$*.a $(librarydir)/lib$*.a; \
		fi; \
	fi

# Build std module from libc++ source
# This ensures the module is built with matching compilation flags
# Based on https://libcxx.llvm.org/Modules.html
# We need both the .pcm (module interface) and .o (module implementation with initializer)
.PHONY: build-builtin-std-module
build-builtin-std-module: $(moduledir)/std.pcm $(objectdir)/std.o | $(moduledir) $(objectdir)

# Build std.pcm (module interface)
# Uses LLVM_PREFIX from config/compiler.mk to find std.cppm (if set)
# Falls back to /usr/lib/llvm-20/share/libc++/v1/std.cppm on Linux
# Assumes LLVM 20 or higher is installed on both Linux and Darwin/macOS
# Reuses CXXFLAGS but filters out user-specific include paths and adds std module-specific flags
# Suppress reserved-module-identifier warning only for std.pcm (comes from libc++ source)
STD_MODULE_FLAGS = $(filter-out -I% -isysroot%,$(CXXFLAGS)) -Wno-reserved-module-identifier -fno-implicit-modules -fno-implicit-module-maps
$(moduledir)/std.pcm: | $(moduledir)
	@mkdir -p $(moduledir)
	@if [ -n "$(LLVM_PREFIX)" ] && [ -f $(LLVM_PREFIX)/share/libc++/v1/std.cppm ]; then \
		echo "Precompiling std module from $(LLVM_PREFIX)/share/libc++/v1/std.cppm"; \
		$(CXX) $(STD_MODULE_FLAGS) -nostdinc++ -isystem $(LLVM_PREFIX)/include/c++/v1 \
			$(LLVM_PREFIX)/share/libc++/v1/std.cppm --precompile -o $(moduledir)/std.pcm; \
	elif [ -f /usr/local/llvm/share/libc++/v1/std.cppm ]; then \
		echo "Precompiling std module from /usr/local/llvm/share/libc++/v1/std.cppm"; \
		$(CXX) $(STD_MODULE_FLAGS) -nostdinc++ -isystem /usr/local/llvm/include/c++/v1 \
			/usr/local/llvm/share/libc++/v1/std.cppm --precompile -o $(moduledir)/std.pcm; \
	elif [ -f /usr/lib/llvm-20/share/libc++/v1/std.cppm ]; then \
		echo "Precompiling std module from /usr/lib/llvm-20/share/libc++/v1/std.cppm"; \
		$(CXX) $(STD_MODULE_FLAGS) -nostdinc++ -isystem /usr/lib/llvm-20/include/c++/v1 \
			/usr/lib/llvm-20/share/libc++/v1/std.cppm --precompile -o $(moduledir)/std.pcm; \
	else \
		echo "Error: std.cppm not found."; \
		if [ -n "$(LLVM_PREFIX)" ]; then \
			echo "  Checked: $(LLVM_PREFIX)/share/libc++/v1/std.cppm"; \
		fi; \
		echo "  Checked: /usr/local/llvm/share/libc++/v1/std.cppm"; \
		echo "  Checked: /usr/lib/llvm-20/share/libc++/v1/std.cppm"; \
		echo "Please ensure LLVM 20 or higher with libc++ modules is installed."; \
		exit 1; \
	fi

# Build std.o (module implementation with initializer)
# This provides the module initializer that the linker needs
# Reuses CXXFLAGS but filters out user-specific include paths
STD_OBJECT_FLAGS = $(filter-out -I% -isysroot%,$(CXXFLAGS)) -fno-implicit-modules -fno-implicit-module-maps -fmodule-file=std=$(moduledir)/std.pcm
$(objectdir)/std.o: $(moduledir)/std.pcm | $(objectdir)
	@mkdir -p $(objectdir)
	@echo "Compiling std module implementation with initializer"; \
	$(CXX) $(STD_OBJECT_FLAGS) $(moduledir)/std.pcm -c -o $(objectdir)/std.o

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
	@echo "  make clean             - Remove all build artifacts except std.pcm"
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

all: deps $(dirs) $(moduledir)/std.pcm $(submodule_module_stamps) $(library) $(targets) $(test-target)

build: $(dirs) $(moduledir)/std.pcm $(build_submodule_module_stamps) $(targets)

tests: deps $(dirs) $(moduledir)/std.pcm $(build_submodule_module_stamps) $(test_submodule_module_stamps) $(library) $(test-target)

run_tests: tests
	$(test-target) $(TEST_TAGS)

clean: mostlyclean
	rm -rf $(binarydir) $(librarydir) $(stamproot)
	@if [ -d $(moduledir) ]; then \
		find $(moduledir) -mindepth 1 ! -name 'std.pcm' -exec rm -rf {} +; \
	fi

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

deps: $(dependencies)

###############################################################################
# Utility targets

.PHONY: dump

dump:
	$(foreach v, $(sort $(.VARIABLES)), $(if $(filter file,$(origin $(v))), $(info $(v)=$($(v)))))
	@echo ''
