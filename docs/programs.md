# YarDB Programs

This document describes the various programs included in the YarDB project.

## yardb - Database Server

The main database server that provides a RESTful HTTP API for document storage and retrieval.

### Purpose

`yardb` is the core database server that:
- Accepts HTTP/1.1 requests
- Stores documents in FSON-encoded format
- Provides RESTful CRUD operations
- Manages collections and indexing
- Handles concurrent requests via multi-threaded architecture

### Usage

```bash
yardb [--help] [--clog] [--slog_level=<level>] [--file=<name>] [--bind=<host>] [--pat=<token>] [--pat-file=<path>] [--admin-pat=<token>] [--admin-pat-file=<path>] [service_or_port]
```

### Options

- `--bind=<host>` - Listen address (default: `127.0.0.1`)
  - Use `127.0.0.1` for local development and reverse-proxy deployments
  - Use `--bind=0.0.0.0` for Docker, devcontainers, and Kubernetes port publishing
  - Binding to `0.0.0.0` or `::` **requires** `--pat` or `--pat-file`

- `--file=<name>` - Database file path (default: `yar.db`)
  - The database file stores all collections and documents
  - If the file doesn't exist, it will be created
  - Multiple instances can use different files for separate databases
  - Opening creates an exclusive `{file}.pid` lock; verify no live owner before manually removing a stale lock
  - Startup recovers incomplete tails automatically and refuses structurally corrupt files

- `--clog` - Redirect logging to console (stdout/stderr) instead of syslog
  - Useful for development and debugging
  - Default: logs to syslog

- `--slog_level=<level>` - Set syslog severity level (numeric mask)
  - Controls which log levels are output
  - Default: debug level

- `service_or_port` - Port number or service name
  - Default: `2112`
  - Can be a numeric port (e.g., `8080`) or service name (e.g., `http`)

- `--help` - Display usage information

- `--pat=<token>` - Accept a data-API personal access token (repeatable). Clients send `Authorization: Bearer <token>`.
- `--pat-file=<path>` - Load data-API PATs from a file (one token per line; `#` comments allowed). Lines may be plaintext tokens or `sha256:<hex>` pre-hashed values.
- `--admin-pat=<token>` - Accept an admin-API token for `/_*` maintenance routes (`/_reindex`, `/_db/...`; repeatable).
- `--admin-pat-file=<path>` - Load admin-API PATs from a file (same format as `--pat-file`).

When data PATs are configured, ordinary API routes require a valid Bearer token except **`GET /health`**, **`GET /ready`**, and **`GET /metrics`**. When admin PATs are configured, `/_*` routes require an admin token (data PATs are rejected there); without admin PATs, `/_*` falls back to the data PAT. Tokens are stored in memory as SHA-256 hashes of the full `Authorization` header value (e.g. `Bearer <token>`), not plaintext.

### Security Note

**Default bind:** `127.0.0.1` (loopback only). Suitable for local development and when a reverse proxy connects on localhost.

**Docker / devcontainer:** Port forwarding requires `--bind=0.0.0.0` plus PAT authentication.

**Default API:** No PAT flags → open API on the bind address (development only).

**With `--pat` / `--pat-file`:** Bearer PAT required on data routes. Required for `--bind=0.0.0.0`; recommended for any routable deployment. Use TLS at a reverse proxy in production.

**With `--admin-pat` / `--admin-pat-file`:** Separate tokens for maintenance (`/_reindex`, `/_db/...`). Recommended whenever data PATs are shared with clients that must not reindex or change indexes.

**Future:** JWT/OAuth2, RBAC, finer-grained scopes.

**`yarsh`:** send `@Authorization: Bearer <token>` before the request line.

### Example

```bash
# Local development (loopback only)
yardb

# Docker / devcontainer with host port forwarding
yardb --bind=0.0.0.0 --pat=devtoken 2112

# Start server on port 8080 with custom database file
yardb --file=production.db 8080

# Start server with console logging for debugging
yardb --clog --file=test.db 2112
```

