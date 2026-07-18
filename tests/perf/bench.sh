#!/usr/bin/env bash
# OData performance bench: seed growing datasets, time simple/medium/complex GETs,
# and report client percentiles plus /metrics histogram deltas per scenario.
#
# Usage:
#   ./tests/perf/bench.sh [--jsonl] [--size=1000,10000] [--iters=50] [--warmup=5]
#
# Not wired into default CI (machine-dependent). Prefer a release build of yardb.

set -euo pipefail

PERF_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "${PERF_DIR}/lib.sh"

SIZES="1000,10000"
ITERS=50
WARMUP=5
SCENARIOS="simple,medium,complex"

usage() {
  cat <<'EOF'
Usage: ./tests/perf/bench.sh [options]

Options:
  --jsonl              Emit JSONL events on stdout (progress on stderr)
  --size=N[,N...]      Dataset sizes to seed (default: 1000,10000)
  --iters=N            Timed iterations per scenario (default: 50)
  --warmup=N           Untimed warmup GETs per scenario (default: 5)
  --scenarios=LIST     Comma list: simple,medium,complex (default: all)
  -h, --help           Show this help

Environment:
  BUILD_DIR            Override build dir (else release preferred, then debug)
  YARDB_BIN            Override path to yardb
  COLLECTION           Collection name (default: perf)
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --jsonl) JSONL_MODE=1; shift ;;
    --size=*) SIZES="${1#*=}"; shift ;;
    --iters=*) ITERS="${1#*=}"; shift ;;
    --warmup=*) WARMUP="${1#*=}"; shift ;;
    --scenarios=*) SCENARIOS="${1#*=}"; shift ;;
    -h|--help) usage; exit 0 ;;
    *) log "Unknown option: $1"; usage; exit 2 ;;
  esac
done

scenario_path() {
  # Percent-encode "$" as %24 so bash never expands $top/$filter in double-quoted URLs.
  case "$1" in
    simple)
      echo "/${COLLECTION}?%24top=20"
      ;;
    medium)
      echo "/${COLLECTION}?%24filter=age%20gt%2025%20and%20status%20eq%20'active'&%24top=50&%24orderby=name"
      ;;
    complex)
      echo "/${COLLECTION}?%24filter=(Customer/Country%20eq%20'USA'%20or%20status%20eq%20'vip')%20and%20startswith(name,'A')&%24select=name,age,status&%24orderby=age%20desc&%24top=100"
      ;;
    *)
      fail "unknown scenario: $1"
      ;;
  esac
}

run_scenario() {
  local size=$1
  local scenario=$2
  local path
  path="$(scenario_path "${scenario}")"

  log "Scenario ${scenario} (size=${size}, warmup=${WARMUP}, iters=${ITERS})"

  # Warmup without the scenario label so metrics deltas cover timed iters only.
  if [[ "${WARMUP}" -gt 0 ]]; then
    timed_gets "-" "${path}" "${WARMUP}" >/dev/null
  fi

  local before_body before_count before_sum
  before_body="$(scrape_metrics)"
  read -r before_count before_sum <<<"$(metrics_scenario_sum_count "${scenario}" "${before_body}")"

  local samples_file tmp_root
  tmp_root="${PERF_TMPDIR:-${TMPDIR:-/tmp}}"
  mkdir -p "${tmp_root}"
  samples_file="$(mktemp "${tmp_root}/yardb_perf_samples.XXXXXX")"
  timed_gets "${scenario}" "${path}" "${ITERS}" >"${samples_file}"

  local after_body after_count after_sum
  after_body="$(scrape_metrics)"
  read -r after_count after_sum <<<"$(metrics_scenario_sum_count "${scenario}" "${after_body}")"

  local delta_count delta_sum mean_ms
  delta_count="$(python3 -c "print(int(${after_count}) - int(${before_count}))")"
  delta_sum="$(python3 -c "print(float(${after_sum}) - float(${before_sum}))")"
  if [[ "${delta_count}" -gt 0 ]]; then
    mean_ms="$(python3 -c "print(round((float(${delta_sum}) / float(${delta_count})) * 1000, 3))")"
  else
    mean_ms="0"
  fi

  local pct_json
  pct_json="$(percentiles_json_file "${samples_file}")"
  rm -f "${samples_file}"

  local p50 p95 max_ms n
  p50="$(python3 -c "import json,sys; print(json.loads(sys.argv[1])['p50_ms'])" "${pct_json}")"
  p95="$(python3 -c "import json,sys; print(json.loads(sys.argv[1])['p95_ms'])" "${pct_json}")"
  max_ms="$(python3 -c "import json,sys; print(json.loads(sys.argv[1])['max_ms'])" "${pct_json}")"
  n="$(python3 -c "import json,sys; print(json.loads(sys.argv[1])['n'])" "${pct_json}")"

  log "  client: n=${n} p50=${p50}ms p95=${p95}ms max=${max_ms}ms"
  log "  metrics: delta_count=${delta_count} mean=${mean_ms}ms (scenario=${scenario})"

  if [[ "${n}" -ne "${ITERS}" ]] || [[ "${delta_count}" -lt "${ITERS}" ]]; then
    fail "scenario ${scenario}: expected ${ITERS} timed samples and >=${ITERS} metrics observations (got n=${n}, delta_count=${delta_count})"
  fi

  jsonl_emit "$(python3 - "${size}" "${scenario}" "${n}" "${p50}" "${p95}" "${max_ms}" "${delta_count}" "${mean_ms}" "${path}" <<'PY'
import json, sys
size, scenario, n, p50, p95, max_ms, delta_count, mean_ms, path = sys.argv[1:]
print(json.dumps({
    "type": "bench_scenario",
    "size": int(size),
    "scenario": scenario,
    "path": path,
    "client": {
        "n": int(n),
        "p50_ms": float(p50),
        "p95_ms": float(p95),
        "max_ms": float(max_ms),
    },
    "metrics": {
        "delta_count": int(delta_count),
        "mean_ms": float(mean_ms),
    },
}))
PY
)"
}

trap stop_yardb EXIT

jsonl_emit "{\"type\":\"bench_start\",\"build_dir\":\"${BUILD_DIR}\",\"yardb_bin\":\"${YARDB_BIN}\",\"sizes\":\"${SIZES}\",\"iters\":${ITERS},\"warmup\":${WARMUP},\"scenarios\":\"${SCENARIOS}\"}"

IFS=',' read -r -a size_list <<<"${SIZES}"
IFS=',' read -r -a scenario_list <<<"${SCENARIOS}"

for size in "${size_list[@]}"; do
  size="$(echo "${size}" | tr -d '[:space:]')"
  [[ -n "${size}" ]] || continue

  start_yardb
  seed_collection "${size}"

  for scenario in "${scenario_list[@]}"; do
    scenario="$(echo "${scenario}" | tr -d '[:space:]')"
    [[ -n "${scenario}" ]] || continue
    run_scenario "${size}" "${scenario}"
  done

  stop_yardb
done

jsonl_emit '{"type":"bench_summary","passed":true}'
log "Bench complete."
trap - EXIT
