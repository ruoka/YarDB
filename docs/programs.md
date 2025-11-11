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
yardb [--help] [--clog] [--slog_tag=<tag>] [--slog_level=<level>] [--file=<name>] [service_or_port]
```

### Options

- `--file=<name>` - Database file path (default: `yar.db`)
  - The database file stores all collections and documents
  - If the file doesn't exist, it will be created
  - Multiple instances can use different files for separate databases

- `--clog` - Redirect logging to console (stdout/stderr) instead of syslog
  - Useful for development and debugging
  - Default: logs to syslog

- `--slog_tag=<tag>` - Set syslog application tag
  - Default: `YarDB`
  - Used for log filtering and identification

- `--slog_level=<level>` - Set syslog severity level (numeric mask)
  - Controls which log levels are output
  - Default: debug level

- `service_or_port` - Port number or service name
  - Default: `2112`
  - Can be a numeric port (e.g., `8080`) or service name (e.g., `http`)

- `--help` - Display usage information

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
- `PUT /{collection}/{id}` - Replace document by ID
- `PATCH /{collection}/{id}` - Update/Upsert document by ID
- `DELETE /{collection}/{id}` - Delete document by ID

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

Once connected, the following commands are available:

#### Data Operations

- `POST /collection` - Create a new document
  - Prompts for JSON document content
  - Example: `POST /users` followed by JSON input

- `PUT /collection/id` - Replace document by ID
  - Completely replaces existing document
  - Example: `PUT /users/123` followed by JSON input

- `PATCH /collection/id` - Update/Upsert document by ID
  - Updates existing fields or creates document if it doesn't exist
  - Example: `PATCH /users/123` followed by JSON input

- `GET /collection/{id}` - Read document by ID
  - Example: `GET /users/123`

- `GET /collection` - Read all documents in collection
  - Example: `GET /users`

- `DELETE /collection/id` - Delete document by ID
  - Example: `DELETE /users/123`

#### Administrative Commands

- `GET /` - List all collections in the database

- `GET /_reindex` - Reindex all collections
  - Rebuilds indexes for all collections
  - Useful after manual database modifications

- `HELP` - Display help text with available commands

- `EXIT` - Exit the shell and close connection

### Example Session

```bash
$ yarsh http://localhost:2112

Currently supported shell commands are:
POST /collection         aka Create
...

Enter restful request: GET /
Enter restful request: POST /users
{"name":"John","email":"john@example.com"}

Enter restful request: GET /users/1
Enter restful request: EXIT
```

## yarproxy - Replication Proxy

A proxy server that forwards requests to replica yardb servers, providing load balancing and replication.

### Purpose

`yarproxy` acts as a:
- **Load balancer** for read operations (GET, HEAD)
- **Replication proxy** for write operations (POST, PUT, PATCH, DELETE)
- **High availability** gateway to multiple database servers

### Usage

```bash
yarproxy [--help] [--clog] [--slog_tag=<tag>] [--slog_level=<level>] --replica=<URL> [service_or_port]
```

### Options

- `--replica=<URL>` - Add a replica server URL
  - Can be specified multiple times to add multiple replicas
  - Required: At least one replica must be specified
  - Example: `--replica=http://localhost:2112`

- `--clog` - Redirect logging to console instead of syslog

- `--slog_tag=<tag>` - Set syslog application tag (default: `YarPROXY`)

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

