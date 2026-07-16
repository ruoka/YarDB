# YarDB Changelog

Shipped features, fixes, and test harness work. **Newest first** within each month.

For planned work see [development.md](development.md#development-roadmap). Historical design evaluations live under [archive/](archive/).

---

## July 2026

### Security

| Item | Summary |
|------|---------|
| **Admin PAT for `/_*`** | `--admin-pat` / `--admin-pat-file` protect maintenance routes (`/_reindex`, `/_db/...`); data PATs rejected there when admin PATs are configured; without admin PATs, `/_*` still accepts the data PAT |

### Observability

| Item | Summary |
|------|---------|
| **Prometheus `GET /metrics`** | Minimum HTTP metrics via `net::http::middleware::metrics_middleware` — `http_requests_total` and `http_request_duration_seconds` (`method`, `status` labels); public with PAT; scrapes not counted |

### Platform & tooling

| Item | Summary |
|------|---------|
| **`deps/tester` on `main`** | Submodule pinned to `23f77d7` — unified `--jsonl=failures|summary|trace` modes and observer-owned output filtering |

### Safe bind defaults

| Item | Summary |
|------|---------|
| **Default loopback bind** | `yardb` listens on `127.0.0.1` by default |
| **`--bind=<host>`** | Explicit listen address; use `--bind=0.0.0.0` for Docker/devcontainer port forwarding |
| **Public bind policy** | Refuses `0.0.0.0` and `::` without `--pat` / `--pat-file` |
| **Tests** | `tests/yardb/smoke.sh` bind-policy smoke harness |

### Server readiness probes

| Item | Summary |
|------|---------|
| **Lifecycle state** | `rest_api_server` tracks `stopped`, `starting`, `ready`, `draining`, `failed` |
| **`GET /ready`** | `200` + `{}` only when `ready`; otherwise `503` + `{"status":...}` |
| **`GET /health`** | Liveness-only `200` + `{}` while the HTTP stack can respond |
| **Tests** | Readiness and draining probe cases in `yar-httpd.test.c++` |

### Engine reader/writer concurrency

| Item | Summary |
|------|---------|
| **Explicit collection API** | Removed mutable `m_collection`; every engine operation takes `collection` explicitly (`b5cd61d`) |
| **Reader/writer locking** | `std::shared_mutex` with independent `std::ifstream` per read; HTTP layer no longer wraps `lockable<engine>` |
| **Atomic conditional writes** | `write_preconditions` (`If-Match`, `If-Unmodified-Since`) checked under the same exclusive lock as create/update/replace/destroy |
| **Tests** | Concurrent reader and stale-precondition engine tests; `[yardb]` suite at **322 tests and 1178 assertions** |

### Storage, engine & HTTP hardening

| Item | Summary |
|------|---------|
| **Transactional write errors** | Engine writes return `std::expected`; failed writes restore metadata and file size before HTTP reports a structured `500 Internal Server Error` (`d3d9c9d`) |
| **Typed secondary indexes** | Secondary keys preserve `xson::primitive` types, preventing stringification collisions and giving numbers native range ordering (`7ab64d2`) |
| **PATCH semantics** | `PATCH /{collection}/{id}` is update-only and returns `404 Not Found` when missing; `PUT` remains upsert (`ec911f6`) |
| **Request body limit** | Shared `net::http` middleware rejects bodies over 1 MiB with `413 Payload Too Large` (`fc336fc`) |
| **Database locking & recovery** | Engine-owned atomic `.pid` lock; strict startup validation; incomplete tails recover to the last complete record; structural corruption fails closed (`707abd2`) |
| **Tests** | `[yardb]` suite expanded to **318 tests and 1157 assertions** |

### yarproxy

| Item | Summary |
|------|---------|
| **Header forwarding** | `Authorization`, `X-Correlation-ID`, and other end-to-end headers forwarded to backends; `Host` rewritten per replica; hop-by-hop headers stripped |
| **Smoke tests** | `header_forward_auth` (PAT through proxy), `header_forward_correlation` (trace ID in replica logs) |

### Auth & security

| Item | Summary |
|------|---------|
| **Bearer PAT authentication** | `yardb --pat` / `--pat-file`; all routes protected via `net` `authentication_middleware` (`5236cab`) |
| **Hashed PAT storage** | Tokens stored as SHA-256 of full `Authorization` header; `--pat-file` accepts `sha256:<hex>` lines (`48a4ac0`) |

### Observability

| Item | Summary |
|------|---------|
| **`GET /health` liveness probe** | Public when PAT is enabled; reserved collection name; initial `{"status":"ok"}` (`48a4ac0`); body changed to `{}` (`d1bab4b`) |
| **`GET /ready` readiness probe** | Initial `{}` stub (`d1bab4b`); superseded by lifecycle-aware `503` + `{"status":...}` when not `ready` (see **Server readiness probes** above) |
| **`correlation_id` request tracing** | `X-Correlation-ID` middleware; logged on handler entry (`POST_DOCUMENT`, etc.), `HTTP_RESPONSE`, and error paths in `yar-httpd.c++m` |
| **Transport `request_id`** | `net::http::server` per-connection counter on `HTTP_REQUEST` / net `HTTP_RESPONSE` (complements `correlation_id`) |

### CLI smoke tests (CI)

| Harness | Cases / coverage |
|---------|------------------|
| **`tests/yarsh/smoke.sh`** | `crud`, `put`, `patch`, `count`, `top_skip`, `orderby`, `select`, `filter_eq_gt`, `filter_in`, `filter_ne`, `filter_or`, `filter_startswith`, `head`, `if_none_match`, `bad_json`, `auth_required` (health + ready PAT-exempt, `{}` body), `auth_crud` |
| **`tests/yarexport/smoke.sh`** | `export_empty`, `export_seeded`, `missing_file`, `help` |
| **`tests/yarproxy/smoke.sh`** | `no_replicas`, `help`, `proxy_crud`, `write_fanout`, `read_round_robin`, `header_forward_auth`, `header_forward_correlation`; `--replicas=N` |

Earlier July commits: piped yarsh harness (`031d52f`), yarexport JSONL fix + smokes (`7066ee1`), yarproxy smokes (`7ebe299`, `22b8400`), health/ready probes (`d1bab4b`).

### OData `$filter` & query

| Operator / feature | Notes |
|--------------------|-------|
| **`$filter` `in`** | `status in ('active','pending')`; yarsh smoke (`420e31b`) |
| **`$filter` `or`** | Multi-branch read merged by `_id`; yarsh smoke (`437007b`) |
| **`$filter` `startswith`** | Index-backed on secondary keys; yarsh smoke (`437007b`) |
| **OData query smokes** | `$top`, `$skip`, `$orderby`, `$select`, `$filter` eq/gt/and (`c283f3f`) |
| **`$filter` `ne`** | xson `$ne` selectors (`e37b8f1`) |
| **Index-backed `startswith`** | Prefix range via `$gte`/`$lt` (`282c7ab`) |
| **Nested paths** | e.g. `Customer/Country eq 'USA'` |
| **Index-only `$count`** | Fast path for simple indexed selectors (`ce75437`) |

### Engine, indexes & tests

- Multivalue secondary indexes; `std::flat_map` collection map
- Expanded `[yardb]` unit tests: index, engine, OData count, and HTTP gaps (`838d033`)
- **`yarsh` REPL**: full body display, resilient bad JSON, `@Authorization` / conditional headers (`ac824c5`)

### Documentation

- **yarproxy** reframed as HTTP fan-out proxy, not replication/HA (`d816a62`, `531b539`)
- **yarsh / yarexport** synced across README and deployment (`961fbb4`)

---

## December 2025

### Platform & tooling

- **Namespace split** — `yar::db::*` vs `yar::http::*`
- **JSONL build/test output** — CB + `test_runner` structured events
- **Structured logging** — JSONL default, RFC 5424 message IDs
- **`net` C++23 modules** — header library → modules
- **Tester conventions** — BDD nesting, `test_case` + `section` refactors
- **C++ naming compliance** — removed `get_`/`set_` prefixes on public API

### OData & HTTP API

- **`GET /$metadata`** — OData 4.01 JSON CSDL ([archive/odata-metadata-plan.md](archive/odata-metadata-plan.md))
- **OData metadata levels** — `minimalmetadata` / `fullmetadata` via `Accept`
- **ETag & conditional requests** — `If-Match`, `If-None-Match`, `Last-Modified`
- **Content negotiation** — `406` for unsupported `Accept`
- **REST CRUD, indexing, OData query** — baseline HTTP API as documented in [programs.md](programs.md)

---

## How to update this file

When something ships to `master`:

1. Add a row or bullet under the current month in the right section.
2. Optionally note the git commit hash for traceability.
3. Keep [programs.md](programs.md) and smoke `--help` case lists in sync for user-facing tools.
4. Do **not** duplicate the full list in [README.md](README.md) — link here instead.