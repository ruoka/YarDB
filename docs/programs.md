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
yardb [--help] [--clog] [--slog_level=<level>] [--file=<name>] [service_or_port]
```

### Options

- `--file=<name>` - Database file path (default: `yar.db`)
  - The database file stores all collections and documents
  - If the file doesn't exist, it will be created
  - Multiple instances can use different files for separate databases

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

### Security Note

**⚠️ Security Warning**: Currently, `yardb` provides **no authentication or access control**. All endpoints are publicly accessible. For production use, deploy behind a reverse proxy with proper authentication and TLS termination.

**Future Security Features** (development roadmap):
- JWT-based authentication with refresh tokens
- Role-based access control (RBAC)
- TLS/HTTPS support via proxy integration
- Security audit logging

### Example

```bash
# Start server on default port 2112 with default database file
yardb

# Start server on port 8080 with custom database file
yardb --file=production.db 8080

# Start server with console logging for debugging
yardb --clog --file=test.db 2112
```

### API Endpoints

Once running, `yardb` provides the following REST endpoints:

- `GET /` - List all collections
- `POST /{collection}` - Create a new document
- `GET /{collection}` - Read all documents in collection
- `GET /{collection}/{id}` - Read document by ID
- `PUT /{collection}/{id}` - Replace document by ID (upsert: creates if not exists, updates if exists)
- `PATCH /{collection}/{id}` - Update/Upsert document by ID
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
- **PATCH**: Updates document → `200 OK` (with `Content-Location` header)
- **DELETE**: Deletes document → `204 No Content`
- **HEAD**: Returns headers only → `200 OK` (no body)

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

- **`$orderby=field [desc]`** - Sort results
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

- **`$select=field1,field2`** - Project specific fields
  - Example: `GET /users?$select=name,email`
  - Note: `_id` field is always included

- **`$count=true`** - Return count of matching documents instead of items
  - Returns a JSON number in the response body (e.g. `42`)
  - Works with `$filter` (including `or`, `ne`, and string functions)
  - Uses index-only counting when the filter is a single indexed constraint with `$eq`/`$gt`/`$gte`/`$lt`/`$lte`; otherwise scans candidates and applies `document.match`
  - Example: `GET /users?$count=true`, `GET /users?$count=true&$filter=age%20gt%2025`

- **`$expand=relatedEntity`** - Expand related entities (parsed only; returns documents unchanged until a relationship model exists)

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

- `POST /collection` - Create a new document (JSON body follows)
- `PUT /collection/id` - Replace document by ID (JSON body follows)
- `PATCH /collection/id` - Update/Upsert document by ID (JSON body follows)
- `GET /collection/{id}` - Read document by ID
- `GET /collection?...` - Read collection with OData query parameters (`$top`, `$skip`, `$orderby`, `$filter`, `$select`, `$count=true`)
- `HEAD /collection/{id}` - Read headers only (no response body)
- `DELETE /collection/id` - Delete document by ID

#### Administrative Commands

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

Cases: `crud`, `count`, `filter_ne`, `head`, `if_none_match`, `bad_json`.

## yarproxy - Replication Proxy

A proxy server that forwards requests to replica yardb servers, providing load balancing and replication.

### Purpose

`yarproxy` acts as a:
- **Load balancer** for read operations (GET, HEAD)
- **Replication proxy** for write operations (POST, PUT, PATCH, DELETE)
- **High availability** gateway to multiple database servers

### Usage

```bash
yarproxy [--help] [--clog] [--slog_level=<level>] --replica=<URL> [service_or_port]
```

### Options

- `--replica=<URL>` - Add a replica server URL
  - Can be specified multiple times to add multiple replicas
  - Required: At least one replica must be specified
  - Example: `--replica=http://localhost:2112`

- `--clog` - Redirect logging to console instead of syslog

- `--slog_level=<level>` - Set syslog severity level

- `service_or_port` - Port number for proxy server (default: `2113`)

- `--help` - Display usage information

### Behavior

#### Read Operations (GET, HEAD)
- Requests are load balanced across replicas using **round-robin**
- Each read request goes to the next replica in rotation
- Provides horizontal scaling for read-heavy workloads

#### Write Operations (POST, PUT, PATCH, DELETE)
- Requests are **replicated to all replicas**
- Ensures data consistency across all database instances
- All replicas receive the same write operations

### Example

```bash
# Proxy with two replicas, listening on port 2113
yarproxy --replica=http://localhost:2112 --replica=http://localhost:2114 2113

# Proxy with three replicas, console logging
yarproxy --clog --replica=http://db1:2112 --replica=http://db2:2112 --replica=http://db3:2112 8080
```

### Use Cases

- **High Availability**: Route requests to multiple database servers
- **Read Scaling**: Distribute read load across multiple replicas
- **Data Replication**: Ensure writes are propagated to all replicas
- **Failover**: If one replica fails, others continue serving requests

## yarexport - Data Export Utility

Exports database contents from the FSON-encoded database file to JSON format.

### Purpose

`yarexport` is a utility for:
- **Data migration**: Export data for backup or migration
- **Debugging**: Inspect database contents in human-readable format
- **Analysis**: Extract data for external analysis tools
- **Recovery**: Export data from corrupted or problematic databases

### Usage

```bash
yarexport [--help] [--file=<name>]
```

### Options

- `--file=<name>` - Database file to export (default: `yar.db`)
- `--help` - Display usage information

### Output Format

Each exported document includes:
- `collection` - Collection name
- `status` - Document status
- `timestamp` - ISO8601 formatted timestamp
- `position` - Position in database file
- `previous` - Previous document position
- `document` - The actual document data

Output is JSON format, one document per line (JSONL format).

### Example

```bash
# Export default database file
yarexport > export.json

# Export specific database file
yarexport --file=production.db > production_export.json

# Export and filter with jq
yarexport --file=mydb.db | jq 'select(.collection=="users")'
```

### Use Cases

- **Backup**: Create JSON backups of database contents
- **Migration**: Export data for migration to another system
- **Debugging**: Inspect database contents when server is not running
- **Data Analysis**: Extract data for analysis with external tools
- **Recovery**: Export data from databases that cannot be started

