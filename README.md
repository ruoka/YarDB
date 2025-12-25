# YarDB - Yet Another RESTful Database

**YarDB now has a production-ready REST API with proper HTTP semantics, correct status codes, comprehensive error handling, and standard response formats. All critical issues have been resolved.**

A document-oriented database with a fully-featured RESTful Web API, implemented in C++23. YarDB provides enterprise-grade HTTP compliance with support for conditional requests, ETag-based caching, OData query parameters, and content negotiation.

**Project Structure**: This project follows [P1204R0: Canonical Project Structure](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p1204r0.html) guidelines for C++ projects.

## Overview

YarDB is a production-ready C++23 application that implements:

- **Production-Grade REST API**: HTTP/1.1 compliant with proper status codes, error handling, and standard response formats
- **OData Compliance**: Full support for OData query parameters (`$top`, `$skip`, `$orderby`, `$filter`, `$select`, `$expand`) and metadata formats
- **Conditional Requests**: Complete support for `If-Match`, `If-None-Match`, `If-Modified-Since`, and `If-Unmodified-Since` headers
- **ETag & Caching**: Resource versioning with ETag headers for efficient caching and optimistic concurrency control
- **Last-Modified Support**: Timestamp-based conditional requests for cache validation
- **Document Storage**: FSON-encoded binary storage (FAST-encoded with minimal metadata)
- **JSON Transport**: JSON-encoded objects over HTTP with content negotiation
- **CRUD Operations**: Full Create, Read, Update, Delete operations via REST endpoints

## Features

### Production-Ready REST API
- ✅ Proper HTTP status codes (200, 201, 204, 304, 400, 404, 409, 412, 415, 422, 500)
- ✅ Comprehensive error handling with standard error response formats
- ✅ Conditional requests (ETag and Last-Modified based)
- ✅ OData query parameters and metadata support
- ✅ Content negotiation (Accept header, OData metadata levels)
- ✅ Standard HTTP headers (Location, Content-Location, ETag, Last-Modified)

