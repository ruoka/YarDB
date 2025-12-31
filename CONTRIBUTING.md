# Contributing to YarDB

Thank you for your interest in contributing to YarDB! This document provides guidelines and instructions for contributing.

## Development Setup

### Prerequisites

- **Clang 20 or higher** with C++23 modules support and built-in std module
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
   ./tools/CB.sh debug test     # Build and run all tests
   ```

### Using Dev Containers

The project includes a VS Code devcontainer configuration. Simply open the project in VS Code and select "Reopen in Container" when prompted.

## Code Style

The project follows the [C++ Core Guidelines](https://github.com/isocpp/CppCoreGuidelines/blob/master/CppCoreGuidelines.md) for code style and best practices, with the following exception:

**Naming Convention:**
- **All identifiers use `snake_case`** (including class names, functions, variables, etc.)
- This differs from the Core Guidelines' typical PascalCase for classes and camelCase for functions
- Examples:
  - Classes: `database_engine`, `http_server` (not `DatabaseEngine`, `HttpServer`)
  - Functions: `get_data()`, `process_request()` (not `getData()`, `processRequest()`)
  - Variables: `user_name`, `connection_count` (not `userName`, `connectionCount`)

**Other Style Rules:**
- Use 4 spaces for indentation in C++ files
- Follow P1204R0 module organization guidelines
- Keep modules focused and well-documented
- Refer to the C++ Core Guidelines for:
  - [Functions](https://github.com/isocpp/CppCoreGuidelines/blob/master/CppCoreGuidelines.md#s-functions)
  - [Classes and class hierarchies](https://github.com/isocpp/CppCoreGuidelines/blob/master/CppCoreGuidelines.md#s-class)
  - [Resource management](https://github.com/isocpp/CppCoreGuidelines/blob/master/CppCoreGuidelines.md#s-resource)
  - [Error handling](https://github.com/isocpp/CppCoreGuidelines/blob/master/CppCoreGuidelines.md#s-errors)
  - And other sections as applicable

## Module Organization

- Module files use `.c++m` extension
- Implementation files use `.impl.c++` extension
- Test files use `.test.c++` extension (co-located with source)
- Module names start with project prefix (`yar:`, `yar:engine`, etc.)

## Testing

- Unit tests are co-located with source files using `.test.c++` extension
- Run tests with `./tools/CB.sh debug test`
- Run specific tests with `./tools/CB.sh debug test "pattern"`
- Ensure all tests pass before submitting a pull request

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
  - `./tools/CB.sh debug test` - Build and run all tests
  - `./tools/CB.sh debug test "pattern"` - Run filtered tests
  - `./tools/CB.sh debug clean` - Clean build artifacts
  - `./tools/CB.sh debug list` - List all translation units

## Questions?

If you have questions or need help, please open an issue on GitHub.

