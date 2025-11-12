# Shared Compiler Configuration
# Include this file in all Makefiles to ensure consistent compiler settings

ifeq ($(MAKELEVEL),0)

ifndef OS
OS = $(shell uname -s)
endif

COMMON_CXXFLAGS = -std=c++23 -stdlib=libc++
COMMON_CXXFLAGS += -Wall -Wextra -Wno-reserved-module-identifier -Wno-deprecated-declarations
COMMON_LDFLAGS = -lc++
AR = ar
ARFLAGS = rv

# Platform-specific compiler configuration
ifeq ($(OS),Linux)
CC = /usr/lib/llvm-18/bin/clang
CXX = /usr/lib/llvm-18/bin/clang++
CXXFLAGS = $(COMMON_CXXFLAGS) -pthread -I/usr/local/include -O3
LDFLAGS = $(COMMON_LDFLAGS) -L/usr/local/lib -O3
endif

ifeq ($(OS),Darwin)
export PATH := /opt/homebrew/bin:$(PATH)
# Use Homebrew LLVM - prefer llvm@19, fallback to llvm
# Note: System clang from Xcode doesn't fully support C++23 modules, so Homebrew LLVM is required
# You can override by setting LLVM_PREFIX environment variable
ifndef LLVM_PREFIX
LLVM_PREFIX := $(shell if [ -d /opt/homebrew/opt/llvm@19 ]; then echo "/opt/homebrew/opt/llvm@19"; elif [ -d /opt/homebrew/opt/llvm ]; then echo "/opt/homebrew/opt/llvm"; else echo ""; fi)
endif
ifeq ($(LLVM_PREFIX),)
SDKROOT := $(shell xcrun --sdk macosx --show-sdk-path 2>/dev/null)
ifeq ($(SDKROOT),)
SDKROOT := /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk
endif
CC = /Library/Developer/CommandLineTools/usr/bin/clang
CXX = /Library/Developer/CommandLineTools/usr/bin/clang++
CXXFLAGS = $(COMMON_CXXFLAGS) -I$(SDKROOT)/usr/include/c++/v1 -isysroot $(SDKROOT) -O3
LDFLAGS = $(COMMON_LDFLAGS) -isysroot $(SDKROOT) -L$(SDKROOT)/usr/lib -O3
else
CC = $(LLVM_PREFIX)/bin/clang
CXX = $(LLVM_PREFIX)/bin/clang++
CXXFLAGS = $(COMMON_CXXFLAGS) -I$(LLVM_PREFIX)/include/c++/v1 -O3
LDFLAGS = $(COMMON_LDFLAGS) -L$(LLVM_PREFIX)/lib -L$(LLVM_PREFIX)/lib/c++ -Wl,-rpath,$(LLVM_PREFIX)/lib/c++ -O3
endif
# Use default linker on Darwin (lld has compatibility issues with libc++ archive)
endif

ifeq ($(OS),Github)
CC = /usr/local/opt/llvm/bin/clang
CXX = /usr/local/opt/llvm/bin/clang++
CXXFLAGS = $(COMMON_CXXFLAGS) -I/usr/local/opt/llvm/include/ -I/usr/local/opt/llvm/include/c++/v1 -O3
LDFLAGS = $(COMMON_LDFLAGS) -L/usr/local/opt/llvm/lib/c++ -Wl,-rpath,/usr/local/opt/llvm/lib/c++ -O3
endif

export CC
export CXX
export CXXFLAGS
export LDFLAGS

endif # ($(MAKELEVEL),0)

