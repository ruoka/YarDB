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
# Initialize and update submodules
git submodule update --init --recursive --depth 1

# Pull with submodules
git pull --recurse-submodules
```

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
- **Current State**: RFC 3164/5424 syslog support, basic console logging
- **Planned Implementation**:
  - **Structured JSON Logging**: Add JSON mode to `syslogstream` class
  - **Prometheus Metrics**: `/metrics` endpoint with request latency, throughput, errors
  - **Health Checks**: `/health`, `/ready` endpoints
  - **Distributed Tracing**: OpenTelemetry integration
  - **Correlation IDs**: Request tracing across components
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

#### 4. 🌐 Net Module Modernization
- **Priority**: MEDIUM
- **Current State**: Header-based C++ library
- **Planned Implementation**:
  - Convert to C++23 modules (remove `.hpp` files)
  - Clean module interface design
  - Update all consumers to use modules
  - Maintain API compatibility during transition
- **Timeline**: 1 month (upcoming work)
- **Impact**: Modernize core networking library, reduce build times

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
- **Logging**: Dual-mode (syslog + structured JSON) for different consumption patterns
- **Metrics**: Prometheus standard for cloud-native monitoring
- **Tracing**: OpenTelemetry for distributed request tracking

### Module Architecture
- **Clean Namespaces**: `yar::*` top-level with clear separation (`db::*` vs `http::*`)
- **C++23 Modules**: Modern module system for better encapsulation and build performance
- **Submodule Independence**: Each dependency maintains its own release cycle

