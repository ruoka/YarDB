# Shared Compiler Configuration
# Include this file in all Makefiles to ensure consistent compiler settings

ifeq ($(MAKELEVEL),0)

ifndef OS
OS = $(shell uname -s)
endif

COMMON_CXXFLAGS = -std=c++23 -stdlib=libc++ -pthread -fPIC
COMMON_CXXFLAGS += -fexperimental-library -Wall -Wextra -Wno-reserved-module-identifier -Wno-deprecated-declarations -g
COMMON_LDFLAGS =
AR = ar
ARFLAGS = rv

# Platform-specific compiler configuration
ifeq ($(OS),Linux)
# Debian/Ubuntu package installation paths
# Assume LLVM 20 or higher is installed
# Try clang-20 first, then fall back to clang (if it's 20+)
CC = $(shell which clang-20 >/dev/null 2>&1 && echo clang-20 || echo clang)
CXX = $(shell which clang++-20 >/dev/null 2>&1 && echo clang++-20 || echo clang++)
CXXFLAGS = $(COMMON_CXXFLAGS) -I/usr/lib/llvm-20/include/c++/v1 -O3
ARCH = $(shell uname -m)
LDFLAGS = $(COMMON_LDFLAGS) -L/usr/lib/llvm-20/lib -L/usr/lib/llvm-20/lib/c++ -lc++ -O3
endif

ifeq ($(OS),Darwin)
export PATH := /opt/homebrew/bin:$(PATH)
# Assume Clang 20 or higher is available
# Check multiple possible locations for LLVM 20+, or use clang/clang++ from PATH if version 20+
# You can override by setting LLVM_PREFIX environment variable
ifndef LLVM_PREFIX
LLVM_PREFIX := $(shell if [ -d /usr/local/llvm ]; then echo "/usr/local/llvm"; elif [ -d /opt/homebrew/opt/llvm@20 ]; then echo "/opt/homebrew/opt/llvm@20"; elif [ -d /opt/homebrew/opt/llvm ]; then echo "/opt/homebrew/opt/llvm"; elif [ -d /usr/local/opt/llvm@20 ]; then echo "/usr/local/opt/llvm@20"; elif [ -d /usr/local/opt/llvm ]; then echo "/usr/local/opt/llvm"; else echo ""; fi)
endif

# Only check for LLVM if we're not in a non-compilation target (clean, help, etc.)
NON_COMPILE_TARGETS = clean mostlyclean help dump submodule-init submodule-update
# Check if any non-compile target is the only target, or if no targets specified (default to all)
HAS_COMPILE_TARGET = $(if $(filter-out $(NON_COMPILE_TARGETS),$(MAKECMDGOALS)),yes,$(if $(MAKECMDGOALS),no,yes))

ifeq ($(HAS_COMPILE_TARGET),yes)
# If LLVM_PREFIX is not set, check if clang/clang++ in PATH is version 20+
USE_PATH_CLANG = no
ifeq ($(LLVM_PREFIX),)
CLANG_VERSION_CHECK := $(shell clang --version 2>/dev/null | sed -nE 's/.*clang version ([0-9]+).*/\1/p' | head -n1)
ifneq ($(CLANG_VERSION_CHECK),)
ifeq ($(shell test $(CLANG_VERSION_CHECK) -ge 20 && echo yes),yes)
USE_PATH_CLANG = yes
endif
endif
ifeq ($(USE_PATH_CLANG),no)
$(error Clang 20 or higher not found. Please install with: brew install llvm@20, or set LLVM_PREFIX, or ensure clang 20+ is in PATH)
endif
endif

