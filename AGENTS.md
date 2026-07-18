# Agent instructions — YarDB

Guidance for AI agents and automation using JSONL test and build output in this repo.

## Golden rule

Use **`--jsonl=failures`**. Parse **stdout only** (one JSON object per line, `schema: "tester-jsonl"`). Treat **stderr** as human/CB wrapper logs — do not parse it for pass/fail.

## Setup

```bash
git submodule update --init --depth 1 deps/cryptic deps/net deps/tester deps/xson
./tools/CB.sh debug build
```

`tools/CB.sh` is a thin wrapper over `deps/tester/tools/CB.sh.core`. Requires **Clang 23+** with libc++ modules (`std.cppm`). On Linux, the wrapper selects `clang++-23` / `/usr/lib/llvm-23` (tester's core still defaults to 21 for other repos).

## Coding standards

[`CONTRIBUTING.md`](CONTRIBUTING.md#c-coding-standards) is the canonical coding standard for humans and agents. Follow its C++23 modules-only policy, `snake_case` naming, Allman production braces, accessor rules, standard-library-first guidance, RAII, and `std::expected` error model. This file only adds automation and JSONL test guidance; do not duplicate the coding standard here.

## Canonical commands

```bash
# Translation-unit inventory
./tools/CB.sh debug list --jsonl=failures

# Build with compile telemetry
./tools/CB.sh debug build --jsonl=failures

# Scoped test run (preferred while fixing)
./tools/CB.sh debug test --jsonl=failures --tags='\[yardb\]'

# Target by registered test-name substring
./tools/CB.sh debug test "REST API status" --jsonl=failures
./tools/CB.sh debug test "parse_filter" --jsonl=failures
./tools/CB.sh debug test "database engine" --jsonl=failures
./tools/CB.sh debug test "index count and view" --jsonl=failures

# Full YarDB suite (final verification — always tag-filter; unfiltered runs pull in deps/tester probes)
./tools/CB.sh debug test --jsonl=failures --tags='\[yardb\]'

# Test catalogue (ids, tags, depends_on for scoped runs)
./tools/CB.sh debug test --list --jsonl=failures
```

**Tag syntax:** YarDB tests use `[yardb]`. Escape brackets in shell: `--tags='\[yardb\]'`. Substring/regex filters also work: `--tags='ETag'`.

**Hidden tags:** bracket tags starting with `.` (Catch2-style, e.g. `[.jsonl-probe]`) are **skipped on unfiltered runs**. In YarDB, unfiltered runs also pull in `deps/tester` `[jsonl-probe]` intentional failures — always scope with `--tags='\[yardb\]'`.

**Scoped runs:** Prefer `--tags='\[yardb\]'` while fixing. Examples are excluded, but an unfiltered run includes tester probe fixtures; do not expect `summary.passed: true` without a project tag.

**Unified JSONL modes:**
- `--jsonl` / `--jsonl=failures` — aggregates plus actionable failures
- `--jsonl=summary` — lifecycle and final aggregates only
- `--jsonl=trace` — complete telemetry, including passing assertions

**CB flag forwarding:** `--tags=`, `--list`, `--jsonl[=summary|failures|trace]`, `--jsonl-output-max-bytes=…`, and `--slowest=…` may appear after `test` without `--`.

**Examples excluded:** `deps/tester/examples/` are not built or run (`CB_INCLUDE_EXAMPLES_MODE=never`). Use project tags in `YarDB/*.test.c++`.

**Re-run tests without rebuild** (after `./tools/CB.sh debug build`):

```bash
./build-darwin-debug/bin/test_runner --jsonl=failures --tags='\[yardb\]'
# Linux: ./build-linux-debug/bin/test_runner ...
```

## Network tests and sandbox

`yar-httpd.test.c++` starts a real HTTP server on port `21120` and currently does not honor `NET_DISABLE_NETWORK_TESTS`. Run HTTP integration tests only where loopback bind/connect is available. The environment variable disables supported dependency-level network tests, not YarDB's HTTP fixture.

## Triage workflow (test failure)

1. Find the last `summary` or `run_end` event on stdout.

2. Check the result:
   - If `passed` (or `run_end.passed`) is `true` → this scoped run succeeded. You're done.
   - If `false`:
     - Read `first_failure` — `file`, `line`, `message`, and usually the failing `matcher` with `actual` / `expected`. Open the source at that location.
     - Read `failed_test_ids` for the full failure set.

3. For detailed diagnosis, inspect `assertion_failed`:
   - `matcher` — e.g. `require_eq`, `check_contains` (not generic `require` / `check`)
   - `actual`, `expected`, `file`, `line`, `column`

4. Fix the source, then **re-run the exact same scoped command**.

If `matcher` is `"require"` or `"check"` on a `require_eq` / `check_eq` line, stale test objects are likely — rebuild test TUs (`./tools/CB.sh debug build --jsonl=failures`), not only `tester_assertions.pcm`. Then re-run the test command.

## Triage workflow (build failure)

1. Find `command_end` with `"ok": false` — use the `argv` array to rerun without shell parsing.
2. Check `compile_end` events: `cache_hit: false` means that translation unit recompiled; `cache_hit: true` means incremental skip. When `rebuild_reason` is `profile_change`, read the single `profile_changed` event for `profile_diff` (not repeated on each `compile_end`). Prefer `build_end.rebuild_summary` for a per-kind rollup.
3. Rebuild: `./tools/CB.sh debug build --jsonl=failures`, then re-run tests.

## Event reference (stdout)

### Correlation

Filter `run_id=<cb>` or `parent_run_id=<cb>` to correlate `list` → `build` → `test` in one `CB … --jsonl` invocation. Full table: [deps/tester/AGENTS.md — Correlation](deps/tester/AGENTS.md#correlation).

| Field | On | Meaning |
|-------|-----|---------|
| `run_id` | Every event | Session id for the emitting process (32-char hex) |
| `parent_run_id` | `test_runner` events only | CB’s `run_id`, passed via `TESTER_PARENT_RUN_ID` when CB spawns the child |
| `config` on `run_start` | `test_runner` when spawned by CB | CB build config (`debug` / `release`), via `TESTER_CONFIG` |
| `pid` | Every event | OS process id (`test_runner` differs from CB) |
| `ts_unix_ms` | Every event | Unix timestamp (ms) |

### Test catalogue (`test --list --jsonl`)

| Event | Use |
|-------|-----|
| `test_list_start` | Catalogue start (`tags_filter`) |
| `registered_test` | Per test: `id`, `name`, `file`, `line`, `column`, `tags[]`, `depends_on[]`, `priority` |
| `test_list_summary` | `registered_total`, `matched_total`, `tags_filter` |

### Test phase

| Event | Use |
|-------|-----|
| `run_start` / `run_end` | Run boundaries; `run_start` has `cwd`, `argv`, `config` (from `TESTER_CONFIG` when CB spawns the child), `env` (curated test-relevant vars when set, e.g. `NET_DISABLE_NETWORK_TESTS`, `CURSOR_SANDBOX`), `passed`, `duration_ms` on `run_end` |
| `assertion_failed` | Always on failed assertions (`matcher`, `actual`, `expected`, optional `message`) |
| `assertion_passed` | Trace mode |
| `test` | Per-test rollup (`success`, `output`, assertion counts) |
| `summary` | `tests_ok`/`tests_total`, `failed_test_ids`, `first_failure` |
| `exception` | Uncaught exceptions (`exception_type`, `message`, `file`, `line`) |
| `eof` | End of JSONL stream |

### Build phase (CB)

| Event | Use |
|-------|-----|
| `list_start` | TU inventory start (`config`, `include_tests`, `include_examples`, `source_dir`) |
| `unit` | Per translation unit (`path`, `module`, `kind`, `imports[]`, `level`, `has_main`, `is_test`, `is_modular`) |
| `list_summary` | Inventory totals (`units_total`, `main_count`, `test_count`, `max_level`) |
| `build_start` / `build_end` | Whole build; `rebuild_summary` lists compile rebuilds by kind + `top_modules` |
| `command_start` / `command_end` | Subprocesses (`cmd` + `argv`) |
| `profile_changed` | Once per build when object-cache profile mismatches (`reason`, `profile_diff`) |
| `cache_status` | `cache status` subcommand (`object_cache_path`, `profile_match`, `legacy_header`, entry counts, `current_profile`) |
| `cache_invalidate_end` | `cache invalidate` subcommand (`object_cache_removed`, `executable_cache_removed`, `compiler_stamp_removed`) |
| `compile_start` | Per TU before compile or cache skip; on rebuild: `rebuild_reason`, structured `rebuild`, `message` |
| `compile_end` | Per TU (`source_path`, `cache_hit`, short `rebuild_reason` + `rebuild` when `cache_hit:false`, `duration_ms`, paths) |
| `link_end` | Per executable (`executable_path`, `cache_hit`, `ok`, `duration_ms`; relinks include `rebuild_reason` / `rebuild`) |
| `cb_error` | CB fatal/diagnostic |

**`unit.is_test`:** `true` for `*.test.c++` / `*.test.c++m`, or when a path segment is exactly `test/` or `tests/`. `false` for sources under a `tester/` framework tree (library modules, not project tests) — including nested paths like `deps/xson/deps/tester/`. Does not match the substring `test` inside names such as `tester` or `test_exception_bug`.

## YarDB test layout

| File | Style | Coverage |
|------|-------|----------|
| `YarDB/yar-httpd.test.c++` | `tester::basic::test_case` + `section` | REST API, OData HTTP, ETag, CORS, rate limiting |
| `YarDB/yar-engine.test.c++` | `tester::basic::test_case` + `section` | CRUD, indexing, FSON storage |
| `YarDB/yar-odata.test.c++` | `tester::bdd::scenario` | OData parsing, metadata, filters |

Tests are co-located with source (P1204R0). Tag all tests with `[yardb]`.

HTTP integration tests use a `fixture` that starts `rest_api_server` on port `21120`. Capture the fixture with `std::make_shared` — sections run after the `test_case` lambda returns.

## Example agent loop

```text
1. ./tools/CB.sh debug build --jsonl=failures
2. ./tools/CB.sh debug test --jsonl=failures --tags='\[yardb\]'
   # or narrower: ./tools/CB.sh debug test "ETag" --jsonl=failures
3. Parse last summary or run_end → check passed
4. If false: follow triage workflow (first_failure + assertion_failed) → edit → re-run the same scoped command
5. Before commit: ./tools/CB.sh release test --tags='\[yardb\]'
```

## Do not

- Infer pass/fail from exit code alone — read `summary.passed` or `run_end.passed`
- Parse stderr as structured JSONL
- Use an unfiltered full-suite run as the default fix loop — scope with `--tags='\[yardb\]'` (unfiltered runs include `deps/tester` `[jsonl-probe]` intentional failures)
- Run `[.tag]` probe fixtures unless explicitly selected (they are hidden by default)
- Run release-only verification until debug tests pass
- **Modify production code to simulate errors.** Do not add `fail_*` friends, private inject flags, `consume_*` / `inject_*` hooks, synthetic `badbit` / truncate / torn-append branches, `#ifndef NDEBUG` test seams, or any other instrumentation inside `yar-engine`, `yar-httpd`, or other product modules solely so tests can force failures. Write paths must only handle real I/O and API errors. Prefer ordinary behavioral tests against the public API; exercise recovery via real fixtures (corrupt/truncated files on disk, lock files, invalid inputs) rather than by wiring test knobs into production code. Never reintroduce removed fault-injection machinery unless a human **explicitly** requests it and accepts production-path pollution.

## Tester JSONL reference

Full event documentation lives in the tester submodule:

- [deps/tester/README.md — JSONL sections](deps/tester/README.md#jsonl-assertion-events)
- [deps/tester/AGENTS.md](deps/tester/AGENTS.md) — standalone tester agent guide
- [deps/tester/docs/cb.md](deps/tester/docs/cb.md) — CB design and wrapper pattern
- [deps/tester/docs/tester-improvements.md](deps/tester/docs/tester-improvements.md) — backlog
- YarDB test authoring (BDD nesting, registration): [`.cursor/rules/testing-instructions.mdc`](.cursor/rules/testing-instructions.mdc)
- Development quick reference: [`docs/development.md`](docs/development.md)