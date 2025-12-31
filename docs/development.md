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
- Add new program: Create `.c++` file in `YarDB/` directory (C++ Builder will automatically detect it)

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
- **HTTP Fields**: `method`, `uri`, `version`, `status`, `content_length`, `request_id`, `duration_ms`
- **Application Fields**: `collection`, `id`, `body_size`
- **Error Fields**: `errno`, `error_message`, `error_code`

**Best Practices:**
- Use `std::pair{"key"sv, value}` syntax to avoid unnecessary string construction
- Include relevant context fields (IP, port, method, URI, status codes)
- Use appropriate types (integers for status codes, strings for IDs)
- Keep field names consistent across similar log entries

**⚠️ Known Gap: Request ID Correlation**
- **Current State**: The HTTP layer (`net::http::server`) generates and logs `request_id` for all HTTP requests/responses, but application-level logs (e.g., `DOCUMENT_CREATED`, `CREATE_ERROR`) in `yar::http::rest_api_server` do not include `request_id`.
- **Impact**: Cannot directly correlate database operations with HTTP requests in logs (requires manual correlation by timestamp/URI).
- **Future Enhancement**: Pass `request_id` from HTTP layer to application handlers (via headers or request context) to enable end-to-end request tracing.

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
- Some tests create `*.db` files (and occasionally `*.pid` lock files) in the repo root. If a test fails in a way that suggests stale/corrupt storage state, delete them and re-run tests:

```bash
find . -maxdepth 2 -type f \( -name "*.db" -o -name "*.pid" \) -delete
./tools/CB.sh debug test
```

### Build System
- All build artifacts go to `build-{os}-{config}/` directory (e.g., `build-darwin-debug/`, `build-linux-release/`)
- C++ Builder automatically detects source files and handles dependencies
- Dependencies are in `deps/` directory
- Build configuration is handled by `tools/CB.sh` script

## Development Roadmap

### ✅ Completed Improvements

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
- **Verification**: All tests pass (226/226 tests, 702/702 assertions)
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
- **Verification**: All tests pass (226/226 tests, 702/702 assertions)
- **Impact**: Codebase now fully compliant with project coding conventions, improved consistency and maintainability
- **Reference**: See [convention_compliance_report.md](convention_compliance_report.md) for detailed analysis

### 🚧 Active Development Projects

#### 1. 🔐 Security & Authentication System
- **Priority**: CRITICAL
- **Current State**: Basic HTTP Basic auth exists in `net::http` module (minimal credential map)
- **Planned Implementation**:
  - JWT/OAuth2 token authentication
  - Role-based access control (RBAC)
  - Security audit logging
  - Input validation and sanitization
- **Timeline**: 2-3 months
- **Impact**: Required for production deployment

#### 2. 📊 Observability & Monitoring
- **Priority**: HIGH
- **Current State**: ✅ Structured logging with JSONL (default) and syslog (RFC 5424) formats, structured fields support, RFC 5424 message IDs
- **Planned Implementation**:
  - **Prometheus Metrics**: `/metrics` endpoint with request latency, throughput, errors
  - **Health Checks**: `/health`, `/ready` endpoints
  - **Distributed Tracing**: OpenTelemetry integration
  - **Correlation IDs**: Request tracing across components
  - **⚠️ Request ID Correlation**: Pass `request_id` from HTTP layer to application handlers to enable end-to-end request tracing (currently HTTP layer has `request_id`, but application logs don't)
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

#### Developer Experience
- **Configuration Files**: YAML/JSON configuration instead of CLI-only
- **API Documentation**: OpenAPI/Swagger documentation
- **Client SDKs**: Language-specific client libraries
- **Development Tools**: GUI admin interface, query debugger

## Architecture Decisions

### Security Architecture
- **TLS Proxy Pattern**: Transport security handled externally for separation of concerns
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

