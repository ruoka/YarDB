# Contributing to YarDB

Thank you for your interest in contributing to YarDB! This document provides guidelines and instructions for contributing.

## Development Setup

### Prerequisites

- **Clang 21 or higher** with C++23 modules support and built-in std module
- **Git** for version control

### Getting Started

1. **Clone the repository:**
   ```bash
   git clone https://github.com/ruoka/YarDB.git
   cd YarDB
   ```

2. **Initialize submodules:**
   ```bash
   git submodule update --init --depth 1
   ```
   
   **Note:** Using `--depth 1` prevents pulling in nested submodules, which avoids multiple tester framework dependencies that could cause conflicts.

3. **Build the project:**
   ```bash
   ./tools/CB.sh debug build    # Build in debug mode
   ./tools/CB.sh release build  # Build in release mode (optimized)
   ```

4. **Run tests:**
   ```bash
   ./tools/CB.sh debug test --tags='\[yardb\]'
   ```

### Using Dev Containers

The project includes a VS Code devcontainer configuration. Simply open the project in VS Code and select "Reopen in Container" when prompted.

## C++ Coding Standards

YarDB targets C++23 and follows the [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines), except for the project naming rules below.

### Naming

- Use `snake_case` for every identifier, including types, functions, variables, and members.
- Use `UPPER_SNAKE_CASE` for constants and enumerators where an uppercase name is appropriate.
- Do not use Java-style `get_` or `set_` accessors. Prefer `size()`, `id()`, `collection()`, `update_state()`, and similar natural names.
- Module partitions use the `yar:` prefix.

### Source Style

- Use exactly four spaces for indentation.
- Production `.c++m` and `.c++` code uses Allman braces.
- Compact K&R-style test registration lambdas are acceptable in `.test.c++`; control-flow braces remain Allman style.
- Prefer `auto` when the initializer makes the type clear.
- Prefer `"value"s` and `"value"sv` with `auto` for owning strings and string views.
- Avoid trailing return types unless required by dependent template syntax.
- Prefer concise range algorithms and range-based loops over index loops and hand-written equivalents.
- Prefer the standard library over project-specific helpers when it expresses the operation clearly.

### Design and Error Handling

- Use RAII for ownership and cleanup; do not introduce raw owning pointers.
- Use `std::expected` for recoverable operation failures and exceptions for construction failures or violated invariants.
- Keep behavior-preserving refactors compatible with existing tests; do not change assertions to hide regressions.
- Keep modules focused and APIs small. Remove redundant compatibility paths when all callers can migrate together.
- Prefer clear return values over output parameters for new APIs.

## Module Organization

- Use modules instead of traditional headers. A global module fragment with a platform header is acceptable only when standard C++ has no equivalent.
- Module files use `.c++m` extension
- Implementation files use `.impl.c++` extension
- Test files use `.test.c++` extension (co-located with source)
- Module partitions use names such as `yar:engine`, `yar:index`, and `yar:httpd`

## Testing

- Unit tests are co-located with source files using `.test.c++` extension
- Tag YarDB tests with `[yardb]`
- Run scoped tests while developing: `./tools/CB.sh debug test "pattern" --jsonl=failures`
- Run the complete suite with `./tools/CB.sh debug test --jsonl=failures --tags='\[yardb\]'`
- Run `./tools/CB.sh release test --tags='\[yardb\]'` before committing
- Use `require_*` when later assertions depend on success and `check_*` for independent checks
- Nested test sections execute later; capture shared fixtures by value, normally through `std::shared_ptr`

## Submitting Changes

1. **Create a branch:**
   ```bash
   git checkout -b feature/your-feature-name
   ```

2. **Make your changes** and ensure tests pass

3. **Commit your changes:**
   ```bash
   git add .
   git commit -m "Description of your changes"
   ```

4. **Push and create a pull request**

## Build System

- Uses C++ Builder (`./tools/CB.sh`) - a pure C++ build system
- Build artifacts go to `build-{os}-{config}/` directory (e.g., `build-darwin-debug/`, `build-linux-release/`)
- Automatic module dependency resolution and caching
- Parallel builds supported
- Common commands:
  - `./tools/CB.sh debug build` - Build in debug mode
  - `./tools/CB.sh release build` - Build in release mode
  - `./tools/CB.sh debug test --tags='\[yardb\]'` - Build and run all YarDB tests
  - `./tools/CB.sh debug test "pattern"` - Run filtered tests
  - `./tools/CB.sh debug clean` - Clean build artifacts
  - `./tools/CB.sh debug list` - List all translation units

## Questions?

If you have questions or need help, please open an issue on GitHub.