### API Endpoints

Once running, `yardb` provides the following REST endpoints:

- `GET /health` - Liveness probe (`{}`); public even when PAT auth is enabled
- `GET /ready` - Readiness probe (`{}` when ready; `503` + `{"status":"starting|draining|stopped|failed"}` otherwise); public even when PAT auth is enabled
- `GET /` - List all collections
- `POST /{collection}` - Create a new document
- `GET /{collection}` - Read all documents in collection
- `GET /{collection}/{id}` - Read document by ID
- `PUT /{collection}/{id}` - Replace document by ID (upsert: creates if not exists, updates if exists)
- `PATCH /{collection}/{id}` - Partially update an existing document (`404` if missing; does not create)
- `DELETE /{collection}/{id}` - Delete document by ID
- `HEAD /{collection}` - Get collection headers (same as GET but no body)
- `HEAD /{collection}/{id}` - Get document headers (same as GET but no body)
- `GET /$metadata` - OData 4.01 JSON CSDL service metadata
- `GET /_reindex` - Rebuild all collection indexes
- `PUT /_db/{collection}` - Configure secondary index keys for a collection
- `PATCH /_db/{collection}` - Add secondary index keys incrementally

### HTTP Methods and Status Codes

- **POST**: Creates new document → `201 Created` (with `Location` header)
- **GET**: Retrieves document(s) → `200 OK` (or `404 Not Found` if not found)
- **PUT**: Creates or replaces document → `201 Created` (new) or `200 OK` (updated, with `Content-Location` header)
- **PATCH**: Updates an existing document → `200 OK` (with `Content-Location` header) or `404 Not Found`
- **DELETE**: Deletes document → `204 No Content`
- **HEAD**: Returns headers only → `200 OK` (no body)

`PUT /{collection}/{id}` is an upsert. `PATCH /{collection}/{id}` is update-only and never creates a missing resource.

Additional failures include `413 Payload Too Large` for request bodies over 1 MiB and structured `500 Internal Server Error` responses when a database write fails after rollback.

### Response Headers

- **Location**: Included on `POST` and `PUT` (when creating new resources) - `Location: /collection/{id}`
- **Content-Location**: Included on `PUT` (updates) and `PATCH` - `Content-Location: /collection/{id}`
- **ETag**: Included in all GET, HEAD, PUT, PATCH responses - `ETag: "hex-encoded-position"`
- **Last-Modified**: Included in all GET, HEAD, PUT, PATCH responses - `Last-Modified: <RFC 7231 date>`
- **Content-Type**: Always `application/json` for JSON responses

### Conditional Requests

YarDB supports conditional HTTP requests for efficient caching and optimistic locking:

#### ETag-Based Conditionals

- **If-Match**: Used with PUT/PATCH/DELETE
  - Example: `PUT /users/123` with `If-Match: "abc123"`
  - Returns `412 Precondition Failed` if ETag doesn't match (document was modified)
  - Supports wildcard: `If-Match: *` (matches any existing resource)

- **If-None-Match**: Used with GET/HEAD/POST/PUT
  - Example: `GET /users/123` with `If-None-Match: "abc123"`
  - Returns `304 Not Modified` if ETag matches (resource unchanged)
  - Supports wildcard: `If-None-Match: *` (fails if resource exists)

#### Date-Based Conditionals

- **If-Modified-Since**: Used with GET/HEAD
  - Example: `GET /users/123` with `If-Modified-Since: Wed, 21 Oct 2015 07:28:00 GMT`
  - Returns `304 Not Modified` if document hasn't been modified since the date

- **If-Unmodified-Since**: Used with PUT/PATCH/DELETE
  - Example: `PUT /users/123` with `If-Unmodified-Since: Wed, 21 Oct 2015 07:28:00 GMT`
  - Returns `412 Precondition Failed` if document was modified after the date

