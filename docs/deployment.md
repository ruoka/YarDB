# YarDB Production Deployment Guide

## Target deployment model

YarDB is mainly targeted as microservice persistence. Other uses are fine when they match the same shape (one owner, one or a few datasets). Within that primary case, the aim is still parallel and fault-tolerant YarDB instances for that owner’s dataset.

| Typical assumption (main target) | Consequence |
|----------------------------------|-------------|
| One microservice owns one (or a few related) datasets | Give that owner its own YarDB deployment |
| The owner needs throughput and uptime | Run multiple `yardb` instances for that dataset (parallelism + fault tolerance) |
| Domains are independent | Prefer separate deployments per owner rather than one shared store for many domains |

Keep each owner’s data private where practical (loopback + reverse proxy, or network policy). Stuffing unrelated domains into one deployment “for convenience” is a weaker fit for how YarDB is designed.

**Today vs aim:** each open database file still has one writer (exclusive `{database}.pid` lock). `yarproxy` can fan out HTTP to independent backends for development, but does not yet provide production-grade replication or failover. Parallel / fault-tolerant instances for a given owner’s dataset remain the product direction.

## 🚀 Production Readiness Status

**Current State**: **Development/Testing** - Not yet production-ready for unattended production use
- ✅ Basic HTTP server with REST API
- ✅ Document storage and retrieval
- ✅ OData query support
- ⚠️ **Partial**: Bearer PAT MVP (data + admin for `/_*`), safe bind defaults (`127.0.0.1`, public bind requires PAT), liveness/readiness probes, Prometheus `/metrics` (`method`/`status`/`path`/`scenario`), `correlation_id` tracing, exclusive database locking, startup validation, truncated-tail recovery, and a 1 MiB request limit
- ❌ **Missing for hardened deploys**: JWT/OAuth2, full RBAC, TLS at reverse proxy, richer ops automation
- ❌ **Missing for parallel / fault-tolerant instances**: production-grade replication, failover, and consistent multi-instance semantics (see below)

**Production Requirements** (see [development roadmap](../docs/development.md)):
- 🔐 **Security & Authentication** (data + admin PATs shipped; JWT/RBAC and TLS at reverse proxy still planned; safe bind defaults shipped)
- 📊 **Monitoring & Observability** (Prometheus `/metrics` with path/scenario labels shipped; process metrics / OTel tracing planned; liveness/readiness probes shipped)
- 🛡️ **Production Hardening** (graceful shutdown, resource limits)
- 🔁 **Parallelism & fault tolerance** (multiple instances for one owner’s dataset — the main microservice case)

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
- **`yarexport` / `yarimport`** - Offline JSONL export/import and compaction (`--live`; see `./tests/yarexport/smoke.sh`)
- **`benchmark`** - Performance testing

## 🐳 Container Deployment

### Using Dev Container

The devcontainer forwards ports `2112` and `2113` to the host. Because forwarded traffic does not arrive on container loopback, use a public bind address **with PAT** when you want host access:

```bash
yardb --bind=0.0.0.0 --pat=devtoken --clog 2112
```

For in-container-only work (smoke tests, `yarsh` to `127.0.0.1`), the default `127.0.0.1` bind is sufficient:

```bash
yardb --clog 2112
```

### Docker Image

```bash
# Build using the provided Dockerfile
docker build -f .devcontainer/Dockerfile -t yardb .

# Run container (public bind + PAT required for -p publishing)
docker run -p 2112:2112 -v /data:/data yardb \
  yardb --bind=0.0.0.0 --pat-file=/data/pat.txt --file=/data/yar.db
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
    command: ["yardb", "--bind=0.0.0.0", "--pat-file=/data/pat.txt", "--file=/data/yar.db", "--clog"]
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

    # Probe endpoints (PAT-exempt on yardb)
    location /health {
        proxy_pass http://localhost:2112/health;
        access_log off;
    }

    location /ready {
        proxy_pass http://localhost:2112/ready;
        access_log off;
    }
}
```

## 🔒 Security Considerations

### Current Capabilities
- **Optional Bearer PAT** — `yardb --pat` / `--pat-file` (SHA-256 hashed in memory; `sha256:` lines in pat-file). When configured, data routes require `Authorization: Bearer <token>` except **`GET /health`**, **`GET /ready`**, and **`GET /metrics`**.
- **Admin PAT** — `yardb --admin-pat` / `--admin-pat-file` for `/_*` maintenance routes (`/_reindex`, `/_db/...`). When set, data PATs cannot call those routes.
- **Public probes** — `GET /health` and `GET /ready` are PAT-exempt. `GET /health` always returns `200` + `{}` while the HTTP stack responds. `GET /ready` returns `200` + `{}` only when the server is `ready`; otherwise `503` + `{"status":"starting|draining|stopped|failed"}`.

### Current Limitations
- **No TLS/HTTPS** — All traffic is plaintext unless terminated at a reverse proxy
- **Coarse scopes only** — data vs admin PAT; no JWT/OAuth2 or full RBAC yet

### Recommended Security Setup
```bash
# Enable data + admin PATs on yardb (or terminate TLS + auth at nginx/Envoy)
yardb --pat-file=/etc/yardb/pat.txt --admin-pat-file=/etc/yardb/admin-pat.txt 2112

# Use reverse proxy for TLS termination
# Use network security groups/firewalls
```

### Future Security (Roadmap)
- **Finer-grained scopes** and token rotation
- **JWT Authentication** with refresh tokens
- **Role-Based Access Control** (RBAC)
- **TLS Proxy Integration** for HTTPS
- **Audit Logging** for security events

## 📊 Monitoring & Observability

