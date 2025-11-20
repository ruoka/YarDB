# Project Organization

Following [P1204R0: Canonical Project Structure](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p1204r0.html)

## Current Structure

```
YarDB/
├── README.md                    # Main project documentation
├── LICENSE                      # Project license
├── Makefile                     # Main build system
├── .gitignore                   # Git ignore rules
├── .gitmodules                  # Git submodule configuration
│
├── YarDB/                       # Main source directory (P1204R0 compliant)
│   ├── yar.c++m                 # Main yar module
│   ├── yar-engine.c++m         # Database engine module
│   ├── yar-engine.impl.c++      # Engine implementation
│   ├── yar-engine.test.c++      # Unit test (co-located)
│   ├── yar-httpd.c++m           # HTTP server module
│   ├── yar-index.c++m           # Indexing module
│   ├── yar-index.impl.c++       # Index implementation
│   ├── yar-metadata.c++m        # Metadata module
│   ├── yardb.c++                # Main database server executable
│   ├── yarsh.c++                # Shell interface executable
│   ├── yarproxy.c++             # Proxy server executable
│   └── yarexport.c++            # Data export utility executable
│
├── tests/                       # Functional/Integration tests (P1204R0)
│   ├── db/                      # Database tests
│   ├── example.html             # Example/test HTML
│   └── yar.sh                   # Test script
│
├── build-{os}/                  # Build artifacts (gitignored, e.g., build-darwin/, build-linux/)
│   ├── obj/                     # Object files
│   ├── pcm/                     # Precompiled modules
│   ├── bin/                     # Executables
│   └── lib/                     # Static libraries
│
├── config/                      # Configuration files
│   └── compiler.mk             # Shared compiler configuration
│
├── deps/                        # Dependencies (submodules)
│   ├── net/                     # Network library
│   ├── xson/                    # JSON/XML library
│   ├── cryptic/                 # Cryptographic functions
│   └── tester/                 # Testing framework
│
│   Note: std module is built from libc++ source (Clang 20+), not from a submodule
│
└── docs/                        # Documentation
    ├── README.md               # Documentation index
    ├── development.md           # Development guide
    ├── deployment.md           # Deployment guide
    └── archive/                # Historical documentation
```

## P1204R0 Compliance

### ✅ Compliant Aspects

1. **Source Organization (P1204R0 Section 4)**
   - ✅ Source files in project-named directory (`YarDB/`)
   - ✅ Module names start with project prefix (`yar:`, `yar:engine`, `yar:httpd`, etc.)
   - ✅ Namespaces match module names (`yar`, `db`)
   - ✅ Headers and sources together (no `include/`/`src/` split)

2. **Test Organization (P1204R0 Section 7)**
   - ✅ Unit tests co-located with source using `.test.c++` extension
   - ✅ Functional/integration tests in `tests/` subdirectory
   - ✅ Each test is standalone executable

3. **Module Structure (P1204R0 Section 4)**
   - ✅ Module partitions using `:` separator (`yar:engine`, `yar:httpd`, etc.)
   - ✅ Implementation files use `.impl.c++` extension
   - ✅ Module interface files use `.c++m` extension

4. **Build System (P1204R0 Tool Compatibility)**
   - ✅ Build artifacts separated in `build-{os}/` directory
   - ✅ Centralized configuration in `config/`
   - ✅ Shared compiler/platform settings

### Deviations from P1204R0

1. **File Extensions**
   - Using `.c++m`/`.c++` instead of `.mpp`/`.cpp` (acceptable deviation for C++23 modules)

2. **Executables in Source Directory**
   - Executables (`yardb.c++`, `yarsh.c++`, etc.) are in the main source directory
   - P1204R0 suggests separating executables, but keeping them together is pragmatic for this project

3. **Module Subdirectories**
   - Currently all modules are in the root `YarDB/` directory
   - Could organize into subdirectories if module families grow (e.g., `YarDB/http/`, `YarDB/storage/`)

## Module Organization

### Module Families

- **yar**: Main database module
  - `yar` - Core database functionality
  - `yar:engine` - Database engine
  - `yar:httpd` - HTTP server
  - `yar:index` - Indexing system
  - `yar:metadata` - Metadata management

### Dependencies

- **std**: Built-in standard library module (from libc++ source, Clang 20+)
- **net**: Network library (module) - HTTP server implementation
- **xson**: JSON/XML library (module) - Data serialization
- **cryptic**: Cryptographic functions (module) - Hashing and encoding
- **tester**: Testing framework (module) - Unit testing

## Build Artifacts

All build artifacts are generated in the `build-{os}/` directory (e.g., `build-darwin/`, `build-linux/`):

- `build-{os}/bin/` - Executable programs
- `build-{os}/lib/` - Static libraries from dependencies
- `build-{os}/obj/` - Object files
- `build-{os}/pcm/` - Precompiled module files

Override `BUILD_DIR` environment variable to use a custom build directory.

## Configuration

Compiler and platform settings are centralized in `config/compiler.mk`:
- Platform detection (Linux, Darwin, Github CI)
- Compiler selection (Homebrew LLVM, system clang)
- Compiler flags and linker settings
- Shared across all Makefiles

## Dependencies Management

Dependencies are managed as git submodules in `deps/`:
- Each dependency is a separate git submodule
- Dependencies are built with `PREFIX=../../build-{os}` to place artifacts in main `build-{os}/` directory
- Submodule paths are configured in `.gitmodules`

## Testing Strategy

- **Unit Tests**: Co-located with source files (`.test.c++` extension)
  - Example: `yar-engine.test.c++` tests `yar-engine.c++m`
- **Functional Tests**: In `tests/` directory
  - Integration tests and end-to-end scenarios
- **Test Runner**: `test_runner` executable built from all `.test.c++` files

## References

- [P1204R0: Canonical Project Structure](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p1204r0.html)
- Project uses C++23 modules with `.c++m` extension
- Build system follows P1204R0 tool compatibility guidelines

