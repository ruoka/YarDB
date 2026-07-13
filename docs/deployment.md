# YarDB Production Deployment Guide

## 🚀 Production Readiness Status

**Current State**: **Development/Testing** - Not yet production-ready
- ✅ Basic HTTP server with REST API
- ✅ Document storage and retrieval
- ✅ OData query support
- ❌ **Missing**: Security, monitoring, high availability

**Production Requirements** (see [development roadmap](../docs/development.md)):
- 🔐 **Security & Authentication** (JWT, RBAC, TLS)
- 📊 **Monitoring & Observability** (metrics, health checks)
- 🛡️ **Production Hardening** (graceful shutdown, resource limits)

## 🏗️ Building for Production

### Release Build
```bash
# Clean release build with optimizations
./tools/CB.sh release clean
./tools/CB.sh release build

# Binaries available in:
# - macOS: build-darwin-release/bin/
# - Linux: build-linux-release/bin/
```

### Available Programs
- **`yardb`** - Main database server
- **`yarsh`** - Interactive client (supports piped stdin for scripts/CI; see `./tests/yarsh/smoke.sh`)
- **`yarproxy`** - HTTP fan-out proxy (dev/testing; not production HA)
- **`yarexport`** - Offline DB export to JSONL (stop `yardb` first; see `./tests/yarexport/smoke.sh`)
- **`benchmark`** - Performance testing

## 🐳 Container Deployment

### Using Dev Container
```bash
# Build using the provided Dockerfile
docker build -f .devcontainer/Dockerfile -t yardb .

# Run container
docker run -p 2112:2112 -v /data:/data yardb yardb --file=/data/yar.db
```

### Docker Compose Example
```yaml
version: '3.8'
services:
  yardb:
    build: .
    ports:
      - "2112:2112"
    volumes:
      - ./data:/data
    command: ["yardb", "--file=/data/yar.db", "--clog"]
    restart: unless-stopped
```

## ☁️ Cloud Deployment Options

### Option 1: Direct Deployment (Not Recommended for Production)
```bash
# Install dependencies
sudo apt-get update
sudo apt-get install -y clang-21 libc++-21-dev libc++abi-21-dev

# Run server
./yardb --file=/var/lib/yardb/data.db
```

### Option 2: Behind Reverse Proxy (Recommended)
```nginx
# nginx configuration
server {
    listen 443 ssl;
    server_name your-domain.com;

    # SSL/TLS configuration (when available)
    # ssl_certificate /path/to/cert.pem;
    # ssl_certificate_key /path/to/key.pem;

    location / {
        proxy_pass http://localhost:2112;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
    }

    # Health check endpoint
    location /health {
        proxy_pass http://localhost:2112/health;
        access_log off;
    }
}
```

## 🔒 Security Considerations

### Current Capabilities
- **Optional Bearer PAT** — `yardb --pat` / `--pat-file` (SHA-256 hashed in memory; `sha256:` lines in pat-file). When configured, all routes require `Authorization: Bearer <token>` except **`GET /health`**.
- **Public liveness probe** — `GET /health` returns `{"status":"ok"}` even when PAT auth is enabled.

### Current Limitations
- **No TLS/HTTPS** — All traffic is plaintext unless terminated at a reverse proxy
- **Single shared token** — No scoped PATs, JWT/OAuth2, or RBAC yet

### Recommended Security Setup
```bash
# Enable PAT on yardb (or terminate TLS + auth at nginx/Envoy)
yardb --pat-file=/etc/yardb/pat.txt 2112

# Use reverse proxy for TLS termination
# Use network security groups/firewalls
```

### Future Security (Roadmap)
- **Scoped PATs** and token rotation
- **JWT Authentication** with refresh tokens
- **Role-Based Access Control** (RBAC)
- **TLS Proxy Integration** for HTTPS
- **Audit Logging** for security events

## 📊 Monitoring & Observability

### Current Capabilities
- **Liveness probe** — `GET /health` returns `{"status":"ok"}` (public when PAT auth is enabled)
- **Syslog Integration** - Structured logging to system logs
- **Console Logging** - For development/debugging
- **Basic Error Handling** - HTTP status codes and error responses

### Future Monitoring (Roadmap)
```bash
# Available now:
curl http://localhost:2112/health   # Liveness probe (public with PAT auth)

# Planned endpoints:
curl http://localhost:2112/metrics  # Prometheus metrics
curl http://localhost:2112/ready    # Readiness probe
```

### Log Aggregation
```bash
# Current: syslog
yardb --slog_level=6

# Future: JSON structured logging
# Will support correlation IDs and structured data
```

## 💾 Data Management

### Database Files
```bash
# Default location
yardb --file=yar.db

# Custom location
yardb --file=/var/lib/yardb/production.db
```

### Backup Strategy
```bash
# Stop yardb before exporting the file it has open
yarexport --file=production.db > backup.jsonl

# Validate lines (optional)
yarexport --file=production.db | jq -e . >/dev/null

# Smoke test locally: ./tests/yarexport/smoke.sh
```

Automated backup/restore and point-in-time recovery are not implemented yet.

### Storage Requirements
- **Database Files**: FSON-encoded binary format
- **Index Files**: Automatic indexing for query performance
- **Log Files**: Syslog or application logs
- **Temp Space**: For large query operations

## 🚨 High Availability & Scaling

### Current Limitations
- **Single Node**: Each `yardb` process owns one database file; no built-in clustering
- **`yarproxy`**: Round-robin reads and write fan-out only — independent DBs, no sync, no guaranteed consistency (see [programs.md](programs.md#yarproxy---http-fan-out-proxy))
- **No Backup**: Manual export only (`yarexport`)

### Future HA Features (Roadmap)
- **Multi-node Replication** with automatic failover and conflict handling
- **Production-grade proxy** with health checks, partial-failure reporting, and read-your-writes semantics
- **Automated Backups** with retention policies
- **Horizontal Scaling** support

## 🔧 Runtime Requirements

### System Dependencies
- **C++ Runtime**: libc++ (LLVM standard library)
- **Operating System**: Linux/macOS with modern kernel
- **Memory**: Minimum 512MB, recommended 2GB+
- **Storage**: SSD recommended for performance
- **Network**: Stable network for HTTP operations

### Environment Variables
```bash
# Optional: Custom LLVM location
export LLVM_PREFIX=/usr/lib/llvm-20

# Logging configuration
export SYSLOG_LEVEL=6  # Debug level
```

## 🚨 Troubleshooting Production Issues

### Common Problems
```bash
# Check database file corruption
file yar.db  # Should be regular file

# Verify network connectivity
netstat -tlnp | grep :2112

# Check system logs
journalctl -t YarDB --since "1 hour ago"

# Test basic connectivity
curl http://localhost:2112/
```

### Performance Tuning
```bash
# Monitor resource usage
top -p $(pgrep yardb)

# Check file system performance
iostat -x 1

# Network performance
sar -n DEV 1
```

## 📋 Deployment Checklist

- [ ] **Security**: Enable Bearer PAT (`--pat` / `--pat-file`); add TLS at reverse proxy
- [ ] **Monitoring**: Set up metrics collection and alerting
- [ ] **Backup**: Configure regular backup procedures
- [ ] **High Availability**: Plan for redundancy and failover
- [ ] **Performance**: Load test and tune resource limits
- [ ] **Documentation**: Update runbooks and procedures

---

**⚠️ Important**: YarDB is currently in development and should not be used in production environments without implementing the security and monitoring features outlined in the development roadmap.

