CXX = clang++

CXXFLAGS = -std=c++1z -stdlib=libc++ -I$(SRCDIR) -MMD# -D DEBUG=1

LDFLAGS =  -stdlib=libc++

SRCDIR = src

TESTDIR = test

OBJDIR = obj

BINDIR = bin

GTESTDIR = ../googletest/googletest


TARGET = yarestdb

SOURCES := $(wildcard $(SRCDIR)/*.cpp) $(wildcard $(SRCDIR)/*/*.cpp) $(wildcard $(SRCDIR)/*/*/*.cpp)

OBJECTS := $(SOURCES:$(SRCDIR)/%.cpp=$(OBJDIR)/%.o)

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BINDIR)/$(TARGET): $(TARGET).o $(OBJECTS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(TARGET).o $(OBJECTS) -o $@



TEST_TARGET = test

TEST_SOURCES := $(wildcard $(TESTDIR)/*.cpp) $(wildcard $(TESTDIR)/*/*.cpp) $(wildcard $(TESTDIR)/*/*/*.cpp)

TEST_OBJECTS := $(TESTS:$(TESTDIR)/%.cpp=$(OBJDIR)/$(TESTDIR)/%.o)

$(OBJDIR)/$(TESTDIR)/%.o: $(TESTDIR)/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(GTESTDIR)/include/ -c $< -o $@

$(BINDIR)/$(TEST_TARGET): $(OBJECTS) $(TEST_OBJECTS)
	@mkdir -p $(@D)
	$(CXX) $(LDFLAGS) $(OBJECTS) $(TEST_OBJECTS) $(GTESTDIR)/make/gtest_main.a -o $@



DEPENDENCIES := $(OBJECTS:$(OBJDIR)/%.o=$(OBJDIR)/%.d) $(TEST_OBJECTS:$(OBJDIR)/%.o=$(OBJDIR)/%.d)

all: $(BINDIR)/$(TARGET) $(BINDIR)/$(TEST_TARGET)

.PHONY: clean
clean:
	@rm -rf $(OBJDIR)
	@rm -rf $(BINDIR)

.PHONY: test
test: $(BINDIR)/$(TEST_TARGET)
	$(BINDIR)/$(TEST_TARGET) --gtest_filter=-*.CommandLine:DbServerTest.*

.PHONY: dump
dump:
	@echo $(SOURCES)
	@echo $(TESTS)
	@echo $(OBJECTS)
	@echo $(DEPENDENCIES)

-include $(DEPENDENCIES)
