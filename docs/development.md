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
# Initialize and update submodules (depth 1 to avoid nested submodules)
git submodule update --init --depth 1

# Pull with submodules
git pull --recurse-submodules
```

**Note:** Using `--depth 1` prevents pulling in nested submodules, which avoids multiple tester framework dependencies that could cause conflicts.

### Environment Setup

#### macOS
```bash
# LLVM 21+ at /usr/local/llvm/ (required for C++23 modules; system Xcode clang is insufficient)
# See README.md for install notes
```

#### Linux
```bash
# Clang 21+ (CI and devcontainer use apt.llvm.org — see .devcontainer/Dockerfile)
sudo apt-get install clang-21 libc++-21-dev libc++abi-21-dev lld-21
```

### Common Tasks
- Add new module: Create `.c++m` file in `YarDB/` directory
- Add new unit test: Create `.test.c++` file next to source (tag `[yardb]`)
- Add new program: Create `.c++` file in `YarDB/` directory (C++ Builder will automatically detect it)
- Run piped `yarsh` smoke tests: `./tests/yarsh/smoke.sh` (see `tests/yarsh/smoke.sh --help`)

## Logging Standards

YarDB follows industry-standard structured logging practices (RFC 5424) for consistent observability across all components.

### Log Levels & Usage

| Level | Usage | Examples |
|-------|-------|----------|
| **NOTICE** | Server lifecycle events | Server startup, shutdown, connection accepted |
| **INFO** | Normal operational events | Document created, request completed successfully |
| **DEBUG** | Detailed diagnostic information | Request processing details, DB operations |
| **WARNING** | Warning conditions | Document not found, invalid input |
| **ERROR** | Error conditions | Server startup failures, exceptions, critical errors |

### Message IDs (RFC 5424)

All log entries use structured message IDs for easy filtering and correlation:

- **Server Events**: `SERVER_START`, `SERVER_READY`, `SERVER_STOP`
- **Connection Events**: `CONN_ACCEPT`, `CONN_CLOSED`, `CONN_EXCEPTION`
- **HTTP Events**: `HTTP_REQUEST`, `HTTP_RESPONSE`, `REQUEST_RECEIVED`
- **Document Operations**: `DOCUMENT_CREATED`, `DOCUMENT_NOT_FOUND`, `CREATE_ERROR`
- **System Events**: `ACCEPT_ERROR`, `LISTEN_ERROR`, `STREAM_ERROR`

### Implementation Guidelines

**✅ Correct Usage:**
```cpp
using namespace std::string_view_literals;

// Server lifecycle events with structured fields
slog << notice("SERVER_START") << "Server starting on port: " << port
     << std::pair{"port"sv, port}
     << flush;

// Request processing with structured fields
slog << debug("POST_DOCUMENT") << "POST /[a-z]+: " << request
     << std::pair{"method"sv, "POST"sv}
     << std::pair{"uri"sv, request}
     << std::pair{"collection"sv, collection}
     << std::pair{"body_size"sv, body.size()}
     << flush;

// Success events with structured fields
slog << info("DOCUMENT_CREATED") << "Created document in " << collection << " with id " << id
     << std::pair{"method"sv, "POST"sv}
     << std::pair{"collection"sv, collection}
     << std::pair{"id"sv, id}
     << std::pair{"status"sv, 201}
     << flush;

// Error conditions with structured fields
slog << error("CREATE_ERROR") << "Error creating document: " << e.what()
     << std::pair{"method"sv, "POST"sv}
     << std::pair{"collection"sv, collection}
     << std::pair{"uri"sv, request}
     << std::pair{"status"sv, 400}
     << flush;
