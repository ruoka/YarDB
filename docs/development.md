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
# Install Homebrew LLVM (required for C++23 modules)
brew install llvm@19

# Or use latest LLVM
brew install llvm
```

#### Linux
```bash
# Install Clang 19
sudo apt-get install clang-19 libc++-19-dev
```

### Common Tasks
- Add new module: Create `.c++m` file in `YarDB/` directory
- Add new test: Create `.test.c++` file next to source
- Add new program: Add to `programs` variable in `Makefile`

## Troubleshooting

### Linking Issues
- Check that all submodules are built: `make -C deps/std module`
- Verify library paths in `config/compiler.mk`

### Module Issues
- Ensure module names follow project prefix convention
- Check PCM files are generated in `build/pcm/`

### Build System
- All build artifacts go to `build/` directory
- Dependencies are in `deps/` directory
- Compiler configuration is in `config/compiler.mk`

