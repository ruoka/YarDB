# Development Notes

## Quick Reference

### Building
```bash
./tools/CB.sh debug build    # Build all programs in debug mode
./tools/CB.sh release build  # Build in release mode (optimized)
./tools/CB.sh debug test     # Build and run tests
./tools/CB.sh debug clean    # Clean build artifacts
./tools/CB.sh debug list     # List all translation units
```

### Git Submodules
```bash
# Initialize and update submodules
git submodule update --init --recursive --depth 1

# Pull with submodules
git pull --recurse-submodules
```

### Environment Setup

#### macOS
```bash
# Install Homebrew LLVM 20+ (required for C++23 modules and built-in std module)
brew install llvm@20

# Or use latest LLVM (must be 20+)
brew install llvm
```

#### Linux
```bash
# Install Clang 20+
sudo apt-get install clang-20 libc++-20-dev

# Or use LLVM 20 from /usr/lib/llvm-20
```

### Common Tasks
- Add new module: Create `.c++m` file in `YarDB/` directory
- Add new test: Create `.test.c++` file next to source
- Add new program: Create `.c++` file in `YarDB/` directory (C++ Builder will automatically detect it)

## Troubleshooting

### Linking Issues
- Check that all submodules are built: `./tools/CB.sh debug build`
- Verify include paths in `tools/CB.sh` script
- Ensure OpenSSL libraries are available (for cryptic submodule)

### Module Issues
- Ensure module names follow project prefix convention
- Check PCM files are generated in `build-{os}-{config}/pcm/` (e.g., `build-darwin-debug/pcm/`)
- Clean and rebuild if module dependencies are incorrect: `./tools/CB.sh debug clean && ./tools/CB.sh debug build`

### Test DB Artifacts
- Some tests create `*.db` files (and occasionally `*.pid` lock files) in the repo root. If a test fails in a way that suggests stale/corrupt storage state, delete them and re-run tests:

```bash
find . -maxdepth 2 -type f \( -name "*.db" -o -name "*.pid" \) -delete
./tools/CB.sh debug test
```

### Build System
- All build artifacts go to `build-{os}-{config}/` directory (e.g., `build-darwin-debug/`, `build-linux-release/`)
- C++ Builder automatically detects source files and handles dependencies
- Dependencies are in `deps/` directory
- Build configuration is handled by `tools/CB.sh` script

