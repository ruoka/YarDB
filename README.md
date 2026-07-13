# YarDB - Yet Another RESTful Database

**YarDB now has a production-ready REST API with proper HTTP semantics, correct status codes, comprehensive error handling, and standard response formats. All critical issues have been resolved.**

A document-oriented database with a fully-featured RESTful Web API, implemented in C++23. YarDB provides enterprise-grade HTTP compliance with support for conditional requests, ETag-based caching, OData query parameters, and content negotiation.

**Project Structure**: This project follows [P1204R0: Canonical Project Structure](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p1204r0.html) guidelines for C++ projects.

## Overview

YarDB is a production-ready C++23 application that implements:

- **Production-Grade REST API**: HTTP/1.1 compliant with proper status codes, error handling, and standard response formats
- **OData Compliance**: Full support for OData query parameters (`$top`, `$skip`, `$orderby`, `$filter`, `$select`, `$count`, `$expand`) and metadata formats
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
- Comprehensive test suite (`[yardb]` — 295 tests, 1042 assertions across engine, index, OData, and HTTP integration) plus CLI smoke harnesses under `tests/`
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
│   Note: std module is built from libc++ source (Clang 21+), not from a submodule
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
yardb [--help] [--clog] [--slog_level=<level>] [--file=<name>] [--pat=<token>] [--pat-file=<path>] [service_or_port]
```

**Options:**
- `--file=<name>` - Database file path (default: `yar.db`)
- `--pat=<token>` - Bearer personal access token (repeatable)
- `--pat-file=<path>` - PAT file (plaintext or `sha256:<hex>` lines)
- `--clog` - Redirect logging to console instead of syslog
- `--slog_level=<level>` - Set syslog severity level (numeric mask)
- `service_or_port` - Port number or service name (default: `2112`)

When PAT is configured, all routes require `Authorization: Bearer <token>` except **`GET /health`** (liveness probe). See [Programs Documentation](docs/programs.md#yardb---database-server) for details.

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

**Commands:** HTTP methods with paths (e.g. `GET /users?$count=true`), plus `HELP` and `EXIT`. Supports OData query parameters, `GET /$metadata`, `GET /health`, `PUT`/`PATCH /_db/{collection}` for indexes, and optional `@Header: value` lines for `Accept`, `Authorization`, `If-Match`, and `If-None-Match`.

**Piped mode:** Pipe a script on stdin for automation and CI. Use **one JSON line** per `POST`/`PUT`/`PATCH` body so multiple commands run in one session. Invalid JSON on a body prints an error and the shell continues.

**Example:**
```bash
yarsh http://localhost:2112
# GET /users?$filter=status%20ne%20'deleted'
# @If-None-Match: "etag"
# GET /users/1

# Piped smoke test (starts ephemeral yardb)
./tests/yarsh/smoke.sh
```

See [Programs Documentation](docs/programs.md#yarsh---interactive-shell) for details.

### yarproxy - HTTP Fan-out Proxy

A development proxy that forwards HTTP to multiple independent `yardb` instances. Each replica uses its own database file — this is **not** built-in replication or HA.

**Usage:**
```bash
yarproxy [--help] [--clog] [--slog_level=<level>] --replica=<URL> [service_or_port]
```

**Options:**
- `--replica=<URL>` - Add a backend `yardb` URL (repeat for each instance)
- `--clog` - Redirect logging to console
- `--slog_level=<level>` - Set syslog severity level
- `service_or_port` - Port number for proxy server (default: `2113`)

**Behavior:**
- **Read operations** (GET, HEAD): Round-robin to one backend per request
- **Write operations** (POST, PUT, PATCH, DELETE): Best-effort fan-out to every backend; the client sees one response (from the last backend contacted)

**Limitations:** No sync protocol, no partial-failure reporting, no strong consistency. Suitable for local multi-instance testing, not production multi-node deployment.

**Example:**
```bash
yarproxy --replica=http://localhost:2112 --replica=http://localhost:2114 2113
./tests/yarproxy/smoke.sh --replicas=5
```

See [Programs Documentation](docs/programs.md#yarproxy---http-fan-out-proxy) for details.

### yarexport - Data Export Utility

Exports records from an FSON database file to **JSONL** on stdout (one compact JSON object per line).

**Usage:**
```bash
yarexport [--help] [--file=<name>]
```

**Options:**
- `--file=<name>` - Database file to export (default: `yar.db`)

**Output:**
Each line includes metadata (`collection`, `status`, `timestamp`, `position`, `previous`) and `document`. Stop `yardb` before exporting the same file it has open.

**Example:**
```bash
yarexport --file=mydb.db > export.jsonl
yarexport --file=mydb.db | jq 'select(.collection=="users")'
./tests/yarexport/smoke.sh
```

See [Programs Documentation](docs/programs.md#yarexport---data-export-utility) for details.

## Notes

- **Binary DB format**: YarDB stores documents in a binary format (FSON + metadata). If the database file is corrupted or you point YarDB at the wrong file, you may see errors like “Invalid FSON type encountered during decoding”.
- **HTTP request bodies**: Some HTTP stacks may pass request bodies with trailing `'\0'` bytes. YarDB trims trailing NUL bytes before parsing JSON request bodies to avoid spurious “trailing garbage” parse failures.

## Dependencies

The project uses git submodules for dependencies:
- `net`: Network library (HTTP server)
- `xson`: JSON/XML parsing
- `cryptic`: Cryptographic functions (SHA1, SHA2, Base64)
- `tester`: Testing framework

**Note**: The `std` module is built from libc++ source (provided by Clang 21+), not from a submodule. `tools/CB.sh` sources shared bootstrap logic from `deps/tester/tools/CB.sh.core`.

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
- `GET /$metadata` - Get OData 4.01 JSON CSDL service metadata

### Advanced Features

- **OData Query Parameters**: `$top`, `$skip`, `$orderby`, `$filter`, `$select`, `$count`, `$expand` (placeholder)
- **`$filter` operators**: `eq`, `ne`, `gt`, `ge`, `lt`, `le`, `and`, `or`, `in`, `startswith`, `contains`, `endswith`; nested paths (e.g. `Customer/Country eq 'USA'`); index-backed `startswith` on top-level secondary keys
- **`$count=true`**: Index-only count when possible; scan fallback for `$ne`, OR, and string post-filters
- **Secondary indexes**: `PUT`/`PATCH /_db/{collection}` with `{"keys":["field",...]}`
- **OData Metadata Endpoint**: `GET /$metadata` returns OData 4.01 JSON CSDL metadata with inferred schemas
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

Start at [docs/README.md](docs/README.md) for the full documentation index. Key guides:

- [Programs Documentation](docs/programs.md) - `yardb`, `yarsh`, `yarproxy`, `yarexport`
- [Development Guide](docs/development.md) - Build, test, roadmap
- [Deployment Guide](docs/deployment.md) - Production deployment
- [Changelog](docs/changelog.md) - Shipped features and smoke coverage
- [REST API evaluation](docs/rest_api_evaluation.md) - API status and prioritized TODO
- [Contributing](CONTRIBUTING.md) - Setup, style, and PR workflow
- [Agent / CI guide](AGENTS.md) - JSONL build and test triage