### Technical Excellence
- **Native C++ Build System**: Uses [tester](https://github.com/ruoka/tester) (C++ Builder), a native C++ build system designed for modern C++ projects
- C++23 modules support (`.c++m` extension)
- Cross-platform (Linux, macOS)
- Comprehensive test suite with extensive coverage
- Modular architecture with clean separation of concerns
- P1204R0-compliant project structure
- RESTful API following OData principles

## Requirements

### Compiler
- **Clang 20 or higher** (required for C++23 modules and built-in std module support)
- libc++ development libraries with module support

### Linux
- Clang 20+ (`/usr/lib/llvm-20/bin/clang++`) or later
- libc++ development libraries

### macOS
- LLVM 20+ installed at `/usr/local/llvm/` (not Homebrew)
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
./tools/CB.sh debug test

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
│   Note: std module is built from libc++ source (Clang 20+), not from a submodule
├── build-{os}-{config}/  # Build artifacts (generated, e.g., build-darwin-debug/, build-linux-release/)
├── tools/            # Build tools (CB.sh)
└── docs/             # Documentation
```

**Note**: Unit tests are co-located with source files using `.test.c++` extension, as per P1204R0 Section 7.1.

## Programs

### yardb - Database Server

The main database server that provides a RESTful HTTP API for document storage and retrieval.

**Usage:**
```bash
yardb [--help] [--clog] [--slog_tag=<tag>] [--slog_level=<level>] [--file=<name>] [service_or_port]
```

**Options:**
- `--file=<name>` - Database file path (default: `yar.db`)
- `--clog` - Redirect logging to console instead of syslog
- `--slog_tag=<tag>` - Set syslog application tag
- `--slog_level=<level>` - Set syslog severity level (numeric mask)
- `service_or_port` - Port number or service name (default: `2112`)

**Example:**
```bash
yardb --file=mydb.db 8080
```

### yarsh - Interactive Shell

An interactive command-line client for connecting to and interacting with a yardb server.

**Usage:**
```bash
yarsh [--help] [URL]
```

**Commands:**
- `POST /collection` - Create a new document in collection
- `PUT /collection/id` - Replace document by ID
- `PATCH /collection/id` - Update/Upsert document by ID
- `GET /collection/{id}` - Read document by ID
- `GET /collection` - Read all documents in collection
- `DELETE /collection/id` - Delete document by ID
- `GET /` - List all collections
- `GET /_reindex` - Reindex all collections
- `HELP` - Show help text
- `EXIT` - Exit the shell

**Example:**
```bash
yarsh http://localhost:2112
```

### yarproxy - Replication Proxy

A proxy server that forwards requests to replica yardb servers, providing load balancing for read operations and replication for write operations.

**Usage:**
```bash
yarproxy [--help] [--clog] [--slog_tag=<tag>] [--slog_level=<level>] --replica=<URL> [service_or_port]
```

**Options:**
- `--replica=<URL>` - Add a replica server URL (can be specified multiple times)
- `--clog` - Redirect logging to console
- `--slog_tag=<tag>` - Set syslog application tag
- `--slog_level=<level>` - Set syslog severity level
- `service_or_port` - Port number for proxy server (default: `2113`)

**Behavior:**
- **Read operations** (GET, HEAD): Load balanced across replicas using round-robin
- **Write operations** (POST, PUT, PATCH, DELETE): Replicated to all replicas

**Example:**
```bash
yarproxy --replica=http://localhost:2112 --replica=http://localhost:2114 2113
```

### yarexport - Data Export Utility

Exports database contents from the FSON-encoded database file to JSON format.

## Notes

- **Binary DB format**: YarDB stores documents in a binary format (FSON + metadata). If the database file is corrupted or you point YarDB at the wrong file, you may see errors like “Invalid FSON type encountered during decoding”.
- **HTTP request bodies**: Some HTTP stacks may pass request bodies with trailing `'\0'` bytes. YarDB trims trailing NUL bytes before parsing JSON request bodies to avoid spurious “trailing garbage” parse failures.

**Usage:**
```bash
yarexport [--help] [--file=<name>]
```

**Options:**
- `--file=<name>` - Database file to export (default: `yar.db`)

**Output:**
Exports each document with its metadata (collection, status, timestamp, position, previous) as JSON.

**Example:**
```bash
yarexport --file=mydb.db > export.json
```

## Dependencies

The project uses git submodules for dependencies:
- `net`: Network library (HTTP server)
- `xson`: JSON/XML parsing
- `cryptic`: Cryptographic functions (SHA1, SHA2, Base64)
- `tester`: Testing framework

**Note**: The `std` module is built from libc++ source (provided by Clang 20+), not from a submodule.

## API Endpoints

The RESTful API follows OData principles and supports:

### Basic Endpoints

- `GET /` - List all collections
- `POST /{collection}` - Create document
- `GET /{collection}` - Read all documents (supports OData query parameters)
- `GET /{collection}/{id}` - Read document by ID
- `PUT /{collection}/{id}` - Replace document (upsert)
- `PATCH /{collection}/{id}` - Update document
- `DELETE /{collection}/{id}` - Delete document
- `HEAD /{collection}` or `/{collection}/{id}` - Get headers only

### Advanced Features

- **OData Query Parameters**: `$top`, `$skip`, `$orderby`, `$filter`, `$select`, `$expand`
- **Conditional Requests**: `If-Match`, `If-None-Match`, `If-Modified-Since`, `If-Unmodified-Since`
- **ETag Support**: Resource versioning for caching and optimistic locking
- **Last-Modified**: Timestamp-based conditional requests
- **Content Negotiation**: OData metadata formats (`odata=fullmetadata`, `odata=minimalmetadata`)

See [Programs Documentation](docs/programs.md) for detailed API documentation.

## License

See [LICENSE](LICENSE) for details.

## Project Structure

This project follows [P1204R0: Canonical Project Structure](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p1204r0.html) guidelines. See [Project Organization](docs/project_organization.md) for detailed structure documentation.

## Documentation

See [docs/](docs/) for detailed documentation:
- [Project Organization](docs/project_organization.md) - Project structure and P1204R0 compliance
- [Development Guide](docs/development.md) - Development workflows and quick reference
- [Deployment Guide](docs/deployment.md) - Deployment procedures
- [Programs Documentation](docs/programs.md) - Detailed guide for all programs