### OData Query Parameters

YarDB implements OData-compliant query parameters for advanced querying:

- **`$top=n`** - Limit number of results (pagination)
  - Example: `GET /users?$top=10`

- **`$skip=n`** - Skip number of results (pagination)
  - Example: `GET /users?$skip=20`

- **`$orderby=field [asc|desc]`** - Sort by that field (single field; default ascending). Applied after `$filter`, before `$skip`/`$top`.
  - Example: `GET /users?$orderby=age desc`

- **`$filter=expression`** - Filter documents
  - Comparison operators: `eq`, `ne`, `gt`, `ge`, `lt`, `le`
  - Logical operators: `and`, `or` (OData precedence: AND before OR)
  - `in` operator: `status in ('active','pending')` or `id in (1, 2, 3)`
  - Nested paths: `Customer/Country eq 'USA'`, `startswith(Customer/Name, 'Ac')` (nested objects only)
  - String functions:
    - `startswith(field, 'prefix')` — index-backed when `field` is a top-level secondary index key; otherwise post-filter
    - `contains(field, 'substring')`, `endswith(field, 'suffix')` — post-filter only
  - Example: `GET /users?$filter=status ne 'deleted'`, `GET /users?$filter=startswith(name,'A')` (requires `name` indexed)
  - Indexed primitive types remain distinct (`1`, `1.0`, `"1"`, and `true` are separate keys); numeric ranges use numeric ordering

- **`$select=field1,field2`** - Project specific fields
  - Example: `GET /users?$select=name,email`
  - Note: `_id` field is always included

- **`$count=true`** - Return count of matching documents instead of items
  - Returns a JSON number in the response body (e.g. `42`)
  - Works with `$filter` (including `or`, `ne`, and string functions)
  - Uses index-only counting when the filter is a single indexed constraint with `$eq`/`$gt`/`$gte`/`$lt`/`$lte`; otherwise scans candidates and applies `document.match`
  - Example: `GET /users?$count=true`, `GET /users?$count=true&$filter=age%20gt%2025`

- **`$expand=relatedEntity`** - Nest related documents using the v1 relationship convention: navigation name is singular snake_case; field `{singular}_id` on the source document references `_id` in the plural collection (e.g. `$expand=customer` reads `customer_id` → `customers`). Missing targets nest as `null`. Applied before `$select`. Multiple navigations: comma-separated.

### OData Metadata

YarDB supports OData metadata in two ways:

#### 1. Metadata Endpoint

The `GET /$metadata` endpoint returns OData 4.01 JSON CSDL (Common Schema Definition Language) metadata describing all collections and their schemas:

```
GET /$metadata
```

Response (OData 4.01 JSON CSDL format):
```json
{
  "$Version": "4.01",
  "$EntityContainer": "DefaultContainer",
  "EntitySets": [
    {
      "Name": "users",
      "EntityType": "Default.users"
    },
    {
      "Name": "orders",
      "EntityType": "Default.orders"
    }
  ],
  "EntityTypes": [
    {
      "Name": "users",
      "Key": [
        {
          "PropertyRef": [
            {
              "Name": "_id"
            }
          ]
        }
      ],
      "Property": [
        {
          "Name": "_id",
          "Type": "Edm.Int64",
          "Nullable": false
        },
        {
          "Name": "name",
          "Type": "Edm.String",
          "Nullable": true
        },
        {
          "Name": "age",
          "Type": "Edm.Int64",
          "Nullable": true
        },
        {
          "Name": "salary",
          "Type": "Edm.Double",
          "Nullable": true
        },
        {
          "Name": "active",
          "Type": "Edm.Boolean",
          "Nullable": true
        }
      ]
    }
  ]
}
```

