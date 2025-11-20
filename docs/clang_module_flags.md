# How YarDB Build System Handles C++23 Modules

## Overview

YarDB uses **implicit modules** with automatic dependency generation. This document explains how `std.pcm` is created and how module dependencies are handled.

## How std.pcm Gets Created

### Automatic Creation with `-fimplicit-modules`

YarDB uses `-fimplicit-modules` which tells Clang to automatically build missing modules when they are first imported.

**Configuration** (from `config/compiler.mk` and `Makefile`):
```makefile
# Enable implicit module building
PCMFLAGS_COMMON = -fimplicit-modules
PCMFLAGS_PRECOMPILE = -fmodules-cache-path=$(moduledir)
```

**How it works**:
1. When Clang encounters `import std;` in any source file
2. It first checks `-fprebuilt-module-path=$(moduledir)/` for an existing `std.pcm`
3. If not found, Clang automatically compiles `std` from libc++ source
4. The compiled `std.pcm` is stored in `$(moduledir)` (via `-fmodules-cache-path`)
5. Subsequent builds reuse the cached `std.pcm`

**Key flags**:
- `-fimplicit-modules`: Enables automatic module building
- `-fmodules-cache-path=$(moduledir)`: Where to STORE auto-generated modules
- `-fprebuilt-module-path=$(moduledir)/`: Where to LOOK for prebuilt modules
- `-fexperimental-library`: Enables experimental std module support from libc++
- `-stdlib=libc++`: Uses libc++ which provides the std module

**No explicit std.pcm build rule**: Unlike explicit module approaches, there's no Makefile rule like `$(moduledir)/std.pcm: std.cppm`. Clang handles it automatically.

## How Dependencies Are Handled

### Automatic Dependency Generation with `clang-scan-deps`

YarDB uses `clang-scan-deps` to automatically discover module dependencies.

**Process**:

1. **Generate dependency graph** (`Makefile` lines 216-221):
   ```makefile
   $(module_depfile): $(all_sources) scripts/parse_module_deps.py
       @$(clang_scan_deps) -format=p1689 \
           -module-files-dir $(moduledir) \
           -- $(CXX) $(CXXFLAGS) $(PCMFLAGS_BUILD) $(all_sources) | \
       python3 scripts/parse_module_deps.py $(moduledir) $(objectdir) > $@
   ```

2. **What `clang-scan-deps` does**:
   - Scans all source files (`$(all_sources)`)
   - Uses `-format=p1689` to output JSON dependency information
   - For each file, identifies:
     - What modules it provides (if it's a module interface)
     - What modules it requires (imports)
   - Outputs JSON with module dependency graph

3. **Python script parses JSON** (`scripts/parse_module_deps.py`):
   - Reads p1689 JSON from `clang-scan-deps`
   - Generates Makefile dependency rules
   - For each module interface (`.c++m`):
     - Creates rule: `module.pcm module.o: source.c++m`
     - Adds module dependencies: `module.pcm: dep1.pcm dep2.pcm`
   - For each implementation (`.c++`, `.impl.c++`):
     - Creates rule: `file.o: source.c++ module.pcm`
     - Adds module dependencies: `file.o: dep1.pcm dep2.pcm`

4. **Include generated dependencies** (`Makefile` line 224):
   ```makefile
   -include $(module_depfile)
   ```
   - Make reads the generated `modules.dep` file
   - Now knows the complete module dependency graph
   - Ensures modules are built in correct order

5. **All compilation rules depend on `$(module_depfile)`**:
   ```makefile
   $(moduledir)/%.pcm: $(sourcedir)/%.c++m $(submodule_module_stamps) $(module_depfile)
   $(objectdir)/%.o: $(sourcedir)/%.c++ $(module_depfile)
   ```
   - Ensures dependencies are generated before compilation
   - Make knows module build order from the dependency graph

### Dependency Flow

```
make build
  ↓
Generate $(module_depfile) via clang-scan-deps
  ↓
Include $(module_depfile) - Make now knows module dependencies
  ↓
Build submodule modules (net, xson, etc.) first
  ↓
Build main project modules (yar, yar:engine, etc.)
  ↓
  - Clang automatically builds std.pcm on first import
  - Stored in $(moduledir) via -fmodules-cache-path
  ↓
Build implementation files (.c++, .impl.c++)
  ↓
Link binaries
```

## Key Variables

- `module_depfile = $(moduledir)/modules.dep` - Single file with all module dependencies
- `clang_scan_deps` - Path to clang-scan-deps tool (from same dir as CXX)
- `all_sources` - All source files scanned for dependencies
- `PCMFLAGS_COMMON` - Module flags used during compilation
- `PCMFLAGS_PRECOMPILE` - Additional flags for precompile operations

## Benefits of This Approach

1. **Automatic std.pcm**: No manual std module build rules needed
2. **Automatic dependencies**: `clang-scan-deps` discovers all module dependencies
3. **Single dependency file**: One `modules.dep` file instead of per-file `.d` files
4. **Correct build order**: Make ensures modules are built before dependents
5. **Simpler Makefile**: Less manual dependency tracking

## References

- [Clang Modules Documentation](https://clang.llvm.org/docs/Modules.html)
- [clang-scan-deps](https://clang.llvm.org/docs/ClangCommandLineReference.html#cmdoption-clang-scan-deps)
- [P1689R5: Format for describing dependencies of source files](https://wg21.link/p1689r5)
