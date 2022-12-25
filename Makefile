.DEFAULT_GOAL := all

OS := $(shell uname -s)
CXX := clang++

ifeq ($(OS),Linux)
CC :=  /usr/lib/llvm-15/bin/clang
CXX := /usr/lib/llvm-15/bin/clang++
CXXFLAGS = -pthread -I/usr/local/include
LDFLAGS = -L/usr/local/lib
endif

ifeq ($(OS),Darwin)
CC := /Library/Developer/CommandLineTools/usr/bin/clang
CXX := /Library/Developer/CommandLineTools/usr/bin/clang++
CXXFLAGS = -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk
endif

CXXFLAGS += -std=c++20 -stdlib=libc++ -MMD -Wall -Wextra -I$(SRCDIR) -I$(INCDIR)
LDFLAGS +=

############

SRCDIR = yar
TESTDIR = test
OBJDIR = obj
BINDIR = bin
LIBDIR = lib
INCDIR = include
LIBRARY := ./lib/libnet4cpp.a
GTESTDIR = ./googletest

############

# Make does not offer a recursive wildcard function, so here's one:
rwildcard = $(wildcard $1$2) $(foreach d,$(wildcard $1*),$(call rwildcard,$d/,$2))

############

SOURCES = $(filter-out $(MAINS), $(call rwildcard,$(SRCDIR)/,*.cpp))

OBJECTS = $(SOURCES:$(SRCDIR)/%.cpp=$(OBJDIR)/%.o)

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(SRCDIR) -c $< -o $@

############

TARGETS = $(addprefix $(BINDIR)/, yardb yarsh yarexport yarproxy)

MAINS	= $(TARGETS:$(BINDIR)/%=$(SRCDIR)/%.cpp)

$(TARGETS): $(LIBRARY) $(OBJECTS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(@:$(BINDIR)/%=$(SRCDIR)/%.cpp) $(OBJECTS) $(LIBRARY) -MF $(@:$(BINDIR)/%=$(OBJDIR)/%.d) -o $@

############

GTESTLIBS = $(addprefix $(LIBDIR)/, libgtest.a libgtest_main.a)

$(GTESTLIBS):
	git submodule update --init --recursive --depth 1
	cd $(GTESTDIR) && cmake -DCMAKE_C_COMPILER="$(CC)" -DCMAKE_CXX_COMPILER="$(CXX)" -DCMAKE_CXX_FLAGS="$(CXXFLAGS)" -DCMAKE_INSTALL_PREFIX=.. . && make install

############

TEST_SOURCES = $(call rwildcard,$(TESTDIR)/,*.cpp)

TEST_OBJECTS = $(TEST_SOURCES:$(TESTDIR)/%.cpp=$(OBJDIR)/$(TESTDIR)/%.o)

TEST_TARGET = $(BINDIR)/test

$(OBJDIR)/$(TESTDIR)/%.o: $(TESTDIR)/%.cpp $(GTESTLIBS) $(LIBRARY)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(INCDIR) -c $< -o $@

$(TEST_TARGET): $(LIBRARY) $(GTESTLIBS) $(OBJECTS) $(TEST_OBJECTS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(OBJECTS) $(TEST_OBJECTS) $(LIBRARY) $(GTESTLIBS) -o $@

############

$(LIBRARY):
	@mkdir -p $(LIBDIR)
	@mkdir -p $(INCDIR)
	cd ./cryptic && cp -r ./src/* ../include
	cd ./json4cpp && cp -r ./src/* ../include
	cd ./net4cpp && make lib && cp ./lib/libnet4cpp.a ../lib/libnet4cpp.a && cp -r ./include/* ../include

############

DEPENDENCIES = $(MAINS:$(SRCDIR)/%.cpp=$(OBJDIR)/%.d) $(OBJECTS:%.o=%.d) $(TEST_OBJECTS:%.o=%.d)

############

.PHONY: all
all: $(TARGETS) $(TEST_TARGET)

.PHONY: lib
lib: $(GTESTLIBS) $(LIBRARY)

.PHONY: test
test: $(TEST_TARGET)
	$(TEST_TARGET) --gtest_filter=-*CommandLine:HttpServerTest*:NetReceiverAndSenderTest*

.PHONY: clean
clean:
	@rm -rf $(OBJDIR) $(BINDIR) $(LIBDIR) $(INCDIR)

.PHONY: dump
dump:
	$(foreach v, $(sort $(.VARIABLES)), $(if $(filter file,$(origin $(v))), $(info $(v)=$($(v)))))
	@echo ''

-include $(DEPENDENCIES)
