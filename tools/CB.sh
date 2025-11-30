#!/usr/bin/env bash
# tools/CB.sh — C++ Builder bootstrap for YarDB project
# Works perfectly with your /usr/local/llvm setup

set -e

TOOLS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$TOOLS_DIR/.." && pwd)"
SRC="$PROJECT_ROOT/deps/tester/tools/cb.c++"
BIN="$TOOLS_DIR/cb"

# Detect OS and set compiler/LLVM paths
UNAME_OUT="$(uname -s)"
case "$UNAME_OUT" in
    Linux)
    CXX_COMPILER="clang++-20"
    LLVM_PREFIX="/usr/lib/llvm-20"
        STD_CPPM_DEFAULT="/usr/lib/llvm-20/share/libc++/v1/std.cppm"
        ;;
    Darwin)
    CXX_COMPILER="/usr/local/llvm/bin/clang++"
    LLVM_PREFIX="/usr/local/llvm"
        STD_CPPM_DEFAULT="/usr/local/llvm/share/libc++/v1/std.cppm"
        ;;
    *)
    echo "ERROR: Unsupported OS '$UNAME_OUT'"
    exit 1
        ;;
esac

# Ensure we always search the LLVM lib dir when linking CB itself
export LDFLAGS="-Wl,-rpath,$LLVM_PREFIX/lib ${LDFLAGS}"

# Recompile only if source is newer or binary missing
if [[ ! -x "$BIN" || "$SRC" -nt "$BIN" ]]; then
    echo "Building CB (C++ Builder) with $CXX_COMPILER..."
    "$CXX_COMPILER" \
        -std=c++23 -O3 -pthread \
        -fuse-ld=lld \
        -stdlib=libc++ \
        -I"$LLVM_PREFIX/include/c++/v1" \
        -L"$LLVM_PREFIX/lib" \
        -Wl,-rpath,"$LLVM_PREFIX/lib" \
        "$SRC" -o "$BIN"
    echo "CB compiled successfully → $BIN"
fi

# Resolve std.cppm path: explicit argument (with slash or .cppm suffix) wins,
# otherwise use LLVM_PATH or defaults per OS.
STD_CPPM=""
if [[ -n "$1" && ("$1" == *.cppm || "$1" == */*) ]]; then
        STD_CPPM="$1"
        shift
fi

if [[ -z "$STD_CPPM" ]]; then
    STD_CPPM="${LLVM_PATH:-$STD_CPPM_DEFAULT}"
fi

if [[ ! -f "$STD_CPPM" ]]; then
    echo "ERROR: std.cppm not found at '$STD_CPPM'."
    echo "Pass the path as the first argument or set LLVM_PATH."
    exit 1
fi

# Find YarDB project root and build include flags
CURRENT_DIR="$(pwd)"
PROJECT_ROOT=""
    CHECK_DIR="$CURRENT_DIR"

# Walk up directory tree to find project root
    while [[ "$CHECK_DIR" != "/" ]]; do
        if [[ -d "$CHECK_DIR/YarDB" && -d "$CHECK_DIR/deps" ]]; then
        PROJECT_ROOT="$CHECK_DIR"
            break
        fi
        CHECK_DIR="$(cd "$CHECK_DIR/.." 2>/dev/null && pwd)"
    done

# Build include flags and link flags if project root found
INCLUDE_FLAGS=()
LINK_FLAGS_STR="-lcrypto"
if [[ -n "$PROJECT_ROOT" ]]; then
        INCLUDE_FLAGS=(
        -I "$PROJECT_ROOT/deps/net/src"
        )
        # Add Homebrew paths on Darwin (macOS)
        if [[ "$UNAME_OUT" == "Darwin" ]]; then
            INCLUDE_FLAGS+=(-I "/opt/homebrew/include")
            LINK_FLAGS_STR="-L/opt/homebrew/lib $LINK_FLAGS_STR"
        fi
fi

# Run it with resolved std.cppm path and include flags
# Add -lcrypto for OpenSSL support (needed for cryptic benchmark)
exec "$BIN" "$STD_CPPM" "${INCLUDE_FLAGS[@]}" --link-flags "$LINK_FLAGS_STR" "$@"

