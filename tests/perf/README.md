# OData performance bench

Manual harness that seeds a collection, runs simple / medium / complex OData GETs, and reports:

1. **Client latencies** — curl `%{time_total}` percentiles (p50 / p95 / max)
2. **Prometheus deltas** — mean server-side duration from `http_request_duration_seconds` for the scenario label

Not part of default CI (results are machine-dependent). Prefer a **release** `yardb` binary.

## Prerequisites

```bash
./tools/CB.sh release build   # preferred
# or: ./tools/CB.sh debug build
```

## Run

```bash
./tests/perf/bench.sh --jsonl --size=1000,10000 --iters=50 --warmup=5
```

| Flag | Default | Meaning |
|------|---------|---------|
| `--size=` | `1000,10000` | Comma-separated document counts |
| `--iters=` | `50` | Timed GETs per scenario |
| `--warmup=` | `5` | Untimed GETs before measurement (no scenario label) |
| `--scenarios=` | `simple,medium,complex` | Subset of scenarios |
| `--jsonl` | off | JSONL events on stdout (`bench_start`, `bench_seed`, `bench_scenario`, `bench_summary`) |

Override binary: `YARDB_BIN=/path/to/yardb ./tests/perf/bench.sh …`

## Scenarios

All hit collection `perf` (override with `COLLECTION=`). Each timed request sends `X-Metrics-Scenario: <name>`.

| Name | Query shape |
|------|-------------|
| `simple` | `$top=20` |
| `medium` | indexed `$filter` + `$orderby` + `$top` |
| `complex` | nested OR + `startswith` + `$select` / `$orderby` / `$top` |

Secondary indexes (`name`, `age`, `status`, `Customer/Country`) are created after seed via `PUT /_db/perf`.

## Reading `/metrics`

YarDB exposes Prometheus text at `GET /metrics` (public even with PAT). Series include:

- `method`, `status`
- `path` — request path **without** query string (keeps cardinality low)
- `scenario` — value of `X-Metrics-Scenario` when set and sanitized (`[A-Za-z0-9_.:-]{1,64}`); otherwise `-`

Example:

```text
http_request_duration_seconds_count{method="GET",status="200",path="/perf",scenario="simple"} 50
```

The bench scrapes before/after each scenario and reports `metrics.mean_ms` from the sum/count delta for that `scenario` label. Scrapes of `/metrics` itself are not counted.
