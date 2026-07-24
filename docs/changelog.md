# YarDB Changelog

Shipped features, fixes, and test harness work. **Newest first** within each month.

For planned work see [development.md](development.md#development-roadmap). Historical design evaluations live under [archive/](archive/).

---

## July 2026

### Query

| Item | Summary |
|------|---------|
| **Boolean `not` in `$filter`** | `not status eq 'deleted'`, `not contains(email,'@example')`, De Morgan for `not (A and B)` / `not (A or B)`; precedence `not` > `and` > `or` |
| **`not` vs nested/impossible `$filter`** | `not` no longer rewrites comparisons to `$ne`/`$lte`/… (those also fail on object/array fields and on impossible `$eq`+`$ne` sentinels); evaluates `!match(P)` so `not Customer eq 'x'` and `not (Customer eq 'x' and Customer/Country eq 'y')` return the logically correct documents |
| **Nested/double `not` De Morgan** | `not not status eq '…'` and `not (A and not B)` restore positive comparison branches from `and_negated_selectors` instead of throwing or dropping the inner `not` term |
| **`$filter` OR-branch budget** | AND-over-OR distribution and De Morgan `not` of DNF formulas reject expressions that would exceed 64 OR branches (fail closed) instead of allocating a cartesian explosion that can OOM a single request |
| **`$filter` `not` depth / length budget** | Unary `not` nesting beyond 64 and `$filter` strings longer than 8192 characters are rejected (fail closed) instead of recursing until a single GET can exhaust the request-thread stack |
| **`$apply` groupby/aggregate (v1)** | `GET /orders?$apply=groupby((status),aggregate(amount with sum as Total))` — also `average`/`min`/`max` and whole-set `aggregate(...)`; optional sibling `$filter` after `$apply`; see [odata.md](odata.md) |
| **`$apply` transform pipeline** | Slash-separated `filter(...)` / `compute(...)` / `groupby(...)` / `aggregate(...)` (e.g. `filter(status eq 'active')/groupby((country),aggregate(...))`) |
| **`$compute` query option** | `$compute=Price mul Qty as LineTotal` (`add`/`sub`/`mul`/`div` on top-level numerics); before `$filter`/`$select`/paging; not combinable with `$apply` |
| **`$compute` before `$filter`/`$count`** | `$filter=LineTotal gt 10` (and `$count=true` with that filter) sees aliases from `$compute=… as LineTotal` instead of silently matching only stored fields |
| **`$apply` balanced `filter(...)` / reserved `_id` alias** | Trailing junk after `filter(...)` (e.g. `filter(a eq 1) or (b eq 2)`) is rejected instead of silently matching nothing; compute/aggregate aliases cannot be `_id` |
| **`$apply` then sibling `$filter`** | `$apply=groupby(...)&$filter=Total gt 40` filters the aggregation result (OData Aggregation order) instead of pre-filtering source docs and silently returning `[]` |
| **Same-field AND `$filter`** | `age eq 10 and age gt 5` (and contradictory `status eq 'a' and status eq 'b'`) keep every predicate instead of last-write-wins clobbering earlier constraints |
| **Same-operator range AND** | `age gt 20 and age gt 10` keeps the tighter bound (`$gt:20`) instead of last-write-wins loosening the filter |
| **Same-field `$in` AND** | `status in ('a','b') and status in ('b','c')` intersects membership (`b` only) instead of last-write-wins keeping the second list |
| **Same-field `$ne` AND** | `status ne 'a' and status ne 'b'` unions exclusions (`$nin`) instead of last-write-wins keeping only the second inequality |
| **Multi-op `$count` vs match** | Indexed `$count=true` for AND-merged multi-op field maps (`$eq`+range, `$gt`+`$gte`) falls back to scan+match so count matches GET results |
| **`$orderby` field sort** | Results sort by the named field (`asc`/`desc`); was previously only reversing `_id` walk order via `$desc` |
| **Object field vs scalar `$filter`** | Nested object/array fields no longer match scalar `$eq`/`$ne`/`$gt`/`$in`/`$nin` (and bare `eq` no longer throws); use `Customer/Country` paths for nested documents |
| **Numeric `$filter` vs JSON doubles** | Integer OData literals compare numerically with JSON `number_type` values (`score eq 100` matches `100.0`; `score lt 9` no longer includes `100.0` via variant index order); secondary indexes and `$orderby` use the same ordering |
| **Boolean/null `$filter` literals** | `active eq true` / `eq null` parse as JSON bool/null instead of strings `"true"` / `"null"` |
| **Parent vs nested path AND** | `Customer eq 'Acme' and Customer/Country eq 'USA'` is contradictory (matches nothing) instead of last-write-wins over-including nested documents |

### Storage & tooling

| Item | Summary |
|------|---------|
| **MCP stdio + SSE bridges** | `tools/yardb_mcp.py` (stdio) and `tools/yardb_mcp_sse.py` (Starlette `/sse` + `/messages/`); Cursor config in `.cursor/mcp.json`; smoke via `./tests/mcp/smoke.sh` (`tools_list`, `probes`, `crud`, `filter`, `indexes`, optional `sse`) |
| **MCP SSE bind/Origin harden** | SSE enables MCP DNS-rebinding Host/Origin allowlists and refuses `0.0.0.0`/`::` binds so an unauthenticated PAT-injecting proxy cannot be published |
| **tester CB MCP** | Bump `deps/tester` for `tools/cb_mcp.py`; YarDB `.cursor/mcp.json` registers `tester-cb` with `CB_SH` → `tools/CB.sh` and default tags `[yardb]` |
| **`index()` populates secondaries before unlock** | Adding a secondary key no longer publishes an empty map that makes `$filter`/read on that field return false-empty results until a separate `reindex()`; existing live rows are indexed under the same exclusive lock |
| **`replace` rejects multi-match selectors** | `engine::replace` returns `conflict` when the selector matches more than one live row instead of tombstoning every match while appending a single successor |
| **Update/replace commit when truncate fails after restore** | If successors are durable, status restore succeeds, but truncating appends fails, update/replace publish the staged index (and re-tombstone priors) instead of reporting failure while reopen would apply the new version |
| **Create/update/replace commit when append truncate fails** | If a durable successor is appended but truncating it on rollback fails (before status markers), create/update/replace publish the staged index (update/replace also tombstone priors) instead of reporting failure while reopen would apply the write |
| **Torn append + truncate failure does not publish** | If the append past `original_size` is incomplete (torn) and truncating it fails, create/update/replace return `rollback_failure` without publishing staged or tombstoning priors — avoids silent data loss when reopen drops the incomplete tail |
| **Multi-update mid-batch torn append aborts** | `update` checks failbit after each append and does not `clear()` away a torn ENOSPC record before writing further successors — prevents tombstoning every prior while reopen truncates at the tear and drops later successors |
| **Multi-status tombstone failbit not masked** | `destroy` / `update` / `replace` status-marker loops no longer `clear()` away a mid-loop write failure before the next seek/write — prevents publishing a successful multi-delete (or update) while some priors remain `created` and resurrect on reopen |
| **Incomplete multi-append neutralize on truncate failure** | When truncate fails after a torn multi-append, complete successors past `original_size` are delete-marked so reopen keeps all priors instead of silently applying a partial update after `rollback_failure` |
| **Destroy re-asserts tombstones after failed status restore** | After a failed/partial status restore, destroy rewrites `deleted` markers before publishing the staged index so partially revived rows cannot resurrect on reopen |
| **PUT honors `If-None-Match`** | Conditional PUT returns `412` when `If-None-Match: *` or a matching ETag would be violated, so create-only clients cannot silently overwrite |
| **`yarexport --live` uses engine** | Live export opens through `engine` so dual-live crash windows export one current document per `_id` (stale pre-images are not resurrected into compaction JSONL) |
| **Dual-live crash recovery** | If a crash leaves two `created` rows for one `_id` (successor appended, prior not yet tombstoned), reopen/reindex supersede the earlier row — drop stale secondary hits and heal the prior status to `updated` |
| **Update/replace commit when status restore fails** | If successors are durable but rollback cannot restore prior rows to `created`, update/replace publish the staged index (durable write won) instead of returning failure while live reads keep serving tombstoned pre-images |
| **Destroy commits when status restore fails** | If delete markers are durable but rollback cannot restore `created`, destroy publishes the staged index (durable delete won) instead of returning failure while reopen drops the documents |
| **`replace` rejects colliding `_id`** | Replacement bodies cannot reuse another live primary key; returns `conflict` like create/update instead of silently orphaning the victim from `_id` lookups |
| **Rollback no longer truncates after failed status restore** | If update/replace/destroy cannot restore prior row statuses during rollback, appends are kept so reopen still finds a live version instead of silently dropping tombstoned documents |
| **UTF-8 document strings in FSON** | json4cpp FAST string codec escapes high-bit bytes so `café` (and similar) no longer corrupt storage or brick reopen; ordinary ASCII stays bit-identical on the wire |
| **Secondary indexes skip object/array values** | `index::insert`/`erase` ignore non-primitive field values so create/reindex/restart cannot throw `bad_variant_access` and brick the database |
| **PUT no longer persists OData annotations** | Response-only `@odata.*` fields are added after `replace`/`create`, matching POST/PATCH — they are not written into stored documents |
| **`replace` preserves history** | Reuses prior metadata so `previous` chains, and marks old live records as `updated` (not `deleted`) |
| **`yarexport` claims history-export lock** | History mode creates exclusive `--file.pid` for the scan (not only refuses an existing lock), so yardb cannot open/append mid-export; `--live` still relies on engine lock ownership |
| **Offline compaction** | `yarexport --live` exports current documents only; new `yarimport` rebuilds a FSON file (preserves `_id` and `_db` indexes). Workflow: stop yardb → live export → import → swap |
| **Safe `yarimport --force`** | Validate JSONL and build a staging sidecar before replacing `--file`; failed `--force` imports no longer delete the existing database |
| **`yarexport` stdout failures** | Exit non-zero when stdout write/flush fails (full disk, broken pipe) so truncated JSONL is not treated as a successful export |
| **Reject duplicate create `_id`** | `engine.create` returns `conflict` when a client-/import-supplied `_id` already exists (HTTP `409`); prevents silent primary-index clobber and secondary-index orphans |
| **Immutable `_id` on update** | `engine.update` rejects `_id` changes (`conflict` / HTTP `409`) |
| **PUT/PATCH zero-match → 404** | After a lost race with concurrent delete, write handlers no longer return `200` with an unstored body |
| **Safer `yarimport` install** | Unique per-pid staging name; refuse when `file.pid` exists; non-`--force` install via hard link (no silent clobber); do not delete `.pid` after swap |

### Security

| Item | Summary |
|------|---------|
| **Admin PAT for `/_*`** | `--admin-pat` / `--admin-pat-file` protect maintenance routes (`/_reindex`, `/_db/...`); data PATs rejected there when admin PATs are configured; without admin PATs, `/_*` still accepts the data PAT |

### Observability

| Item | Summary |
|------|---------|
| **Prometheus `GET /metrics`** | HTTP metrics via `net::http::middleware::metrics_middleware` — `http_requests_total` and `http_request_duration_seconds` labeled by `method`, `status`, `path` (query stripped; digit-only segments → `{id}`), and `scenario` (`X-Metrics-Scenario` or `-`); public with PAT; scrapes not counted; unique series hard-capped (overflow collapses to an all-`_other` label bucket so scenario spam cannot OOM) |
| **OData perf bench** | Manual `tests/perf/bench.sh` seeds growing datasets, times simple/medium/complex OData GETs (client percentiles + `/metrics` scenario deltas); see `tests/perf/README.md` |

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
| **`$expand`** | v1 relationship convention: `{singular}_id` → plural collection `_id`; nests related doc or `null` |
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