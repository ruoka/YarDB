# Inspired by https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p1204r0.html

programs = yardb yarsh yarproxy yarexport

submodules = std net xson cryptic tester

###############################################################################

ifeq ($(MAKELEVEL),0)

ifndef OS
OS = $(shell uname -s)
endif

ifeq ($(OS),Linux)
CC := /usr/lib/llvm-18/bin/clang
CXX := /usr/lib/llvm-18/bin/clang++
CXXFLAGS = -pthread -I/usr/local/include
LDFLAGS = -L/usr/local/lib
CXXFLAGS += -std=c++23
endif

ifeq ($(OS),Darwin)
CC := /Library/Developer/CommandLineTools/usr/bin/clang
CXX := /Library/Developer/CommandLineTools/usr/bin/clang++
CXXFLAGS = -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk
CXXFLAGS += -std=c++2b
endif

CXXFLAGS += -stdlib=libc++ -Wall -Wextra
CXXFLAGS += -Wno-reserved-module-identifier -Wno-deprecated-declarations
CXXFLAGS += -I$(sourcedir)
LDFLAGS += -fuse-ld=lld

export CC
export CXX
export CXXFLAGS
export LDFLAGS

endif # ($(MAKELEVEL),0)

PCMFLAGS = -fno-implicit-modules -fno-implicit-module-maps
PCMFLAGS += $(foreach P, $(submodules) ,-fmodule-file=$(subst -,:,$(P))=$(moduledir)/$(P).pcm)
PCMFLAGS += $(foreach P, $(foreach M, $(modules), $(basename $(notdir $(M)))), -fmodule-file=$(subst -,:,$(P))=$(moduledir)/$(P).pcm)
PCMFLAGS += -fprebuilt-module-path=$(moduledir)/

###############################################################################

PREFIX = .
sourcedir = ./$(project)
sourcedirs = $(sourcedir)
moduledir = $(PREFIX)/pcm
objectdir = $(PREFIX)/obj
librarydir = $(PREFIX)/lib
binarydir = $(PREFIX)/bin