The metadata endpoint automatically infers schemas from existing documents in collections. Field types are mapped as follows:
- **Strings** → `Edm.String`
- **Integers** → `Edm.Int64`
- **Floating-point numbers** → `Edm.Double`
- **Booleans** → `Edm.Boolean`
- **Objects and arrays** → `Edm.String` (serialized as JSON strings)

The `_id` field is always included as a non-nullable `Edm.Int64` key field for all entity types.

#### 2. Metadata in Responses

YarDB supports OData metadata formats via the `Accept` header:

- **`application/json;odata=fullmetadata`** - Includes `@odata.context`, `@odata.id`, `@odata.editLink`
- **`application/json;odata=minimalmetadata`** - Includes only `@odata.context`
- **`application/json;odata=nometadata`** or default - Plain JSON (no metadata)

Example request:
```
GET /users/1
Accept: application/json;odata=fullmetadata
```

Response:
```json
{
  "@odata.context": "/$metadata#users/$entity",
  "@odata.id": "/users/1",
  "@odata.editLink": "/users/1",
  "_id": 1,
  "name": "John",
  "email": "john@example.com"
}
```

### Content Negotiation

- YarDB accepts `Accept` header for content type negotiation
- Supported content types: `application/json`, `application/json;odata=fullmetadata`, `application/json;odata=minimalmetadata`
- Returns `406 Not Acceptable` for unsupported content types

## yarsh - Interactive Shell

An interactive command-line client for connecting to and interacting with a yardb server.

### Purpose

`yarsh` provides an interactive shell interface for:
- Testing database operations
- Manual data entry and retrieval
- Debugging and development
- Administrative tasks

### Usage

```bash
yarsh [--help] [URL]
```

### Options

- `URL` - Server URL to connect to (default: `http://localhost:2112`)
- `--help` - Display usage information

### Commands

Once connected, enter an HTTP method and path on one line. For `POST`/`PUT`/`PATCH`, provide JSON on the following line(s). Type `HELP` or `EXIT` at the method prompt.

#### Data Operations

- `POST /collection` - Create a new document (JSON body follows). Optional integer `_id` is allowed when unused; duplicate `_id` returns `409 Conflict`
- `PUT /collection/id` - Replace document by ID (JSON body follows)
- `PATCH /collection/id` - Update an existing document (`404` if missing; JSON body follows)
- `GET /collection/{id}` - Read document by ID
- `GET /collection?...` - Read collection with OData query parameters (`$top`, `$skip`, `$orderby`, `$filter`, `$select`, `$count=true`)
- `HEAD /collection/{id}` - Read headers only (no response body)
- `DELETE /collection/id` - Delete document by ID

#### Administrative Commands

- `GET /health` - Liveness probe (`{}`); public even when PAT auth is enabled
- `GET /ready` - Readiness probe (`{}` when ready; `503` + `{"status":"starting|draining|stopped|failed"}` otherwise); public even when PAT auth is enabled
- `GET /` - List all collections
- `GET /$metadata` - OData JSON CSDL metadata
- `GET /_reindex` - Reindex all collections
- `PUT /_db/collection` - Configure secondary indexes (JSON body: `{"keys":["field"]}`)
- `PATCH /_db/collection` - Add secondary indexes incrementally

#### Optional Request Headers

Before the JSON body (or before the next request for `GET`/`HEAD`/`DELETE`), you may send optional headers — one per line, prefixed with `@`:

```
@Accept: application/json;odata=minimalmetadata
@If-Match: "etag-value"
@If-None-Match: "etag-value"
```

`@Accept` overrides the default `application/json`. Other `@` lines are sent as HTTP headers (names are lowercased).

#### Shell Commands

- `HELP` - Display help text with available commands
- `EXIT` - Exit the shell and close connection

### Example Session

```bash
$ yarsh http://localhost:2112

Enter restful request: GET /
Enter restful request: POST /users
{"name":"John","email":"john@example.com"}

Enter restful request: GET /users/1
Enter restful request: GET /users?$count=true&$filter=status%20ne%20'deleted'
Enter restful request: @If-None-Match: "abc123"
GET /users/1
Enter restful request: EXIT
```

