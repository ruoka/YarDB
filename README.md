# YarDB - Yet Another RESTful Database

A document-oriented database with a RESTful Web API, implemented in C++23. YarDB is intended for development, testing, and controlled single-node deployments; see the [deployment guide](docs/deployment.md) for current operational limitations.

**Project Structure**: This project follows [P1204R0: Canonical Project Structure](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p1204r0.html) guidelines for C++ projects.

## Overview

YarDB is a C++23 application that implements:

- **REST API**: HTTP/1.1 status codes, structured errors, and standard response headers
- **OData Querying**: `$top`, `$skip`, `$orderby`, `$filter`, `$select`, and `$count`; `$expand` is parsed but awaits a relationship model
- **Conditional Requests**: Complete support for `If-Match`, `If-None-Match`, `If-Modified-Since`, and `If-Unmodified-Since` headers
- **ETag & Caching**: Resource versioning with ETag headers for efficient caching and optimistic concurrency control
- **Last-Modified Support**: Timestamp-based conditional requests for cache validation
- **Document Storage**: FSON-encoded binary storage (FAST-encoded with minimal metadata)
- **JSON Transport**: JSON-encoded objects over HTTP with content negotiation
- **CRUD Operations**: Full Create, Read, Update, Delete operations via REST endpoints

## Features

### REST API
- ✅ Proper HTTP status codes (200, 201, 204, 304, 400, 404, 409, 412, 413, 415, 422, 500)
- ✅ Comprehensive error handling with standard error response formats
- ✅ Conditional requests (ETag and Last-Modified based)
- ✅ OData query parameters and metadata support
- ✅ Content negotiation (Accept header, OData metadata levels)
- ✅ Standard HTTP headers (Location, Content-Location, ETag, Last-Modified)

### Technical Excellence
- **Native C++ Build System**: Uses [tester](https://github.com/ruoka/tester) (C++ Builder), a native C++ build system designed for modern C++ projects
- C++23 modules support (`.c++m` extension)
- Cross-platform (Linux, macOS)
- Comprehensive `[yardb]` engine, index, OData, and HTTP integration tests plus CLI smoke harnesses under `tests/`
- Modular architecture with clean separation of concerns
- P1204R0-compliant project structure
- RESTful API following OData principles

## Requirements

### Compiler
- **Clang 21 or higher** (required for C++23 modules and built-in std module support)
- libc++ development libraries with module support

### Linux
- Clang 21+ (`clang++-21`, LLVM at `/usr/lib/llvm-21/`) — CI and devcontainer use apt.llvm.org
- libc++ development libraries (`libc++-21-dev`, `libc++abi-21-dev`)

### macOS
- LLVM 21+ installed at `/usr/local/llvm/` (not Homebrew)
- The build system expects `/usr/local/llvm/bin/clang++` to be available
- System clang from Xcode doesn't fully support C++23 modules

## Building

YarDB uses [tester](https://github.com/ruoka/tester) (C++ Builder), a native C++ build system designed for modern C++ projects with full C++23 module support:

```bash
# Build all programs in debug mode
./tools/CB.sh debug build

# Build in release mode (optimized)
./tools/CB.sh release build

# Build and run tests
./tools/CB.sh debug test --tags='\[yardb\]'

# Clean build artifacts
./tools/CB.sh debug clean

# List all translation units
./tools/CB.sh debug list
```

**Build Output**: Artifacts are generated in `build-<os>-<config>/`:
- `build-<os>-<config>/bin/` - Executable programs
- `build-<os>-<config>/obj/` - Object files
- `build-<os>-<config>/pcm/` - Precompiled module files

Example: `build-darwin-debug/`, `build-linux-release/`

## Project Structure

Following [P1204R0](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p1204r0.html):

```
YarDB/
├── YarDB/            # Source directory (P1204R0 Section 4)
│   ├── yar.c++m      # Main yar module
│   ├── yar-engine.*  # Database engine module
│   ├── yar-httpd.*   # HTTP server module
│   ├── yar-index.*   # Indexing module
│   └── yar-metadata.* # Metadata module
├── tests/            # Functional/integration tests (P1204R0 Section 7)
├── deps/             # Dependencies (submodules)
│   ├── net/          # Network library
│   ├── xson/         # JSON/XML library
│   ├── cryptic/       # Cryptographic functions
│   └── tester/       # Testing framework
│
│   Note: std module is built from libc++ source (Clang 21+), not from a submodule
├── build-{os}-{config}/  # Build artifacts (generated, e.g., build-darwin-debug/, build-linux-release/)
├── tools/            # Build tools (CB.sh)
└── docs/             # Documentation
```

**Note**: Unit tests are co-located with source files using `.test.c++` extension, as per P1204R0 Section 7.1.

## Programs

### yardb - Database Server

The database server and REST API. See [yardb program documentation](docs/programs.md#yardb---database-server) for options, authentication, endpoints, and status codes.

### yarsh - Interactive Shell

An interactive and pipeable HTTP client. See [yarsh program documentation](docs/programs.md#yarsh---interactive-shell).

### yarproxy - HTTP Fan-out Proxy

A development HTTP fan-out proxy for independent `yardb` instances, not replication or HA. See [yarproxy program documentation](docs/programs.md#yarproxy---http-fan-out-proxy).

### yarexport / yarimport - Export, Import, Compaction

Export FSON records as JSONL (`yarexport`, including `--live` for current docs only) and rebuild a database offline (`yarimport`). See [yarexport](docs/programs.md#yarexport---data-export-utility) and [yarimport](docs/programs.md#yarimport---offline-import--compaction).

## Operations

Database locking, recovery behavior, backups, request limits, and production constraints are documented in the [Deployment Guide](docs/deployment.md).

## Dependencies

The project uses git submodules for dependencies:
- `net`: Network library (HTTP server)
- `xson`: JSON/XML parsing
- `cryptic`: Cryptographic functions (SHA1, SHA2, Base64)
- `tester`: Testing framework

**Note**: The `std` module is built from libc++ source (provided by Clang 21+), not from a submodule. `tools/CB.sh` sources shared bootstrap logic from `deps/tester/tools/CB.sh.core`.

## API Endpoints

See the canonical [Programs Documentation](docs/programs.md#api-endpoints) for endpoints, OData behavior, status codes, headers, and examples.

## License

See [LICENSE](LICENSE) for details.

## Documentation

Start at [docs/README.md](docs/README.md) for the full documentation index. Key guides:

- [Programs Documentation](docs/programs.md) - `yardb`, `yarsh`, `yarproxy`, `yarexport`, `yarimport`
- [Development Guide](docs/development.md) - Build, test, roadmap
- [Deployment Guide](docs/deployment.md) - Production deployment
- [Changelog](docs/changelog.md) - Shipped features and smoke coverage
- [Archived REST API evaluation](docs/archive/rest_api_evaluation.md) - Historical design review
- [Contributing](CONTRIBUTING.md) - Setup, style, and PR workflow
- [Agent / CI guide](AGENTS.md) - JSONL build and test triage