project = $(lastword $(notdir $(CURDIR)))
library = $(addprefix $(librarydir)/, lib$(project).a)q
targets = $(programs:%=$(binarydir)/%)
modules = $(wildcard $(sourcedir)/*.c++m)
sources = $(filter-out $(programs:%=$(sourcedir)/%.c++) $(test-program:%=$(sourcedir)/%.c++) $(test-sources), $(wildcard $(sourcedir)/*.c++))
objects = $(modules:$(sourcedir)%.c++m=$(objectdir)%.o) $(sources:$(sourcedir)%.c++=$(objectdir)%.o)

test-program = test_runner
test-target = $(test-program:%=$(binarydir)/%)
test-sources = $(wildcard $(sourcedir)/*.test.c++)
test-objects = $(test-sources:$(sourcedir)%.c++=$(objectdir)%.o)

###############################################################################

libraries = $(librarydir)/libstd.a $(librarydir)/libnet.a $(librarydir)/libxson.a  $(librarydir)/libtester.a # $(submodules:%=$(librarydir)/lib%.a)

###############################################################################

.SUFFIXES:
.SUFFIXES:  .deps .c++m .c++ .impl.c++ .test.c++ .pcm .o .impl.o .test.o .a
.PRECIOUS: $(objectdir)/%.deps $(moduledir)/%.pcm

###############################################################################

$(moduledir)/%.pcm: $(sourcedir)/%.c++m
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(PCMFLAGS) $< --precompile -c -o $@

$(objectdir)/%.impl.o: $(sourcedir)/%.impl.c++
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(PCMFLAGS) $< -fmodule-file=$(basename $(basename $(@F)))=$(moduledir)/$(basename $(basename $(@F))).pcm -c -o $@

$(objectdir)/%.test.o: $(sourcedir)/%.test.c++
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(PCMFLAGS) $< -c -o $@

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
	$(CXX) $(LDFLAGS) $(PCMFLAGS) $^ -o $@

###############################################################################

$(objectdir)/%.o: $(moduledir)/%.pcm
	@mkdir -p $(@D)
	$(CXX) $(PCMFLAGS) $< -c -o $@

###############################################################################

dependencies = $(foreach D, $(sourcedirs), $(objectdir)/$(D).deps)

define create_dependency_hierarchy
	-grep -HE '^[ ]*export[ ]+module' $(1)/*.c++m | sed -E 's|.+/([a-z_0-9\-]+)\.c\+\+m.+|$(objectdir)/\1.o: $(moduledir)/\1.pcm|' >> $(2)
	-grep -HE '^[ ]*export[ ]+import[ ]+([a-z_0-9]+)' $(1)/*.c++m | sed -E 's|.+/([a-z_0-9\-]+)\.c\+\+m:[ ]*import[ ]+([a-z_0-9]+)[ ]*;|$(moduledir)/\1.pcm: $(moduledir)/\2.pcm|' >> $(2)
	-grep -HE '^[ ]*import[ ]+([a-z_0-9]+)' $(1)/*.c++m | sed -E 's|.+/([a-z_0-9\-]+)\.c\+\+m:[ ]*import[ ]+([a-z_0-9]+)[ ]*;|$(moduledir)/\1.pcm: $(moduledir)/\2.pcm|' >> $(2)
	-grep -HE '^[ ]*export[ ]+[ ]*import[ ]+:([a-z_0-9]+)' $(1)/*.c++m | sed -E 's|.+/([a-z_0-9]+)(\-*)([a-z_0-9]*)\.c\+\+m:.*import[ ]+:([a-z_0-9]+)[ ]*;|$(moduledir)/\1\2\3.pcm: $(moduledir)/\1\-\4.pcm|' >> $(2)
	-grep -HE '^[ ]*import[ ]+:([a-z_0-9]+)' $(1)/*.c++m | sed -E 's|.+/([a-z_0-9]+)(\-*)([a-z_0-9]*)\.c\+\+m:.*import[ ]+:([a-z_0-9]+)[ ]*;|$(moduledir)/\1\2\3.pcm: $(moduledir)/\1\-\4.pcm|' >> $(2)
	grep -HE '^[ ]*module[ ]+([a-z_0-9]+)' $(1)/*.c++ | sed -E 's|.+/([a-z_0-9\.\-]+)\.c\+\+:[ ]*module[ ]+([a-z_0-9]+)[ ]*;|$(objectdir)/\1.o: $(moduledir)/\2.pcm|' >> $(2)
	grep -HE '^[ ]*import[ ]+([a-z_0-9]+)' $(1)/*.c++ | sed -E 's|.+/([a-z_0-9\.\-]+)\.c\+\+:[ ]*import[ ]+([a-z_0-9]+)[ ]*;|$(objectdir)/\1.o: $(moduledir)/\2.pcm|' >> $(2)
	grep -HE '^[ ]*import[ ]+:([a-z_0-9]+)' $(1)/*.c++ | sed -E 's|.+/([a-z_0-9]+)(\-*)([a-z_0-9\.]*)\.c\+\+:.*import[ ]+:([a-z_0-9]+)[ ]*;|$(objectdir)/\1\2\3.o: $(moduledir)/\1\-\4.pcm|'|  >> $(2)
endef

$(dependencies): $(modules) $(sources)
	@mkdir -p $(@D)
	$(call create_dependency_hierarchy, ./$(basename $(@F)), $@)

-include $(dependencies)

###############################################################################

$(foreach M, $(submodules), $(MAKE) -C $(M) deps PREFIX=../$(PREFIX)):

$(foreach M, $(submodules), $(moduledir)/$(M).pcm):
#	git submodule update --init --depth 1
	$(MAKE) -C $(basename $(@F)) module PREFIX=../$(PREFIX)

$(librarydir)/%.a:
#	git submodule update --init --depth 1
	$(MAKE) -C $(subst lib,,$(basename $(@F))) module PREFIX=../$(PREFIX)

###############################################################################

.DEFAULT_GOAL = all

.PHONY: deps
deps: $(dependencies)

.PHONY: build
build: $(targets)

.PHONY: all
all: deps build

.PHONY: tests
tests: deps $(test-target)

.PHONY: run_tests
run_tests: tests
	$(test-target) $(TEST_TAGS)

.PHONY: clean
clean: mostlyclean
	rm -rf $(binarydir) $(librarydir)

.PHONY: mostlyclean
mostlyclean:
	rm -rf $(objectdir) $(moduledir)

.PHONY: dump
dump:
	$(foreach v, $(sort $(.VARIABLES)), $(if $(filter file,$(origin $(v))), $(info $(v)=$($(v)))))
	@echo ''
