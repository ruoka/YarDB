# YarDB - Yet Another RESTful Database

A document-oriented database with RESTful Web API, implemented in C++23.

**Project Structure**: This project follows [P1204R0: Canonical Project Structure](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p1204r0.html) guidelines for C++ projects.

## Overview

YarDB is a C++23 application that implements:
- **RESTful Web API**: HTTP/1.1 based REST interface
- **Document Storage**: FSON-encoded binary storage (FAST-encoded with minimal metadata)
- **JSON Transport**: JSON-encoded objects over HTTP
- **CRUD Operations**: Create, Read, Update, Delete operations via REST endpoints

## Features

- C++23 modules support (`.c++m` extension)
- Cross-platform (Linux, macOS)
- Comprehensive test suite
- Modular architecture
- P1204R0-compliant project structure
- RESTful API following OData principles

## Requirements

### Linux
- Clang 18+ (`/usr/lib/llvm-18/bin/clang++`)
- libc++ development libraries

### macOS
- Homebrew LLVM 19+ (`brew install llvm@19`) or LLVM (`brew install llvm`)
- System clang from Xcode doesn't fully support C++23 modules

## Building

```bash
# Build all programs
make build

# Build and run tests
make tests
make run_tests

# Clean build artifacts
make clean
```

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
│   ├── std/          # Standard library extensions
│   ├── net/          # Network library
│   ├── xson/         # JSON/XML library
│   ├── cryptic/       # Cryptographic functions
│   └── tester/       # Testing framework
├── build/            # Build artifacts (generated)
├── config/           # Build configuration
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
- `std`: Standard library extensions
- `net`: Network library (HTTP server)
- `xson`: JSON/XML parsing
- `cryptic`: Cryptographic functions (SHA1, SHA2, Base64)
- `tester`: Testing framework

## API Endpoints

The RESTful API follows OData principles:

- `GET /` - List all collections
- `POST /{collection}` - Create document
- `GET /{collection}` - Read all documents
- `GET /{collection}/{id}` - Read document by ID
- `PUT /{collection}/{id}` - Replace document
- `PATCH /{collection}/{id}` - Update document
- `DELETE /{collection}/{id}` - Delete document

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