### Smoke tests (piped stdin)

Starts a local `yardb`, pipes commands into `yarsh`, and asserts on status lines, headers, and bodies:

```bash
./tests/yarsh/smoke.sh
./tests/yarsh/smoke.sh --case count
./tests/yarsh/smoke.sh --jsonl   # machine-readable output for CI
```

`tools/cli_test.sh` is a thin wrapper around the same harness. Piped scripts use **one JSON line** per `POST`/`PUT`/`PATCH` body so multiple commands can run in one session.

Cases: `crud`, `put`, `patch`, `count`, `top_skip`, `orderby`, `select`, `filter_eq_gt`, `filter_in`, `filter_ne`, `filter_or`, `filter_startswith`, `head`, `if_none_match`, `bad_json`, `auth_required`, `auth_crud`.

## yarproxy - HTTP Fan-out Proxy

Forwards HTTP requests to multiple independent `yardb` backends. Each backend has its own database file.

### Purpose

`yarproxy` is a **thin HTTP forwarder** for development and testing:
- **Read fan-out** (GET, HEAD): round-robin — one backend per request
- **Write fan-out** (POST, PUT, PATCH, DELETE): same request sent to every backend

It does **not** implement database replication, leader election, or conflict resolution.

### Usage

```bash
yarproxy [--help] [--clog] [--slog_level=<level>] --replica=<URL> [service_or_port]
```

### Options

- `--replica=<URL>` - Add a backend `yardb` URL
  - Repeat for each instance (at least one required)
  - Example: `--replica=http://localhost:2112`

- `--clog` - Redirect logging to console instead of syslog

- `--slog_level=<level>` - Set syslog severity level

- `service_or_port` - Port number for proxy server (default: `2113`)

- `--help` - Display usage information

### Behavior

#### Read Operations (GET, HEAD)
- Round-robin across backends: each request hits **one** backend
- `any_of` retry can reach the next backend if a connection is dead
- Backends may return **different data** (separate DB files)

#### Write Operations (POST, PUT, PATCH, DELETE)
- Request is forwarded to **all** backends
- Client receives **one** response — from the last backend in the list
- Failures on individual backends are not surfaced to the client today
- `_id` values and timestamps may **diverge** across backends

#### Header forwarding
- Forwards end-to-end headers to backends, including **`Authorization`** and **`X-Correlation-ID`**
- Rewrites **`Host`** to each backend's `--replica` host:port
- Strips hop-by-hop headers (`Connection`, `Keep-Alive`, etc.) before forwarding

### Limitations

| Topic | Reality |
|-------|---------|
| Consistency | None — independent databases |
| Partial write failure | Silent — some backends may miss a write |
| Failover | Best-effort on reads only; not HA |
| Production use | Not recommended without external orchestration |

### Example

```bash
# Proxy with two backends, listening on port 2113
yarproxy --replica=http://localhost:2112 --replica=http://localhost:2114 2113

# Three backends, console logging (e.g. tests/yar.sh demo)
yarproxy --clog --replica=http://db1:2112 --replica=http://db2:2112 --replica=http://db3:2112 8080
```

### Use Cases

- **Local testing**: Single client port in front of multiple `yardb` instances (`tests/yar.sh`)
- **Read experiments**: Observe round-robin across independent datasets
- **Write fan-out demos**: Broadcast creates/updates to several empty databases started together

Not suitable for: production HA, guaranteed replication, or strongly consistent reads after writes.

### Smoke tests

```bash
./tests/yarproxy/smoke.sh
./tests/yarproxy/smoke.sh --replicas=5
./tests/yarproxy/smoke.sh --jsonl
./tests/yarproxy/smoke.sh --case write_fanout
```

