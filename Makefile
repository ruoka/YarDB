.SUFFIXES:
.SUFFIXES:  .cpp .hpp .c++ .c++m .impl.c++  .test.c++ .o .impl.o .test.o
.DEFAULT_GOAL = all
OS = $(shell uname -s)
UNITTEST = -s

ifeq ($(OS),Linux)
CXX = /usr/lib/llvm-15/bin/clang++
CXXFLAGS = -pthread -I/usr/lib/llvm-15/include/c++/v1
LDFLAGS = -lc++ -lc++experimental -L/usr/lib/llvm-15/lib/c++
endif

ifeq ($(OS),Darwin)
CXX = /opt/homebrew/opt/llvm/bin/clang++
CXXFLAGS =-I/opt/homebrew/opt/llvm/include/c++/v1
LDFLAGS += -L/opt/homebrew/opt/llvm/lib/c++
endif

CXXFLAGS += -std=c++20 -stdlib=libc++ -fexperimental-library
CXXFLAGS += -fprebuilt-module-path=$(objectdir)
CXXFLAGS += -Wall -Wextra
CXXFLAGS += -I$(sourcedir) -I$(includedir)
LDFLAGS += -fuse-ld=lld -fexperimental-library

sourcedir = yar
includedir = include
objectdir = obj
librarydir = lib
binarydir = bin

test-program = catch_amalgamated
test-target = $(test-program:%=$(binarydir)/%)
test-sources = $(wildcard $(sourcedir)/*test.c++)
test-objects = $(test-sources:$(sourcedir)%.c++=$(objectdir)%.o) $(test-program:%=$(objectdir)/%.o)

programs = yardb yarexport yarproxy yarsh
libraries = $(librarydir)/libnet4cpp.a
targets = $(programs:%=$(binarydir)/%)
sources = $(filter-out $(programs:%=$(sourcedir)/%.c++) $(test-sources), $(wildcard $(sourcedir)/*.c++))
modules = $(wildcard $(sourcedir)/*.c++m)
objects = $(modules:$(sourcedir)%.c++m=$(objectdir)%.o) $(sources:$(sourcedir)%.c++=$(objectdir)%.o)

dependencies = $(objectdir)/Makefile.deps

$(objectdir)/%.pcm: $(sourcedir)/%.c++m
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $< --precompile -c -o $@

$(objectdir)/%.o: $(objectdir)/%.pcm
	@mkdir -p $(@D)
	$(CXX) $< -c -o $@

$(objectdir)/%.impl.o: $(sourcedir)/%.impl.c++
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $< -fmodule-file=$(objectdir)/yar.pcm -c -o $@
#	$(CXX) $(CXXFLAGS) $< -fmodule-file=$(objectdir)/$(basename $(basename $(@F))).pcm -c -o $@

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

$(test-target): $(objects) $(test-objects) $(libraries)
	@mkdir -p $(@D)
	$(CXX) $(LDFLAGS) $^ -o $@

$(librarydir)/%.a:
#	git submodule update --init --depth 10
#	$(MAKE) -C $(subst lib,,$(basename $(@F))) lib PREFIX=..
	$(MAKE) -C net4cpp lib PREFIX=..
	$(MAKE) -C json4cpp all PREFIX=..

$(dependencies): $(sources) $(modules) $(test-sources)
	@mkdir -p $(@D)
#c++m module wrapping headers etc.
	grep -HE '[ ]*export[ ]+module' $(sourcedir)/*.c++m | sed -E 's/.+\/([a-z_0-9\-]+)\.c\+\+m.+/$(objectdir)\/\1.o: $(objectdir)\/\1.pcm/' > $(dependencies)
#c++m module interface unit
	grep -HE '[ ]*import[ ]+([a-z_0-9]+)' $(sourcedir)/*.c++m | sed -E 's/.+\/([a-z_0-9\-]+)\.c\+\+m:[ ]*import[ ]+([a-z_0-9]+)[ ]*;/$(objectdir)\/\1.pcm: $(objectdir)\/\2.pcm/' >> $(dependencies)
#c++m module partition unit
	grep -HE '[ ]*import[ ]+:([a-z_0-9]+)' $(sourcedir)/*.c++m | sed -E 's/.+\/([a-z_0-9]+)(\-*)([a-z_0-9]*)\.c\+\+m:.*import[ ]+:([a-z_0-9]+)[ ]*;/$(objectdir)\/\1\2\3.pcm: $(objectdir)\/\1\-\4.pcm/' >> $(dependencies)
#c++m module impl unit
	grep -HE '[ ]*module[ ]+([a-z_0-9]+)' $(sourcedir)/*.c++ | sed -E 's/.+\/([a-z_0-9\.\-]+)\.c\+\+:[ ]*module[ ]+([a-z_0-9]+)[ ]*;/$(objectdir)\/\1.o: $(objectdir)\/\2.pcm/' >> $(dependencies)
#c++ source code
	grep -HE '[ ]*import[ ]+([a-z_0-9]+)' $(sourcedir)/*.c++ | sed -E 's/.+\/([a-z_0-9\.\-]+)\.c\+\+:[ ]*import[ ]+([a-z_0-9]+)[ ]*;/$(objectdir)\/\1.o: $(objectdir)\/\2.pcm/' >> $(dependencies)

-include $(dependencies)

.PHONY: all
all: $(libraries) $(dependencies) $(targets)

.PHONY: test
test: $(libraries) $(dependencies) $(test-target)
	$(test-target) $(UNITTEST)

.PHONY: clean
clean: mostlyclean
	rm -rf $(librarydir) $(includedir)

.PHONY: mostlyclean
mostlyclean:
	rm -rf $(objectdir) $(binarydir)

.PHONY: dump
dump:
	$(foreach v, $(sort $(.VARIABLES)), $(if $(filter file,$(origin $(v))), $(info $(v)=$($(v)))))
	@echo ''