```

**❌ Avoid:**
- Using `std::cout` or `printf` for logging
- Missing message IDs on log entries
- Inconsistent log levels for similar events
- Log levels that don't match the severity
- Constructing `std::string` unnecessarily (use `string_view` literals with `sv` suffix)

### Structured Fields

Structured fields provide machine-readable context for log analysis:

- **Network Fields**: `ip`, `port`, `client_ip`, `client_port`
- **HTTP Fields (transport layer)**: `method`, `uri`, `version`, `status`, `content_length`, `request_id`, `duration_ms` — logged by `net::http::server`
- **HTTP Fields (application layer)**: `method`, `uri`, `status`, `content_length`, `correlation_id` — logged by `yar::http::rest_api_server` middleware (`POST_DOCUMENT`, `GET_DOCUMENT`, `HTTP_RESPONSE`, `CREATE_ERROR`, etc.)
- **Application Fields**: `collection`, `id`, `body_size`
- **Error Fields**: `errno`, `error_message`, `error_code`

**Best Practices:**
- Use `std::pair{"key"sv, value}` syntax to avoid unnecessary string construction
- Include relevant context fields (IP, port, method, URI, status codes)
- Use appropriate types (integers for status codes, strings for IDs)
- Keep field names consistent across similar log entries

**Request tracing (shipped)**

YarDB uses two complementary trace identifiers:

| Field | Layer | Source | Use |
|-------|-------|--------|-----|
| `request_id` | `net::http::server` | Per-process counter on each HTTP connection | Transport logs (`HTTP_REQUEST`, net `HTTP_RESPONSE`) |
| `correlation_id` | `yar::http::rest_api_server` | `X-Correlation-ID` header (UUID generated if missing) | Application handler logs and error paths |

Filter application logs on `correlation_id` to follow a request end-to-end (`POST_DOCUMENT` → `HTTP_RESPONSE`). Clients may send `X-Correlation-ID` for cross-service tracing. The engine layer does not emit separate logs.

**Optional future polish:** copy `request_id` into application log lines so both IDs appear on every event (not required for tracing today).

### Log Format

- **Default**: JSONL (JSON Lines) format for better observability tooling integration
- **Alternative**: Syslog (RFC 5424) format available via configuration
- **Output**: Structured JSON with timestamp, host, app name, PID, level, message ID, message, and structured fields

### Testing Log Output

When running tests, logs appear in the test output. Use appropriate log levels for debugging:

```bash
# Run tests with debug logging enabled
./tools/CB.sh debug test --tags="[yardb]"

# Logs are in JSONL format by default, making them easy to parse and filter
```

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
- Engine tests create `*.db` files and atomic `*.pid` lock files in the repo root. A stale lock fails closed; after confirming no test or server owns the database, delete matching artifacts and re-run:

```bash
find . -maxdepth 2 -type f \( -name "*.db" -o -name "*.pid" \) -delete
./tools/CB.sh debug test --tags='\[yardb\]'
```

### Build System
- All build artifacts go to `build-{os}-{config}/` directory (e.g., `build-darwin-debug/`, `build-linux-release/`)
- C++ Builder automatically detects source files and handles dependencies
- Dependencies are in `deps/` directory
- Build configuration is handled by `tools/CB.sh` script

## Development Roadmap

### ✅ Completed Improvements

Current shipped work and verification totals are maintained only in [changelog.md](changelog.md). The entries below retain historical context for the December 2025 architecture work.

#### Namespace Architecture Refactoring
- **Status**: ✅ **COMPLETED** (Dec 2025)
- **Implementation**: Clean separation with `yar::db::*` for database logic and `yar::http::*` for HTTP/REST logic
- **Impact**: Improved maintainability, clearer separation of concerns, easier testing

#### AI-Friendly Build & Test Output
- **Status**: ✅ **COMPLETED** (Dec 2025)
- **Implementation**: JSONL structured output for CI/CD pipelines
- **Features**:
  - `build_start`/`build_end` events with timing
  - `test_start`/`test_end` events with exit codes
  - Deterministic `{"type":"eof"}` stream termination
  - Human-readable logs directed to stderr, machine-readable JSONL to stdout

#### Structured Logging System
- **Status**: ✅ **COMPLETED** (Dec 2025)
- **Implementation**: Replaced `syslogstream` with `structured_log_stream` supporting both syslog (RFC 5424) and JSONL formats
- **Features**:
  - **Dual Format Support**: Syslog (RFC 5424) and JSONL formats, configurable at runtime
  - **Structured Fields**: Support for key-value pairs via `std::pair{"key"sv, value}` syntax
  - **Message IDs**: RFC 5424 compliant message identifiers (e.g., "SERVER_START", "CONNECTION_ACCEPTED")
  - **Backward Compatibility**: Maintains API compatibility with existing `syslogstream` usage
  - **Default JSONL**: JSONL format is now the default for better observability tooling integration
  - **Type Support**: Automatic conversion for integers, floats, strings, booleans, and null values
  - **Thread Safety**: Proper locking for concurrent logging operations
- **Impact**: Improved observability, better integration with log aggregation tools, structured data for analysis

#### Net Module Modernization
- **Status**: ✅ **COMPLETED** (Dec 2025)
- **Implementation**: Converted `net` module from header-based C++ library to C++23 modules
- **Features**:
  - **C++23 Modules**: All `.hpp` files converted to `.c++m` module files
  - **Clean Module Interface**: Proper `export module` and `import` declarations
  - **System Encapsulation**: All POSIX system calls and constants encapsulated in `net:posix` module
  - **Modern C++23 Features**: Uses `std::byteswap` and `std::endian` for byte order conversion
  - **API Compatibility**: Maintained backward compatibility during transition
  - **Module Organization**: Clear separation with module partitions (e.g., `net:http_server`, `net:syslogstream`)
- **Impact**: Modernized core networking library, improved build times, better encapsulation, easier maintenance

#### Test Framework Conventions Refactoring
- **Status**: ✅ **COMPLETED** (Dec 2025)
- **Implementation**: Refactored test files to follow proper tester framework conventions
- **Features**:
  - **BDD Structure**: All BDD tests now use proper nesting (`scenario` -> `given` -> `when` -> `then`)
  - **Test Cases**: Simple tests use `tester::basic::test_case` with `section` subtests
  - **Variable Capture**: Proper use of `std::shared_ptr` for shared state in nested lambdas
  - **Documentation**: Updated testing instructions with best practices and lessons learned
- **Files Refactored**:
  - `net-http_server.test.c++` - 9 scenarios refactored to proper BDD structure
  - `net-http_headers.test.c++` - Converted to `test_case` with `section` subtests
- **Verification**: 226 tests and 702 assertions passed at the time
- **Impact**: Project now serves as a proper example of tester framework usage, improved test readability and maintainability

#### C++ Convention Compliance
- **Status**: ✅ **COMPLETED** (Dec 2025)
- **Implementation**: Comprehensive review and fix of all convention violations
- **Features**:
  - **Naming Conventions**: Removed all `get_`/`set_` prefixes from member functions
  - **Public API**: Updated `get_metadata_timestamp()` → `metadata_timestamp()` and `get_metadata_position()` → `metadata_position()`
  - **Internal Code**: Fixed internal helper functions to follow conventions
  - **Test Code**: Updated test fixtures to use convention-compliant accessors
  - **Exception Handling**: Documented `#include <csignal>` as approved exception (signals defined with macros)
