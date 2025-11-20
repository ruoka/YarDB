# Development Notes

## Quick Reference

### Building
```bash
make build          # Build all programs
make tests          # Build tests
make run_tests      # Run tests
make clean          # Clean build artifacts
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
- Add new program: Add to `programs` variable in `Makefile`

## Troubleshooting

### Linking Issues
- Check that all submodules are built: `make build` (std module is built from libc++ source, not from a submodule)
- Verify library paths in `config/compiler.mk`

### Module Issues
- Ensure module names follow project prefix convention
- Check PCM files are generated in `build/pcm/`

### Build System
- All build artifacts go to `build/` directory
- Dependencies are in `deps/` directory
- Compiler configuration is in `config/compiler.mk`

