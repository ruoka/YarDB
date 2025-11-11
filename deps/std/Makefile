.SUFFIXES:
.SUFFIXES:  .cpp .hpp .c++ .c++m .test.c++ .o
.DEFAULT_GOAL = all

ifeq ($(MAKELEVEL),0)

ifndef OS
OS = $(shell uname -s)
endif

ifeq ($(OS),Linux)
CC = /usr/lib/llvm-19/bin/clang
CXX = /usr/lib/llvm-19/bin/clang++
CXXFLAGS = -pthread -I/usr/lib/llvm-19/include/c++/v1
LDFLAGS = -lc++ -L/usr/lib/llvm-19/lib/c++
endif

ifeq ($(OS),Darwin)
CC = /opt/homebrew/opt/llvm/bin/clang
CXX = /opt/homebrew/opt/llvm/bin/clang++
CXXFLAGS =-I/opt/homebrew/opt/llvm/include/c++/v1
LDFLAGS = -L/opt/homebrew/opt/llvm/lib/c++
endif

ifeq ($(OS),Github)
CC = /usr/local/opt/llvm/bin/clang
CXX = /usr/local/opt/llvm/bin/clang++
CXXFLAGS = -I/usr/local/opt/llvm/include/ -I/usr/local/opt/llvm/include/c++/v1
LDFLAGS = -L/usr/local/opt/llvm/lib/c++ -Wl,-rpath,/usr/local/opt/llvm/lib/c++
endif

CXXFLAGS += -std=c++23 -stdlib=libc++
CXXFLAGS += -fprebuilt-module-path=$(moduledir)
CXXFLAGS += -Wall -Wextra -Wno-reserved-module-identifier
CXXFLAGS += -I$(sourcedir)
LDFLAGS += -fuse-ld=lld

endif #($(MAKELEVEL),0)

PREFIX = .
sourcedir = src
objectdir = $(PREFIX)/obj
binarydir = $(PREFIX)/bin
moduledir = $(PREFIX)/pcm
librarydir = $(PREFIX)/lib

programs = main
library = $(librarydir)/libstd.a
.PRECIOUS: $(moduledir)/%.pcm

targets = $(programs:%=$(binarydir)/%)
test-sources = $(wildcard $(sourcedir)/*test.c++)
test-objects = $(test-sources:$(sourcedir)%.c++=$(objectdir)%.o) $(test-program:%=$(objectdir)/%.o)

sources = $(filter-out $(programs:%=$(sourcedir)/%.c++) $(test-sources), $(wildcard $(sourcedir)/*.c++))
modules = $(wildcard $(sourcedir)/*.c++m)
objects = $(modules:$(sourcedir)%.c++m=$(objectdir)%.o) $(sources:$(sourcedir)%.c++=$(objectdir)%.o)

$(moduledir)/%.pcm: $(sourcedir)/%.c++m
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $< --precompile -o $@

$(objectdir)/%.o: $(moduledir)/%.pcm
	@mkdir -p $(@D)
	$(CXX) $< -c -o $@

$(objectdir)/%.impl.o: $(sourcedir)/%.impl.c++
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $< -fmodule-file=$(moduledir)/$(basename $(basename $(@F))).pcm -c -o $@

$(objectdir)/%.test.o: $(sourcedir)/%.test.c++
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $< -c -o $@

$(objectdir)/%.o: $(sourcedir)/%.c++
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $< -c -o $@

$(objectdir)/%.o: $(sourcedir)/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $< -c -o $@

$(binarydir)/%: $(sourcedir)/%.c++ $(objects) $(libraries)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $^ -o $@

$(library) : $(objects)
	@mkdir -p $(@D)
	$(AR) $(ARFLAGS) $@ $^

.PHONY: all
all: module

.PHONY: module
module: $(library)

.PHONY: test
test: $(test-objects) $(targets)

.PHONY: clean
clean:
	rm -rf $(objectdir) $(binarydir) $(moduledir)

.PHONY: dump
dump:
	$(foreach v, $(sort $(.VARIABLES)), $(if $(filter file,$(origin $(v))), $(info $(v)=$($(v)))))
	@echo ''
