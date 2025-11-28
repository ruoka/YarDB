# How YarDB Build System Handles C++23 Modules

## Overview

YarDB uses **C++ Builder (CB.sh)** for building, which automatically handles C++23 modules with implicit module building and automatic dependency generation.

## How std.pcm Gets Created

### Explicit Module File Specification

C++ Builder uses **explicit module files** (`-fno-implicit-modules`) and explicitly builds `std.pcm` from `std.cppm` before compiling any source files.

**How it works**:
1. C++ Builder locates `std.cppm` from the LLVM installation (typically at `$LLVM_PREFIX/include/c++/v1/std.cppm`)
2. It explicitly compiles `std.cppm` to `std.pcm` using `--precompile`
3. The compiled `std.pcm` is stored in the build directory's `pcm/` folder
4. All source files use `-fmodule-file=std=<path>` to explicitly reference this `std.pcm`
5. Subsequent builds check timestamps and only rebuild if `std.cppm` is newer

**Key flags** (handled automatically by C++ Builder):
- `-fno-implicit-modules`: Disables implicit module building (explicit module files required)
- `-fno-implicit-module-maps`: Disables implicit module map discovery
- `-fmodule-file=std=<path>`: Explicitly specifies the std module PCM file path
- `-fprebuilt-module-path=<path>`: Where to LOOK for prebuilt modules (module cache directory)
- `-fexperimental-library`: Enables experimental std module support from libc++
- `-stdlib=libc++`: Uses libc++ which provides the std module

**Explicit std.pcm build**: C++ Builder explicitly builds `std.pcm` from `std.cppm` before compiling any source files, ensuring it's always available.

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
Explicitly builds std.pcm from std.cppm (if needed)
  ↓
Topologically sorts translation units
  ↓
Builds modules in correct order (using -fmodule-file for each module)
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

1. **Explicit module files**: Uses `-fno-implicit-modules` for predictable, explicit module resolution
2. **Automatic dependencies**: Parses source files to discover all module dependencies
3. **Correct build order**: Topological sorting ensures modules are built before dependents
4. **Incremental builds**: Only rebuilds what changed (checks timestamps)
5. **Simpler workflow**: Single command handles everything: `./tools/CB.sh debug build`
6. **Explicit std module**: Builds `std.pcm` explicitly from `std.cppm`, ensuring it's always available

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