# Set compiler: use LLVM_PREFIX if set, otherwise use clang/clang++ from PATH
ifeq ($(HAS_COMPILE_TARGET),yes)
ifeq ($(LLVM_PREFIX),)
# Use clang/clang++ from PATH (already verified to be 20+)
override CC := clang
override CXX := clang++
# Try to find libc++ headers - check common locations
CLANG_PATH := $(shell which clang 2>/dev/null | xargs dirname 2>/dev/null | xargs dirname 2>/dev/null)
ifeq ($(CLANG_PATH),)
CLANG_PATH := /usr
endif
# Check for libc++ headers in common locations
ifneq ($(wildcard $(CLANG_PATH)/include/c++/v1),)
CXXFLAGS ?= $(COMMON_CXXFLAGS) -I$(CLANG_PATH)/include/c++/v1 -O3
else ifneq ($(wildcard /usr/include/c++/v1),)
CXXFLAGS ?= $(COMMON_CXXFLAGS) -I/usr/include/c++/v1 -O3
else
CXXFLAGS ?= $(COMMON_CXXFLAGS) -O3
endif
# Set LDFLAGS for system clang
LDFLAGS ?= $(COMMON_LDFLAGS) -lc++ -O3
else
# Use LLVM from LLVM_PREFIX
override CC := $(LLVM_PREFIX)/bin/clang
override CXX := $(LLVM_PREFIX)/bin/clang++
# Resolve an SDK once to avoid mixing CommandLineTools and Xcode paths
SDKROOT ?= $(shell xcrun --show-sdk-path 2>/dev/null)
export SDKROOT
# Prevent mixing Apple libc++ headers with Homebrew libc++: rely on Homebrew's c++ headers only
CXXFLAGS ?= $(COMMON_CXXFLAGS) -nostdinc++ -isystem $(LLVM_PREFIX)/include/c++/v1 -O3
# Ensure we consistently use one SDK for C headers/libs and link against Homebrew libc++
# Critical: Explicit libc++ linkage with rpath to fix exception unwinding issues
# This ensures the correct libc++ is used at link and runtime, resolving uncaught exceptions
# Note: -stdlib=libc++ in CXXFLAGS automatically links -lc++, so we don't need to add it explicitly
# Only add library paths that actually exist to avoid linker warnings
# Suppress libunwind reexport warning (harmless, just a library structure issue)
ifneq ($(SDKROOT),)
CXXFLAGS += -isysroot $(SDKROOT)
LDFLAGS ?= $(COMMON_LDFLAGS) -isysroot $(SDKROOT) -L$(LLVM_PREFIX)/lib -Wl,-rpath,$(LLVM_PREFIX)/lib -Wl,-w -O3
else
LDFLAGS ?= $(COMMON_LDFLAGS) -L$(LLVM_PREFIX)/lib -Wl,-rpath,$(LLVM_PREFIX)/lib -Wl,-w -O3
endif
endif
endif

# Verify Clang version is 20 or higher
ifeq ($(HAS_COMPILE_TARGET),yes)
CLANG_VERSION_MAJOR := $(shell "$(CXX)" --version 2>/dev/null | sed -nE 's/.*clang version ([0-9]+).*/\1/p' | head -n1)
ifneq ($(CLANG_VERSION_MAJOR),)
ifeq ($(shell test $(CLANG_VERSION_MAJOR) -lt 20 && echo yes),yes)
$(error Clang version 20 or higher is required. Detected Clang $(CLANG_VERSION_MAJOR). Please install LLVM 20 or higher.)
endif
endif

# Detect problematic Clang exception handling on macOS ARM (see llvm/llvm-project#92121).
# Root cause: LLVM's built-in unwinder enabled by default in Homebrew Clang 18+ on macOS ARM
# causes exceptions thrown from module implementations or constructors in return statements
# to not be caught properly. This is specific to macOS ARM due to Darwin's exception model
# and doesn't occur on Linux (which uses libunwind/libgcc instead).
#
# The proper fix requires rebuilding LLVM with:
#   -DLIBCXXABI_USE_LLVM_UNWINDER=OFF -DCOMPILER_RT_USE_LLVM_UNWINDER=OFF
# Since we're using Homebrew's pre-built LLVM, we work around with:
# 1. Explicit libc++ linkage (already done above)
# 2. Conservative optimization when CLANG_EXCEPTIONS_WORKAROUND=1
ifdef CLANG_EXCEPTIONS_WORKAROUND
# Mitigation: disable optimization to reduce unwinder-related miscompilation.
# This workaround applies to all Clang versions when explicitly enabled.
# NOTE: This is a temporary workaround. The proper fix requires rebuilding LLVM
# without the built-in unwinder, or waiting for an upstream fix.
CXXFLAGS := $(filter-out -O3 -O2 -O1 -O0,$(CXXFLAGS)) -O0 -fno-inline-functions -fno-vectorize -fno-slp-vectorize
CXXFLAGS += -frtti -fexceptions -fcxx-exceptions
LDFLAGS := $(filter-out -O3 -O2 -O1 -O0,$(LDFLAGS)) -O0
$(warning Applied exception handling workaround for Clang $(CLANG_VERSION_MAJOR) (llvm/llvm-project#92121) - using -O0)
endif
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
# For static builds, explicitly add libraries (stdlib=libc++ doesn't apply in static mode)
LDFLAGS := $(filter-out -lc++,$(LDFLAGS))
LDFLAGS += -static -lc++ -lc++abi -lunwind -ldl -pthread
endif

export CC
export CXX
export CXXFLAGS
export LDFLAGS
export OS
export LLVM_PREFIX

endif # ($(MAKELEVEL),0)