- **Files Modified**:
  - `yar-engine.c++m` - Public API method renames (2 methods)
  - `yar-engine.impl.c++` - Internal helper rename (1 function)
  - `yar-httpd.c++m` - Updated 9 call sites
  - `yar-httpd.test.c++` - Test fixture accessor rename (~175 call sites)
- **Verification**: 226 tests and 702 assertions passed at the time
- **Impact**: Codebase now fully compliant with project coding conventions, improved consistency and maintainability
- **Reference**: See project history and git commits for detailed analysis

### 🚧 Active Development Projects

#### 0. 📊 OData `$filter` enhancements — ✅ largely complete
- **Priority**: DONE (near-term API work)
- **Current State**: `$filter` supports `eq`, `ne`, `gt`, `ge`, `lt`, `le`, `and`, `or`, `in`, nested property paths, and string functions
  - **Index-backed**: `startswith(field,'prefix')` on top-level secondary-indexed fields (prefix range via `$gte`/`$lt`)
  - **Post-filter only**: `contains`, `endswith` (by design — no lexicographic index shortcut)
- **Completed**:
  - ✅ `in`, `or`, nested paths, `ne`, index-backed `startswith`
  - ✅ Multivalue secondary indexes and `std::flat_map` collection map (engine/index layer)
- **Recommended next (API)**: Relationship / navigation model for `$expand` and `$apply`

#### 1. 🔐 Security & Authentication System
- **Priority**: CRITICAL
- **Current State**: Bearer PAT on `yardb` (`--pat` / `--pat-file`, SHA-256 hashed in memory, public `GET /health` and `GET /ready`). `net::http` also has optional Basic auth middleware for custom servers.
- **Shipped** (see [changelog.md](changelog.md)): PAT MVP, hashed storage, auth smokes, safe bind defaults (`--bind`, default `127.0.0.1`, refuse `0.0.0.0`/`::` without PAT)
- **Planned Implementation**:
  - Scoped tokens (read-only vs write)
  - JWT/OAuth2 token authentication
  - Role-based access control (RBAC)
  - Security audit logging
- **Timeline**: 2-3 months
- **Impact**: Required for production deployment

