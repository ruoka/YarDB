# Inspired by https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p1204r0.html

programs = yardb yarsh yarproxy yarexport

submodules = std net xson cryptic tester
submoduledir = deps

###############################################################################

# Include shared compiler configuration
include config/compiler.mk

lowercase_os := $(if $(OS),$(shell echo $(OS) | tr '[:upper:]' '[:lower:]'),unknown)
BUILD_DIR ?= build-$(lowercase_os)
export BUILD_DIR

ifndef PREFIX
PREFIX := $(BUILD_DIR)
endif

# Detect spaces in the absolute path; GNU make targets/prereqs break on unescaped spaces.
empty :=
space := $(empty) $(empty)
# Export compiler settings for submodules (they may not include config/compiler.mk)
# Note: config/compiler.mk uses 'override', so these are already set, but we export for submodules
export CC CXX CXXFLAGS LDFLAGS
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

submodule_deps_prev :=
$(foreach s,$(submodule_deps_stamps),$(if $(submodule_deps_prev),$(eval $(s): $(submodule_deps_prev)))$(eval submodule_deps_prev := $(s)))
submodule_module_prev :=
$(foreach s,$(submodule_module_stamps),$(if $(submodule_module_prev),$(eval $(s): $(submodule_module_prev)))$(eval submodule_module_prev := $(s)))

PCMFLAGS = -fno-implicit-modules -fno-implicit-module-maps
PCMFLAGS += $(foreach P, $(submodules),-fmodule-file=$(subst -,:,$(P))=$(moduledir)/$(P).pcm)
PCMFLAGS += $(foreach P, $(foreach M, $(modules) $(example-modules), $(basename $(notdir $(M)))), -fmodule-file=$(subst -,:,$(P))=$(moduledir)/$(P).pcm)
PCMFLAGS += -fprebuilt-module-path=$(moduledir)/

###############################################################################

sourcedir = $(project)
testdir = tests
sourcedirs = $(sourcedir) $(testdir)
moduledir = $(PREFIX)/pcm
objectdir = $(PREFIX)/obj
librarydir = $(PREFIX)/lib
binarydir = $(PREFIX)/bin

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

libraries = $(filter-out $(librarydir)/libcryptic.a, $(submodules:%=$(librarydir)/lib%.a))

###############################################################################

.SUFFIXES:
.SUFFIXES:  .deps .c++m .c++ .impl.c++ .test.c++ .pcm .o .impl.o .test.o .a
.PRECIOUS: $(objectdir)/%.deps $(moduledir)/%.pcm

###############################################################################

$(moduledir)/%.pcm: $(sourcedir)/%.c++m
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(PCMFLAGS) $< --precompile -o $@

$(objectdir)/%.impl.o: $(sourcedir)/%.impl.c++
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(PCMFLAGS) $< -fmodule-file=$(basename $(basename $(@F)))=$(moduledir)/$(basename $(basename $(@F))).pcm -c -o $@

$(objectdir)/%.test.o: $(sourcedir)/%.test.c++
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(PCMFLAGS) -c $< -o $@

$(objectdir)/%.o: $(sourcedir)/%.c++
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(PCMFLAGS) $< -c -o $@

$(binarydir)/%: $(sourcedir)/%.c++ $(objects) $(libraries)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(PCMFLAGS) $^ -o $@

$(library) : $(objects)
	@mkdir -p $(@D)
	$(AR) $(ARFLAGS) $@ $^

$(test-target): $(objects) $(test-objects) $(libraries)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(PCMFLAGS) $^ -o $@

###############################################################################

$(moduledir)/%.pcm: $(testdir)/%.c++m
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(PCMFLAGS) $< --precompile -o $@

$(objectdir)/%.impl.o: $(testdir)/%.impl.c++
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(PCMFLAGS) $< -fmodule-file=$(basename $(basename $(@F)))=$(moduledir)/$(basename $(basename $(@F))).pcm -c -o $@

$(objectdir)/%.test.o: $(testdir)/%.test.c++
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(PCMFLAGS) $< -c -o $@

$(objectdir)/%.o: $(testdir)/%.c++
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(PCMFLAGS) $< -c -o $@

$(binarydir)/%: $(testdir)/%.c++ $(example-objects) $(library) $(libraries)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(PCMFLAGS) $(LDFLAGS) $^ -o $@

###############################################################################

$(objectdir)/%.o: $(moduledir)/%.pcm
	@mkdir -p $(@D)
	$(CXX) -fPIC $(PCMFLAGS) $< -c -o $@

###############################################################################

dependencies = $(foreach D, $(sourcedirs), $(objectdir)/$(D).deps)

