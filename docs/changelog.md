# YarDB Changelog

Shipped features, fixes, and test harness work. **Newest first** within each month.

For planned work see [rest_api_evaluation.md](rest_api_evaluation.md) and [development.md](development.md#development-roadmap).

---

## July 2026

### Auth & security

| Item | Summary |
|------|---------|
| **Bearer PAT authentication** | `yardb --pat` / `--pat-file`; all routes protected via `net` `authentication_middleware` (`5236cab`) |
| **Hashed PAT storage** | Tokens stored as SHA-256 of full `Authorization` header; `--pat-file` accepts `sha256:<hex>` lines (`48a4ac0`) |
| **`GET /health` liveness probe** | Returns `{"status":"ok"}`; public even when PAT is enabled; reserved collection name (`48a4ac0`) |

### Observability

| Item | Summary |
|------|---------|
| **`correlation_id` request tracing** | `X-Correlation-ID` middleware; logged on handler entry (`POST_DOCUMENT`, etc.), `HTTP_RESPONSE`, and error paths in `yar-httpd.c++m` |
| **Transport `request_id`** | `net::http::server` per-connection counter on `HTTP_REQUEST` / net `HTTP_RESPONSE` (complements `correlation_id`) |

### CLI smoke tests (CI)

| Harness | Cases / coverage |
|---------|------------------|
| **`tests/yarsh/smoke.sh`** | `crud`, `put`, `patch`, `count`, `top_skip`, `orderby`, `select`, `filter_eq_gt`, `filter_in`, `filter_ne`, `filter_or`, `filter_startswith`, `head`, `if_none_match`, `bad_json`, `auth_required`, `auth_crud` |
| **`tests/yarexport/smoke.sh`** | `export_empty`, `export_seeded`, `missing_file`, `help` |
| **`tests/yarproxy/smoke.sh`** | `no_replicas`, `help`, `proxy_crud`, `write_fanout`, `read_round_robin`; `--replicas=N` |

Earlier July commits: piped yarsh harness (`031d52f`), yarexport JSONL fix + smokes (`7066ee1`), yarproxy smokes (`7ebe299`, `22b8400`).

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
- Expanded `[yardb]` unit tests: index, engine, OData count, HTTP gaps (`838d033` — 288+ tests at time of commit)
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