#### 2. 📊 Observability & Monitoring
- **Priority**: HIGH
- **Current State**: ✅ Structured logging with JSONL (default) and syslog (RFC 5424) formats, structured fields support, RFC 5424 message IDs
- **Shipped** (see [changelog.md](changelog.md)):
  - `GET /health` liveness probe (`{}`; public with PAT auth)
  - `GET /ready` readiness probe (`{}`; public with PAT auth)
  - **`correlation_id` request tracing** — `X-Correlation-ID` middleware + logging on all `yar::http::rest_api_server` handlers and error paths (`[yardb]` correlation ID tests)
- **Remaining** (near-term):
  - **Prometheus `/metrics`** — request latency, throughput, errors
  - Engine readiness state so `GET /ready` returns `503` when starting, locked, or shutting down
- **Planned Implementation** (longer-term):
  - **Distributed Tracing**: OpenTelemetry integration (beyond header-based `correlation_id`)
- **Timeline**: 1-2 months
- **Impact**: Essential for production operations and debugging

#### 3. 🔒 TLS Proxy Implementation
- **Priority**: MEDIUM-HIGH
- **Architecture Decision**: TLS proxy approach (not direct TLS in database)
- **Rationale**:
  - Separation of concerns (database vs transport security)
  - Cloud-native (most platforms provide TLS termination)
  - Easier maintenance and testing
  - Performance (specialized TLS proxies)
- **Planned Implementation**:
  - New `net::tls_proxy` module
  - HTTPS termination with forwarding to HTTP server
  - Certificate management
  - Optional client certificate authentication
- **Timeline**: 1-2 months
- **Alternatives Considered**: Direct TLS in database (rejected for architectural reasons)

### 📋 Future Enhancements (Post-MVP)

#### Data Management & Operations
- **Backup/Restore**: Point-in-time recovery, incremental backups
- **Data Migration**: Schema evolution, migration tools
- **Advanced Indexing**: Compound indexes, full-text search
- **Query Optimization**: Query planning and execution optimization

#### Performance & Scalability
- **Connection Pooling**: Efficient connection management
- **Async I/O**: Non-blocking operations
- **Memory Management**: Memory-mapped files, optimized storage
- **Horizontal Scaling**: Multi-node clustering support

##### Engine Reader/Writer Concurrency — ✅ implemented (Jul 2026)

Reader/writer granularity is implemented in the engine; see [changelog.md](changelog.md#july-2026).

- **Explicit collection**: every engine operation takes `collection`; no mutable `m_collection`.
- **Independent readers**: each read opens its own `std::ifstream` so concurrent reads do not share stream cursor or error state.
- **`std::shared_mutex`**: `shared_lock` for `read`, `count`, `history`, `metadata_timestamp`, and `metadata_position`; `unique_lock` for writes and index configuration.
- **Atomic conditional writes**: HTTP `If-Match` / `If-Unmodified-Since` map to `write_preconditions` checked under the same exclusive lock as the mutation.
- **HTTP layer**: `rest_api_server` holds a plain `engine`; external `lockable<engine>` wrapper removed.

The storage writer remains single-owner. A write lock covers file mutation and staged index publication; readers retain their shared lock until selected records are decoded.

#### Developer Experience
- **Configuration Files**: YAML/JSON configuration instead of CLI-only
- **API Documentation**: OpenAPI/Swagger documentation
- **Client SDKs**: Language-specific client libraries
- **Development Tools**: GUI admin interface, query debugger

## Architecture Decisions

### Security Architecture
- **TLS Proxy Pattern**: Transport security handled externally for separation of concerns
- **Bind Policy**: Default `127.0.0.1`; public bind requires an explicit flag; refuse `0.0.0.0` without PAT
- **Authentication Strategy**: JWT-based with refresh tokens for stateless auth
- **Authorization Model**: RBAC with collection/database-level permissions

### Observability Strategy
- **Logging**: ✅ **IMPLEMENTED** - Dual-mode (syslog RFC 5424 + JSONL) with structured fields support, RFC 5424 message IDs, default JSONL format
- **Metrics**: Prometheus standard for cloud-native monitoring (planned)
- **Tracing**: OpenTelemetry for distributed request tracking (planned)

### Module Architecture
- **Clean Namespaces**: `yar::*` top-level with clear separation (`db::*` vs `http::*`)
- **C++23 Modules**: ✅ **IMPLEMENTED** - Modern module system for better encapsulation and build performance (all `net` modules converted from headers)
- **Submodule Independence**: Each dependency maintains its own release cycle

