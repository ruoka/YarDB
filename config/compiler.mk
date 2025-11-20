# Shared Compiler Configuration
# Include this file in all Makefiles to ensure consistent compiler settings

ifeq ($(MAKELEVEL),0)

ifndef OS
OS = $(shell uname -s)
endif

COMMON_CXXFLAGS = -std=c++23 -stdlib=libc++ -pthread -fPIC
COMMON_CXXFLAGS += -fexperimental-library -Wall -Wextra -g
COMMON_LDFLAGS =
AR = ar
ARFLAGS = rv

# Platform-specific compiler configuration
ifeq ($(OS),Linux)
# Debian/Ubuntu package installation paths
CC = clang-20
CXX = clang++-20
CXXFLAGS = $(COMMON_CXXFLAGS) -I/usr/lib/llvm-20/include/c++/v1 -O3
ARCH = $(shell uname -m)
LDFLAGS = $(COMMON_LDFLAGS) -L/usr/lib/$(ARCH)-linux-gnu -lc++ -O3
endif

ifeq ($(OS),Darwin)
export PATH := /opt/homebrew/bin:$(PATH)
# Prefer /usr/local/llvm if available, otherwise use Homebrew LLVM
# Note: System clang from Xcode doesn't fully support C++23 modules, so LLVM is required.
# You can override by setting LLVM_PREFIX environment variable
ifndef LLVM_PREFIX
LLVM_PREFIX := $(shell if [ -d /usr/local/llvm ]; then echo "/usr/local/llvm"; elif [ -d /opt/homebrew/opt/llvm ]; then echo "/opt/homebrew/opt/llvm"; else echo ""; fi)
endif
ifeq ($(LLVM_PREFIX),)
$(error LLVM not found. Please install LLVM at /usr/local/llvm or: brew install llvm)
endif
# Force LLVM unless the user explicitly overrides on the command line.
# This avoids picking up system 'cc'/'c++' from the environment.
override CC := $(LLVM_PREFIX)/bin/clang
override CXX := $(LLVM_PREFIX)/bin/clang++
# Resolve an SDK once to avoid mixing CommandLineTools and Xcode paths
SDKROOT ?= $(shell xcrun --show-sdk-path 2>/dev/null)
export SDKROOT

# Check if LLVM has its own libc++ (Homebrew or /usr/local/llvm with libc++)
LLVM_HAS_LIBCXX := $(shell test -d $(LLVM_PREFIX)/include/c++/v1 && echo yes || echo no)

# Always use built-in std module from libc++
# With -fimplicit-modules, Clang automatically builds std.pcm when first imported
ifeq ($(LLVM_HAS_LIBCXX),yes)
# LLVM with its own libc++: use LLVM's libc++ headers and libraries
# This enables using "import std;" with -fexperimental-library
override CXXFLAGS := $(COMMON_CXXFLAGS) -nostdinc++ -isystem $(LLVM_PREFIX)/include/c++/v1 -fimplicit-modules -O3
# Ensure we consistently use one SDK for C headers/libs and link against LLVM libc++
# Explicit libc++ linkage with rpath ensures correct library resolution
# Check if lib/c++ subdirectory exists (Homebrew), otherwise use lib directly (/usr/local/llvm)
LLVM_LIBCXX_LIBDIR := $(shell if [ -d $(LLVM_PREFIX)/lib/c++ ]; then echo "$(LLVM_PREFIX)/lib/c++"; else echo "$(LLVM_PREFIX)/lib"; fi)
# Build rpath list, avoiding duplicates
LLVM_RPATH := -Wl,-rpath,$(LLVM_LIBCXX_LIBDIR)
ifeq ($(LLVM_LIBCXX_LIBDIR),$(LLVM_PREFIX)/lib)
# LLVM_LIBCXX_LIBDIR equals LLVM_PREFIX/lib, so don't add duplicate rpath
else
# Add both paths since they're different
LLVM_RPATH += -Wl,-rpath,$(LLVM_PREFIX)/lib
endif
ifneq ($(SDKROOT),)
CXXFLAGS += -isysroot $(SDKROOT)
# Note: -stdlib=libc++ in CXXFLAGS automatically links libc++, so no explicit -lc++ needed
LDFLAGS ?= $(COMMON_LDFLAGS) -isysroot $(SDKROOT) -L$(LLVM_LIBCXX_LIBDIR) -L$(LLVM_PREFIX)/lib $(LLVM_RPATH) -O3
else
# Note: -stdlib=libc++ in CXXFLAGS automatically links libc++, so no explicit -lc++ needed
LDFLAGS ?= $(COMMON_LDFLAGS) -L$(LLVM_LIBCXX_LIBDIR) -L$(LLVM_PREFIX)/lib $(LLVM_RPATH) -O3
endif
else
# LLVM without its own libc++: use system libc++ headers and libraries
# Use system libc++ from the SDK (LLVM compiler but system libc++)
# Note: -stdlib=libc++ in CXXFLAGS automatically links libc++, so no need for explicit -lc++
CXXFLAGS ?= $(COMMON_CXXFLAGS) -O3
ifneq ($(SDKROOT),)
CXXFLAGS += -isysroot $(SDKROOT)
LDFLAGS ?= $(COMMON_LDFLAGS) -isysroot $(SDKROOT) -O3
else
LDFLAGS ?= $(COMMON_LDFLAGS) -O3
endif
endif

endif

# Debug override
ifeq ($(DEBUG),1)
CXXFLAGS := $(filter-out -O3 -O2 -O1 -O0,$(CXXFLAGS)) -O0
CXXFLAGS += -fno-omit-frame-pointer -fno-pie
LDFLAGS := $(filter-out -O3 -O2 -O1 -O0,$(LDFLAGS)) -O0
LDFLAGS += -no-pie -rdynamic
endif

ifeq ($(STATIC),1)
LDFLAGS += -static -lc++ -lc++abi -lunwind -ldl -pthread
endif

export CC
export CXX
export CXXFLAGS
export LDFLAGS
export OS
export LLVM_PREFIX

endif # ($(MAKELEVEL),0)