Cases: `no_replicas`, `help`, `proxy_crud`, `write_fanout`, `read_round_robin`, `header_forward_auth`, `header_forward_correlation`. Default: 2 backends; override with `--replicas=N` or `REPLICA_COUNT=N`.

## yarexport - Data Export Utility

Exports database contents from the FSON-encoded database file to JSONL on stdout.

### Purpose

`yarexport` is a utility for:
- **Data migration**: Export data for backup or migration
- **Debugging**: Inspect database contents in human-readable format
- **Analysis**: Extract data for external analysis tools
- **Recovery**: Export data from corrupted or problematic databases
- **Compaction**: `--live` export of current documents only (pair with `yarimport`)

### Usage

```bash
yarexport [--help] [--file=<name>] [--live]
```

### Options

- `--file=<name>` - Database file to export (default: `yar.db`)
- `--live` - Export only current documents (`status=created`); omit history, tombstones, and file positions
- `--help` - Display usage information

### Output Format

**Full export** (default) includes:
- `collection`, `status`, `timestamp`, `position`, `previous`, `document`

**Live export** (`--live`) includes:
- `collection`, `document` (current versions only, including live `_db` index configs)

Output is JSONL: one JSON object per line (no trailing commas). Stop `yardb` before exporting the same database file.

### Example

```bash
# Export default database file (stop yardb first if it uses yar.db)
yarexport > export.jsonl

# Export specific database file
yarexport --file=production.db > production_export.jsonl

# Live-only export for compaction
yarexport --file=production.db --live > live.jsonl

# Export and filter with jq
yarexport --file=mydb.db | jq 'select(.collection=="users")'
```

### Smoke tests

```bash
./tests/yarexport/smoke.sh
./tests/yarexport/smoke.sh --jsonl
./tests/yarexport/smoke.sh --case export_live
./tests/yarexport/smoke.sh --case compact_roundtrip
```

Cases: `export_empty`, `export_seeded`, `export_live`, `compact_roundtrip`, `missing_file`, `help`.

### Use Cases

- **Backup**: Create JSONL backups of database contents
- **Migration**: Export data for migration to another system
- **Debugging**: Inspect database contents when server is not running
- **Data Analysis**: Extract data for analysis with external tools
- **Recovery**: Export data from databases that cannot be started
- **Compaction**: Reclaim space from updates/deletes via live export + `yarimport`

## yarimport - Offline Import / Compaction

Rebuilds a FSON database from live JSONL produced by `yarexport --live`.

### Purpose

- Offline compaction (drop history and tombstones)
- Restore from a live-only backup
- Preserve `_id` values and secondary index configuration (`_db`)

### Usage

```bash
yarimport [--help] [--file=<name>] [--input=<path>] [--force]
```

### Options

- `--file=<name>` - Output database path (default: `yar.db`)
- `--input=<path>` - JSONL input (default: stdin)
- `--force` - Replace an existing non-empty output file after a successful import
- `--help` - Display usage information

### Compaction workflow

```bash
# 1. Stop yardb
# 2. Export current documents only
yarexport --file=production.db --live > /tmp/live.jsonl

# 3. Import into a new file
yarimport --file=production.db.new --input=/tmp/live.jsonl

# 4. Swap and restart
mv production.db production.db.bak
mv production.db.new production.db
# start yardb --file=production.db
```

`yarimport` validates the full JSONL input first, builds a unique per-pid staging sidecar (`*.yarimport.<pid>.tmp`), then installs it over `--file` only after create/index/reindex succeed (`rename` with `--force`, hard-link otherwise so a newly appearing target is not clobbered). A failed `--force` run therefore leaves any existing database intact. It refuses to run when `{file}.pid` exists (stop yardb first; remove a stale lock only after verifying no live owner). It refuses `status=updated` / `status=deleted` rows (use `--live` export). Secondary indexes are restored from live `_db` rows via `engine.index()`, then `reindex()`.

