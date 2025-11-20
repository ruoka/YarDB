# Contributing to YarDB

Thank you for your interest in contributing to YarDB! This document provides guidelines and instructions for contributing.

## Development Setup

### Prerequisites

- **Clang 20 or higher** with C++23 modules support and built-in std module
- **Make** build system
- **Git** for version control

### Getting Started

1. **Clone the repository:**
   ```bash
   git clone https://github.com/ruoka/YarDB.git
   cd YarDB
   ```

2. **Initialize submodules:**
   ```bash
   git submodule init
   git submodule update
   ```

3. **Build the project:**
   ```bash
   make clean
   make build
   ```

4. **Run tests:**
   ```bash
   make run_tests
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
- Run tests with `make run_tests`
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

- Main Makefile is in the project root
- Compiler configuration is in `config/compiler.mk`
- Build artifacts go to `build-{os}/` directory (e.g., `build-darwin/`, `build-linux/`)
- Submodules are built with `PREFIX=../../build-{os}` to share the same build directory
- Override `BUILD_DIR` environment variable to use a custom build directory

## Questions?

If you have questions or need help, please open an issue on GitHub.

