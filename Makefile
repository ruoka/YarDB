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
MODULE := ./lib/libnet4cpp.a
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

$(TARGETS): $(MODULE) $(OBJECTS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(@:$(BINDIR)/%=$(SRCDIR)/%.cpp) $(OBJECTS) $(MODULE) -MF $(@:$(BINDIR)/%=$(OBJDIR)/%.d) -o $@

LIBRARIES = $(addprefix $(LIBDIR)/, libyardb.a)

$(LIBRARIES): $(MODULE) $(OBJECTS)
	@mkdir -p $(@D)
	$(AR) $(ARFLAGS) $@ $^

HEADERS = $(wildcard $(SRCDIR)/*.hpp $(SRCDIR)/*/*.hpp $(SRCDIR)/*/*/*.hpp)

INCLUDES = $(HEADERS:$(SRCDIR)/%.hpp=$(INCDIR)/%.hpp)

$(INCDIR)/%.hpp: $(SRCDIR)/%.hpp
	@mkdir -p $(@D)
	cp $< $@

############

GTESTLIBS = $(addprefix $(LIBDIR)/, libgtest.a libgtest_main.a)

$(GTESTLIBS):
	git submodule update --init --recursive --depth 1
	cd $(GTESTDIR) && cmake -DCMAKE_C_COMPILER="$(CC)" -DCMAKE_CXX_COMPILER="$(CXX)" -DCMAKE_CXX_FLAGS="$(CXXFLAGS)" -DCMAKE_INSTALL_PREFIX=.. . && make install

############

TEST_SOURCES = $(call rwildcard,$(TESTDIR)/,*.cpp)

TEST_OBJECTS = $(TEST_SOURCES:$(TESTDIR)/%.cpp=$(OBJDIR)/$(TESTDIR)/%.o)

TEST_TARGET = $(BINDIR)/test

$(OBJDIR)/$(TESTDIR)/%.o: $(TESTDIR)/%.cpp $(GTESTLIBS) $(MODULE)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(INCDIR) -c $< -o $@

$(TEST_TARGET): $(OBJECTS) $(TEST_OBJECTS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(OBJECTS) $(TEST_OBJECTS) $(MODULE) $(GTESTLIBS) -o $@

############

$(MODULE):
	@mkdir -p $(LIBDIR)
	@mkdir -p $(INCDIR)
	cd ./net4cpp && make lib && cp ./lib/libnet4cpp.a ../lib/libnet4cpp.a && cp -r ./include/* ../include
	cd ./cryptic && cp -r ./src/* ../include
	cd ./json4cpp && cp -r ./src/* ../include

############

DEPENDENCIES = $(MAINS:$(SRCDIR)/%.cpp=$(OBJDIR)/%.d) $(OBJECTS:%.o=%.d) $(TEST_OBJECTS:%.o=%.d)

############

.PHONY: all
all: bin

.PHONY: bin
bin: $(TARGETS)

.PHONY: lib
lib: $(LIBRARIES) $(INCLUDES)

.PHONY: test
test: $(TEST_TARGET) $(GTESTLIBS)
	$(TEST_TARGET) --gtest_filter=-*CommandLine:HttpServerTest*:NetReceiverAndSenderTest*

.PHONY: clean
clean:
	@rm -rf $(OBJDIR) $(BINDIR) $(LIBDIR) $(INCDIR)

.PHONY: dump
dump:
	$(foreach v, $(sort $(.VARIABLES)), $(if $(filter file,$(origin $(v))), $(info $(v)=$($(v)))))
	@echo ''

-include $(DEPENDENCIES)