### Current Capabilities
- **Liveness probe** — `GET /health` returns `{}` (public when PAT auth is enabled)
- **Readiness probe** — `GET /ready` returns `200` + `{}` when `ready`; otherwise `503` + `{"status":...}` (`starting`, `draining`, `stopped`, or `failed`). Public when PAT auth is enabled.
- **Request tracing** — `X-Correlation-ID` on inbound requests; `correlation_id` on application log lines (`POST_DOCUMENT`, `HTTP_RESPONSE`, errors). Transport layer also logs `request_id` per connection.
- **JSONL structured logging** — Default format; use `--clog` for console output during development
- **Syslog Integration** - Structured logging to system logs (`--slog_level`)
- **Basic Error Handling** - HTTP status codes and error responses

### Monitoring endpoints
```bash
curl http://localhost:2112/health   # Liveness probe (public with PAT auth)
curl http://localhost:2112/ready    # Readiness probe (public with PAT auth)
curl http://localhost:2112/metrics  # Prometheus HTTP metrics (public with PAT auth)

# Optional: attribute a request series for benches / ad-hoc profiling
curl -H 'X-Metrics-Scenario: simple' 'http://localhost:2112/perf?$top=20'
```

Labels on `http_requests_total` / `http_request_duration_seconds`: `method`, `status`, `path` (query stripped; digit-only segments → `{id}`), `scenario` (`X-Metrics-Scenario` or `-`). Unique series are capped; surplus paths collapse to `path="_other"`.

### Log Aggregation
```bash
# Console JSONL (development)
yardb --clog --slog_level=7 2112

# Syslog (production)
yardb --slog_level=6 2112

# Filter application logs by trace ID
yardb --clog 2112 2>&1 | grep '"correlation_id":"your-trace-id"'
```

## 💾 Data Management

### Database Files
```bash
# Default location
yardb --file=yar.db

# Custom location
yardb --file=/var/lib/yardb/production.db
```

### Database Lock and Recovery

Opening a database atomically creates `{database}.pid`; only one engine can own a database file. The lock is removed on clean shutdown. If startup reports an existing lock after a crash, first verify that no `yardb` process is using the database, then remove the stale `.pid` file manually.

On startup, YarDB validates record status, positions, and history links. An incomplete final record is safely removed by truncating to the last complete record. Structural corruption fails closed and leaves the file unchanged; restore from a verified backup rather than truncating mid-file data.

### Backup Strategy
```bash
# Stop yardb before exporting the file it has open
yarexport --file=production.db > backup.jsonl

# Compact (drop history/tombstones): live export → import → swap
yarexport --file=production.db --live > /tmp/live.jsonl
yarimport --file=production.db.new --input=/tmp/live.jsonl
mv production.db production.db.bak && mv production.db.new production.db

# Validate lines (optional)
yarexport --file=production.db | jq -e . >/dev/null

# Smoke test locally: ./tests/yarexport/smoke.sh
```

Point-in-time recovery is not implemented yet. Stop `yardb` before copying, exporting, or compacting its open database.

### Storage Requirements
- **Database Files**: FSON-encoded binary format
- **Index Files**: Automatic indexing for query performance
- **Log Files**: Syslog or application logs
- **Temp Space**: For large query operations

## 🚨 Availability & Scaling

### Main target (microservice-shaped ownership)
- **Ownership boundary**: one owner (typically a microservice) → one YarDB deployment for its collections. Prefer separate deployments when domains are independent.
- **Aim per owner**: several parallel, fault-tolerant `yardb` instances for that dataset (throughput + failover).
- **Today**: one writer per open database file (exclusive `.pid` lock); `yarproxy` is HTTP fan-out for independent backends in development — not production replication/HA yet (see [programs.md](programs.md#yarproxy---http-fan-out-proxy))
- **Manual backup/compact**: `yarexport` / `yarimport` (no automated PITR)

### Roadmap (per owner / per microservice)
- Replication and automatic failover among instances that serve the same dataset
- Production-grade proxy: health checks, partial-failure reporting, and clear read/write consistency (including read-your-writes where required)
- Automated backups and retention for each deployment

### Weaker fit (not the main target)
- One shared YarDB for many unrelated domains (monolith-style or multi-tenant enterprise store)
- Expecting enterprise shared-DB features that assume that model

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

# Remove a stale lock only after confirming yardb is not running
pgrep yardb
rm yar.db.pid

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

- [ ] **Security**: Bind to private interfaces (or explicit public bind with PAT); enable Bearer PAT (`--pat` / `--pat-file`); add TLS at reverse proxy
- [ ] **Monitoring**: Set up metrics collection and alerting
- [ ] **Backup**: Configure regular backup procedures
- [ ] **Ownership boundary**: Prefer one YarDB deployment per owner (e.g. per microservice); avoid stuffing unrelated domains into one store unless you accept the weaker fit
- [ ] **Instance lock awareness**: Until multi-instance semantics ship, ensure one process owns each open database file and document the stale-lock runbook
- [ ] **Client limits**: Ensure clients keep request bodies within the 1 MiB limit
- [ ] **Parallelism & fault tolerance**: Plan for multiple instances when the owner needs them; today use explicit ops patterns, and track the HA roadmap above
- [ ] **Performance**: Load test within the expected dataset size for that deployment
- [ ] **Documentation**: Update runbooks and procedures

---

**⚠️ Important**: YarDB is still hardening for production (auth, TLS at the proxy, multi-instance semantics, ops automation). It is mainly targeted at per-owner (typically microservice) persistence with parallel/fault-tolerant instances for that owner’s data — a weaker fit as a shared enterprise database for many domains.