# Generate dependencies for modules and sources
define create_dependency_hierarchy
	-grep -HE '^[ ]*export[ ]+module' $(1)/*.c++m | sed -E 's|.+/([a-z_0-9\-]+)\.c\+\+m.+|$(objectdir)/\1.o: $(moduledir)/\1.pcm|' >> $(2)
	-grep -HE '^[ ]*export[ ]+import[ ]+([a-z_0-9]+)' $(1)/*.c++m | sed -E 's|.+/([a-z_0-9\-]+)\.c\+\+m:[ ]*import[ ]+([a-z_0-9]+)[ ]*;|$(moduledir)/\1.pcm: $(moduledir)/\2.pcm|' >> $(2)
	-grep -HE '^[ ]*import[ ]+([a-z_0-9]+)' $(1)/*.c++m | sed -E 's|.+/([a-z_0-9\-]+)\.c\+\+m:[ ]*import[ ]+([a-z_0-9]+)[ ]*;|$(moduledir)/\1.pcm: $(moduledir)/\2.pcm|' >> $(2)
	-grep -HE '^[ ]*export[ ]+[ ]*import[ ]+:([a-z_0-9]+)' $(1)/*.c++m | sed -E 's|.+/([a-z_0-9]+)(\-*)([a-z_0-9]*)\.c\+\+m:.*import[ ]+:([a-z_0-9]+)[ ]*;|$(moduledir)/\1\2\3.pcm: $(moduledir)/\1\-\4.pcm|' >> $(2)
	-grep -HE '^[ ]*import[ ]+:([a-z_0-9]+)' $(1)/*.c++m | sed -E 's|.+/([a-z_0-9]+)(\-*)([a-z_0-9]*)\.c\+\+m:.*import[ ]+:([a-z_0-9]+)[ ]*;|$(moduledir)/\1\2\3.pcm: $(moduledir)/\1\-\4.pcm|' >> $(2)
	grep -HE '^[ ]*module[ ]+([a-z_0-9]+)' $(1)/*.c++ | sed -E 's|.+/([a-z_0-9\.\-]+)\.c\+\+:[ ]*module[ ]+([a-z_0-9]+)[ ]*;|$(objectdir)/\1.o: $(moduledir)/\2.pcm|' >> $(2)
	grep -HE '^[ ]*import[ ]+([a-z_0-9]+)' $(1)/*.c++ | sed -E 's|.+/([a-z_0-9\.\-]+)\.c\+\+:[ ]*import[ ]+([a-z_0-9]+)[ ]*;|$(objectdir)/\1.o: $(moduledir)/\2.pcm|' >> $(2)
	grep -HE '^[ ]*import[ ]+:([a-z_0-9]+)' $(1)/*.c++ | sed -E 's|.+/([a-z_0-9]+)(\-*)([a-z_0-9\.]*)\.c\+\+:.*import[ ]+:([a-z_0-9]+)[ ]*;|$(objectdir)/\1\2\3.o: $(moduledir)/\1\-\4.pcm|' >> $(2)
endef

$(dependencies): $(modules) $(sources)
	@mkdir -p $(@D)
	$(call create_dependency_hierarchy, ./$(basename $(@F)), $@)

-include $(dependencies)

###############################################################################

.PHONY: submodule-deps
submodule-deps: $(submodule_deps_stamps)

$(stamproot)/deps-%:
	@mkdir -p $(@D)
	$(MAKE) -C $(submoduledir)/$* deps $(SUBMAKE_PREFIX_ARG)
	@touch $@

.PHONY: submodule-modules
submodule-modules: $(submodule_module_stamps)

$(submodules:%=$(moduledir)/%.pcm): $(moduledir)/%.pcm: $(stamproot)/module-% ;

$(stamproot)/module-%:
	@mkdir -p $(@D)
	$(MAKE) -C $(submoduledir)/$* module $(SUBMAKE_PREFIX_ARG)
	@touch $@

$(librarydir)/lib%.a: $(moduledir)/%.pcm
	@true

###############################################################################

.DEFAULT_GOAL = all

.PHONY: submodule-init
submodule-init:
	git submodule init
	git submodule update --init --recursive

.PHONY: all
all: submodule-update deps build

.PHONY: submodule-update
submodule-update:
	@echo "git submodule update skipped"
#	git submodule update --recursive

.PHONY: deps
deps: $(dependencies)

.PHONY: build
build: submodule-modules $(targets)

.PHONY: tests
tests: submodule-update deps $(test-target)

.PHONY: run_tests
run_tests: tests
	$(test-target) $(TEST_TAGS)

.PHONY: clean
clean: mostlyclean
	rm -rf $(binarydir) $(librarydir)
#	git submodule foreach --recursive "git clean -fdx" 

.PHONY: mostlyclean
mostlyclean:
	rm -rf $(objectdir) $(moduledir) $(stamproot)

.PHONY: dump
dump:
	$(foreach v, $(sort $(.VARIABLES)), $(if $(filter file,$(origin $(v))), $(info $(v)=$($(v)))))
	@echo ''
