# Agent instructions — YarDB

Guidance for AI agents and automation using JSONL test and build output in this repo.

## Golden rule

Use **`--jsonl`**. Parse **stdout only** (one JSON object per line, `schema: "tester-jsonl"`). Treat **stderr** as human/CB wrapper logs — do not parse it for pass/fail.

## Setup

```bash
git submodule update --init --depth 1 deps/cryptic deps/net deps/tester deps/xson
./tools/CB.sh debug build
```

`tools/CB.sh` is a thin wrapper over `deps/tester/tools/CB.sh.core`. Requires **Clang 21+** with libc++ modules (`std.cppm`).

## Canonical commands

```bash
# Translation-unit inventory
./tools/CB.sh debug list --jsonl

# Build with compile telemetry
./tools/CB.sh debug build --jsonl

# Scoped test run (preferred while fixing)
./tools/CB.sh debug test --jsonl --jsonl-output=always --tags='\[yardb\]'

# Target by module/file substring
./tools/CB.sh debug test "yar-httpd" --jsonl --jsonl-output=always
./tools/CB.sh debug test "yar-odata" --jsonl --jsonl-output=always
./tools/CB.sh debug test "yar-engine" --jsonl --jsonl-output=always

# Full suite (final verification)
./tools/CB.sh debug test --jsonl --jsonl-output=always

# Test catalogue (ids, tags, depends_on for scoped runs)
./tools/CB.sh debug test --list --jsonl
```

**Tag syntax:** YarDB tests use `[yardb]`. Escape brackets in shell: `--tags='\[yardb\]'`.

**Flags:**
- `--jsonl` — machine-readable stdout for CB and `test_runner`
- `--jsonl-output=always` — emit `assertion_passed` as well as `assertion_failed` (default: failures only)

**CB flag forwarding:** `--tags=`, `--list`, `--jsonl`, `--output=jsonl`, `--jsonl-output=…`, and `--slowest=…` may appear after `test` without `--`. Use `--` only for uncommon `test_runner` flags. Legacy CI form is also valid: `./tools/CB.sh debug test -- --output=jsonl`.

**Examples excluded:** `deps/tester/examples/` are not built or run (`CB_INCLUDE_EXAMPLES_MODE=never`). Use project tags in `YarDB/*.test.c++`.

**Re-run tests without rebuild** (after `./tools/CB.sh debug build`):

```bash
./build-darwin-debug/bin/test_runner --output=jsonl --jsonl-output=always --tags='\[yardb\]'
# Linux: ./build-linux-debug/bin/test_runner ...
```

## Network tests and sandbox

`yar-httpd.test.c++` starts a real HTTP server. In Cursor sandbox environments, `tools/CB.sh` sets `NET_DISABLE_NETWORK_TESTS=1` automatically (`CB_SANDBOX_DISABLE_NETWORK_TESTS=1`). Override explicitly if you need network tests locally:

```bash
NET_DISABLE_NETWORK_TESTS=0 ./tools/CB.sh debug test "yar-httpd" --jsonl
```

## Triage workflow (test failure)

1. Find the last `summary` or `run_end` on stdout.
2. If `passed` is `false`:
   - Read `first_failure` → open `file` at `line`, use `message`
   - Read `failed_test_ids` for the full failure set
3. For diagnosis, grep stdout for `assertion_failed` (or `assertion_passed` when using `always`):
   - `matcher` — e.g. `require_eq`, `check_contains` (not generic `require` / `check`)
   - `actual`, `expected`, `file`, `line`, `column`
4. Fix the source, then re-run the **same** scoped command.

If `matcher` is `"require"` or `"check"` on a `require_eq` / `check_eq` line, rebuild test objects (`./tools/CB.sh debug build --jsonl`) — template matchers are instantiated in `*.test.c++` TUs. See [deps/tester/AGENTS.md](deps/tester/AGENTS.md) and [deps/tester/docs/tester-improvements.md](deps/tester/docs/tester-improvements.md) §2.4.

## Triage workflow (build failure)

1. Find `command_end` with `"ok":false` — use the `argv` array to rerun without shell parsing.
2. Check `compile_end`: `cache_hit:false` = recompiled; `cache_hit:true` = incremental skip.
3. `./tools/CB.sh debug build --jsonl`, then re-run tests.

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
| `run_start` / `run_end` | Run boundaries; `run_start` has `cwd`, `argv`, `config` (from `TESTER_CONFIG` when CB spawns the child), `env` (curated vars when set, e.g. `NET_DISABLE_NETWORK_TESTS`, `CURSOR_SANDBOX`), `passed`, `duration_ms` on `run_end` |
| `assertion_failed` | Always on failed assertions (`matcher`, `actual`, `expected`, optional `message`) |
| `assertion_passed` | With `--jsonl-output=always` |
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
| `build_start` / `build_end` | Whole build |
| `command_start` / `command_end` | Subprocesses (`cmd` + `argv`) |
| `compile_end` | Per TU (`source_path`, `cache_hit`, `rebuild_reason` when `cache_hit:false`, paths) |
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
1. ./tools/CB.sh debug build --jsonl
2. ./tools/CB.sh debug test --jsonl --jsonl-output=always --tags='\[yardb\]'
   # or narrower: ./tools/CB.sh debug test "ETag" --jsonl --jsonl-output=always
3. Parse last summary → passed?
4. If false: first_failure + assertion_failed → edit → goto 1 or 2
5. Before commit: ./tools/CB.sh release test
```

## Do not

- Infer pass/fail from exit code alone — read `summary.passed` or `run_end.passed`
- Parse stderr as structured JSONL
- Run the full suite on every iteration — scope with `--tags='\[yardb\]'` or a name substring first
- Run release-only verification until debug tests pass

## Tester JSONL reference

Full event documentation lives in the tester submodule:

- [deps/tester/README.md — JSONL sections](deps/tester/README.md#jsonl-assertion-events)
- [deps/tester/AGENTS.md](deps/tester/AGENTS.md) — standalone tester agent guide
- [deps/tester/docs/cb.md](deps/tester/docs/cb.md) — CB design and wrapper pattern
- [deps/tester/docs/tester-improvements.md](deps/tester/docs/tester-improvements.md) — backlog
- YarDB test authoring (BDD nesting, registration): [`.cursor/rules/testing-instructions.mdc`](.cursor/rules/testing-instructions.mdc)
- Development quick reference: [`docs/development.md`](docs/development.md)