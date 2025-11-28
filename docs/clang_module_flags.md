# How YarDB Build System Handles C++23 Modules

## Overview

YarDB uses **C++ Builder (CB.sh)** for building, which automatically handles C++23 modules with implicit module building and automatic dependency generation.

## How std.pcm Gets Created

### Automatic Creation with `-fimplicit-modules`

C++ Builder uses `-fimplicit-modules` which tells Clang to automatically build missing modules when they are first imported.

**How it works**:
1. When Clang encounters `import std;` in any source file
2. It first checks for an existing `std.pcm` in the module cache
3. If not found, Clang automatically compiles `std` from libc++ source
4. The compiled `std.pcm` is stored in the build directory's `pcm/` folder
5. Subsequent builds reuse the cached `std.pcm`

**Key flags** (handled automatically by C++ Builder):
- `-fimplicit-modules`: Enables automatic module building
- `-fmodules-cache-path`: Where to STORE auto-generated modules
- `-fprebuilt-module-path`: Where to LOOK for prebuilt modules
- `-fexperimental-library`: Enables experimental std module support from libc++
- `-stdlib=libc++`: Uses libc++ which provides the std module

**No explicit std.pcm build rule**: C++ Builder handles this automatically. Clang builds `std.pcm` on first import.

## How Dependencies Are Handled

### Automatic Dependency Resolution

C++ Builder automatically:
1. **Scans source files** - Discovers all `.c++m`, `.c++`, `.impl.c++`, and `.test.c++` files
2. **Parses module dependencies** - Reads `import` statements to build dependency graph
3. **Topological sorting** - Ensures modules are built before dependents
4. **Incremental builds** - Only rebuilds changed files and their dependents
5. **Module caching** - Caches PCM files for faster subsequent builds

**Process**:

```
./tools/CB.sh debug build
  ↓
C++ Builder scans all source files
  ↓
Parses import statements to build dependency graph
  ↓
Topologically sorts translation units
  ↓
Builds modules in correct order
  ↓
  - Clang automatically builds std.pcm on first import
  - Stored in build-{os}-{config}/pcm/ via -fmodules-cache-path
  ↓
Builds implementation files (.c++, .impl.c++)
  ↓
Links binaries
```

## Build Output Structure

All build artifacts are in `build-{os}-{config}/`:
- `build-{os}-{config}/pcm/` - Precompiled module files (including `std.pcm`)
- `build-{os}-{config}/obj/` - Object files
- `build-{os}-{config}/bin/` - Executable programs
- `build-{os}-{config}/cache/` - Build cache (timestamps, signatures)

Example: `build-darwin-debug/`, `build-linux-release/`

## Benefits of C++ Builder Approach

1. **Automatic std.pcm**: No manual std module build rules needed
2. **Automatic dependencies**: Parses source files to discover all module dependencies
3. **Correct build order**: Topological sorting ensures modules are built before dependents
4. **Incremental builds**: Only rebuilds what changed
5. **Simpler workflow**: Single command handles everything: `./tools/CB.sh debug build`

## Commands

- `./tools/CB.sh debug build` - Build in debug mode (includes tests)
- `./tools/CB.sh release build` - Build in release mode (optimized)
- `./tools/CB.sh debug test` - Build and run tests
- `./tools/CB.sh debug clean` - Remove build directories
- `./tools/CB.sh debug list` - List all translation units

## Troubleshooting

### Module dependency errors
- Clean and rebuild: `./tools/CB.sh debug clean && ./tools/CB.sh debug build`
- Check that all submodules are initialized: `git submodule update --init --recursive`

### std.pcm not found
- Ensure LLVM is installed with libc++ module support (Clang 20+)
- C++ Builder automatically resolves `std.cppm` path from LLVM installation

## References

- [Clang Modules Documentation](https://clang.llvm.org/docs/Modules.html)
- [C++ Builder (tester)](https://github.com/ruoka/tester) - The build system used by YarDB
