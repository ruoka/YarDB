# https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p1204r0.html

PROJECT := $(lastword $(notdir $(CURDIR)))
OS := $(shell uname -s)

ifeq ($(OS),Linux)
CC :=  /usr/lib/llvm-18/bin/clang
CXX := /usr/lib/llvm-18/bin/clang++
CXXFLAGS = -pthread -I/usr/local/include
LDFLAGS = -L/usr/local/lib
endif

ifeq ($(OS),Darwin)
CC := /Library/Developer/CommandLineTools/usr/bin/clang
CXX := /Library/Developer/CommandLineTools/usr/bin/clang++
CXXFLAGS = -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk
endif

CXXFLAGS += -std=c++23 -stdlib=libc++ -MMD -Wall -Wextra -I. -I$(INCDIR) -I/usr/local/ssl/include/
CXXFLAGS += -Ofast -D__OPTIMIZE__
#CXXFLAGS += -g -DDEBUG
#CXXFLAGS += -fprofile-instr-generate -fcoverage-mapping  -v
CXXFLAGS += -Wno-deprecated-declarations 
LDFLAGS += -lc++ -lcrypto -L/usr/local/ssl/lib

############

PREFIX = .
SRCDIR = $(PROJECT)
TESTDIR = tests
EXAMPLEDIR = examples
OBJDIR = $(PREFIX)/obj
BINDIR = $(PREFIX)/bin
LIBDIR = $(PREFIX)/lib
INCDIR = $(PREFIX)/include
PCMDIR = $(PREFIX)/pcm
GTESTDIR = $(PREFIX)/googletest

.SUFFIXES:
.SUFFIXES: .cpp .hpp .c++m .c++ .o .a

############

ifeq ($(basename $(basename $(shell $(CXX) -dumpversion))),18) # This section only works with Clang 18

MODULES = $(wildcard $(SRCDIR)/*.c++m)

PCMS = $(MODULES:$(SRCDIR)/%.c++m=$(PCMDIR)/%.pcm)

OBJECTS += $(MODULES:$(SRCDIR)/%.c++m=$(OBJDIR)/%.o)

$(PCMDIR)/%.pcm: $(SRCDIR)/%.c++m
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $< --precompile -c -o $@

$(OBJDIR)/%.o: $(PCMDIR)/%.pcm
	@mkdir -p $(@D)
	$(CXX) $< -c -o $@

LIBRARY = $(addprefix $(LIBDIR)/, lib$(PROJECT).a)

$(LIBRARY) : $(OBJECTS)
	@mkdir -p $(@D)
	$(AR) $(ARFLAGS) $@ $^

endif # Clang 18 and above

############

T-SOURCES = $(wildcard $(TESTDIR)/*.cpp)
T-OBJECTS += $(T-SOURCES:$(TESTDIR)/%.cpp=$(OBJDIR)/%.o)

$(BINDIR)/%: $(TESTDIR)/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $< -o $@

############

E-SOURCES = $(wildcard $(EXAMPLEDIR)/*.cpp)
E-OBJECTS += $(E-SOURCES:$(EXAMPLEDIR)/%.cpp=$(OBJDIR)/%.o)

$(BINDIR)/%: $(EXAMPLEDIR)/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $< -o $@

############

HEADERS = $(wildcard $(SRCDIR)/*.hpp) $(wildcard $(SRCDIR)/*/*.hpp)

INCLUDES = $(addprefix $(INCDIR)/, $(HEADERS))

$(INCDIR)/$(SRCDIR)/%.hpp: $(SRCDIR)/%.hpp
	@mkdir -p $(@D)
	cp $< $@

############

GTESTLIBS = $(addprefix $(LIBDIR)/, libgtest.a libgtest_main.a)

$(GTESTLIBS):
	git submodule update --init --recursive --depth 1
	cd $(GTESTDIR) && cmake -DCMAKE_C_COMPILER="$(CC)" -DCMAKE_CXX_COMPILER="$(CXX)" -DCMAKE_CXX_FLAGS="$(CXXFLAGS)" -DCMAKE_INSTALL_PREFIX=.. . && make install

############

TEST_SOURCES = $(wildcard $(SRCDIR)/*.test.c++)

TEST_OBJECTS = $(TEST_SOURCES:$(SRCDIR)/%.c++=$(OBJDIR)/%.o)

TEST_TARGET = $(BINDIR)/test_$(PROJECT)

$(OBJDIR)/%.o: $(SRCDIR)/%.c++
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(SRCDIR) -c $< -o $@

$(TEST_TARGET): $(GTESTLIBS) $(TEST_OBJECTS) $(LIBRARY)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(TEST_OBJECTS) $(LIBRARY) $(GTESTLIBS) -o $@

############

DEPENDENCIES = $(MAINS:$(SRCDIR)/%.cpp=$(OBJDIR)/%.d) $(OBJECTS:%.o=%.d) $(TEST_OBJECTS:%.o=%.d)

############

.DEFAULT_GOAL := all

.PHONY: all
all: $(INCLUDES)

.PHONY: module
module: $(PCMS) $(LIBRARY)

.PHONY: test
test: $(TEST_TARGET)
	$(TEST_TARGET)

.PHONY: benchmark
benchmark: ./bin/benchmark
	./bin/benchmark

.PHONY: example
example: ./bin/example
	./bin/example

.PHONY: clean
clean:
	@rm -rf $(OBJDIR) $(BINDIR) $(LIBDIR) $(INCDIR) $(PCMDIR) 

.PHONY: dump
dump:
	$(foreach v, $(sort $(.VARIABLES)), $(if $(filter file,$(origin $(v))), $(info $(v)=$($(v)))))
	@echo ''

-include $(DEPENDENCIES)

# /usr/bin/valgrind --tool=callgrind  ./bin/benchmark

# /usr/bin/callgrind_annotate callgrind.out.4483 > analysis.txt

# /usr/lib/llvm-18/bin/llvm-profdata 

# /usr/lib/llvm-18/bin/llvm-cov gcov bin/benchmark
