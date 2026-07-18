# Shared helpers for OData performance benches.

set -euo pipefail

PERF_LIB_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PERF_ROOT_DIR="$(cd "${PERF_LIB_DIR}/../.." && pwd)"

# Prefer release binary when present; fall back to debug.
resolve_build_dir() {
  local os_prefix
  case "$(uname -s)" in
    Darwin) os_prefix="build-darwin" ;;
    Linux) os_prefix="build-linux" ;;
    *) os_prefix="build" ;;
  esac
  if [[ -x "${PERF_ROOT_DIR}/${os_prefix}-release/bin/yardb" ]]; then
    echo "${os_prefix}-release"
  elif [[ -x "${PERF_ROOT_DIR}/${os_prefix}-debug/bin/yardb" ]]; then
    echo "${os_prefix}-debug"
  else
    echo "${os_prefix}-debug"
  fi
}

BUILD_DIR="${BUILD_DIR:-$(resolve_build_dir)}"
YARDB_BIN="${YARDB_BIN:-${PERF_ROOT_DIR}/${BUILD_DIR}/bin/yardb}"

JSONL_MODE=0
YARDB_PID=""
YARDB_DB=""
YARDB_PORT=""
YARDB_URL=""
COLLECTION="${COLLECTION:-perf}"

jsonl_emit() {
  [[ "${JSONL_MODE}" -eq 1 ]] || return 0
  printf '%s\n' "$1"
}

log() {
  printf '%s\n' "$*" >&2
}

fail() {
  log "FAIL: $*"
  return 1
}

pick_port() {
  python3 - <<'PY'
import socket
s = socket.socket()
s.bind(("127.0.0.1", 0))
print(s.getsockname()[1])
s.close()
PY
}

stop_yardb() {
  if [[ -n "${YARDB_PID}" ]] && kill -0 "${YARDB_PID}" 2>/dev/null; then
    kill "${YARDB_PID}" 2>/dev/null || true
    wait "${YARDB_PID}" 2>/dev/null || true
  fi
  YARDB_PID=""
  if [[ -n "${YARDB_DB}" ]]; then
    rm -f "${YARDB_DB}" "${YARDB_DB}.pid" "${YARDB_DB}.log"
  fi
  YARDB_DB=""
}

start_yardb() {
  YARDB_PORT="$(pick_port)"
  YARDB_URL="http://127.0.0.1:${YARDB_PORT}"
  # macOS mktemp requires trailing X's (no suffix after the template).
  local tmp_root="${PERF_TMPDIR:-${TMPDIR:-/tmp}}"
  mkdir -p "${tmp_root}"
  YARDB_DB="$(mktemp "${tmp_root}/yardb_perf.XXXXXX")"
  YARDB_DB="${YARDB_DB}.db"
  mv "${YARDB_DB%.db}" "${YARDB_DB}"

  if [[ ! -x "${YARDB_BIN}" ]]; then
    fail "yardb binary not found: ${YARDB_BIN} (build release or debug first)"
  fi

  log "Starting yardb on ${YARDB_URL} (db=${YARDB_DB}, bin=${YARDB_BIN})"
  "${YARDB_BIN}" --clog --file="${YARDB_DB}" "${YARDB_PORT}" >"${YARDB_DB}.log" 2>&1 &
  YARDB_PID=$!

  local attempt
  for attempt in $(seq 1 100); do
    if curl -sf "${YARDB_URL}/ready" >/dev/null 2>&1; then
      log "yardb ready (pid=${YARDB_PID})"
      return 0
    fi
    if ! kill -0 "${YARDB_PID}" 2>/dev/null; then
      fail "yardb exited before becoming ready; log: ${YARDB_DB}.log"
    fi
    sleep 0.1
  done
  fail "yardb did not become ready in time"
}

# Seed N documents into COLLECTION with fields used by bench scenarios.
seed_collection() {
  local size=$1
  local started
  started="$(python3 - <<'PY'
import time
print(time.time())
PY
)"
  log "Seeding ${size} documents into /${COLLECTION}"
  python3 - "${YARDB_URL}" "${COLLECTION}" "${size}" <<'PY'
import json, sys, urllib.request

base, collection, size_s = sys.argv[1], sys.argv[2], sys.argv[3]
size = int(size_s)
statuses = ("active", "inactive", "vip", "pending")
countries = ("USA", "FIN", "DEU", "GBR", "JPN")

for i in range(size):
    name = f"{chr(ord('A') + (i % 26))}user{i}"
    doc = {
        "name": name,
        "age": 18 + (i % 50),
        "status": statuses[i % len(statuses)],
        "Customer": {"Country": countries[i % len(countries)]},
    }
    data = json.dumps(doc).encode()
    req = urllib.request.Request(
        f"{base}/{collection}",
        data=data,
        method="POST",
        headers={"Content-Type": "application/json", "Accept": "application/json"},
    )
    with urllib.request.urlopen(req, timeout=30) as resp:
        if resp.status not in (200, 201):
            raise SystemExit(f"POST failed status={resp.status}")
PY

  # Secondary indexes for medium/complex filter fairness.
  curl -sf -X PUT \
    -H "Content-Type: application/json" \
    -d '{"keys":["name","age","status","Customer/Country"]}' \
    "${YARDB_URL}/_db/${COLLECTION}" >/dev/null

  local elapsed
  elapsed="$(python3 - "${started}" <<'PY'
import sys, time
print(f"{time.time() - float(sys.argv[1]):.3f}")
PY
)"
  jsonl_emit "{\"type\":\"bench_seed\",\"collection\":\"${COLLECTION}\",\"size\":${size},\"elapsed_s\":${elapsed}}"
  log "Seed complete (${elapsed}s)"
}

scrape_metrics() {
  curl -sf "${YARDB_URL}/metrics"
}

# Extract sum/count for a scenario label from Prometheus text (first matching series).
# Prints: count sum  (or 0 0 if missing)
metrics_scenario_sum_count() {
  local scenario=$1
  local body=$2
  python3 - "${scenario}" "${body}" <<'PY'
import re, sys
scenario, body = sys.argv[1], sys.argv[2]
# Match any series that includes scenario="<name>"
count_re = re.compile(
    r'http_request_duration_seconds_count\{[^}]*scenario="' + re.escape(scenario) + r'"[^}]*\}\s+([0-9.]+)'
)
sum_re = re.compile(
    r'http_request_duration_seconds_sum\{[^}]*scenario="' + re.escape(scenario) + r'"[^}]*\}\s+([0-9.eE+-]+)'
)
counts = [float(m.group(1)) for m in count_re.finditer(body)]
sums = [float(m.group(1)) for m in sum_re.finditer(body)]
print(f"{sum(counts):.0f} {sum(sums):.9f}")
PY
}

# Timed GETs with X-Metrics-Scenario. Prints one latency (seconds) per line on stdout.
timed_gets() {
  local scenario=$1
  local path_and_query=$2
  local iters=$3
  local i
  for i in $(seq 1 "${iters}"); do
    if ! curl -sf -o /dev/null -w '%{time_total}\n' \
      -H "X-Metrics-Scenario: ${scenario}" \
      -H "Accept: application/json" \
      "${YARDB_URL}${path_and_query}"
    then
      fail "GET failed for scenario=${scenario} url=${YARDB_URL}${path_and_query}"
    fi
  done
}

# Read latencies (seconds) from a file; print JSON object with p50/p95/max_ms and n.
# (Do not use stdin: callers often combine this with heredocs.)
percentiles_json_file() {
  local samples_file=$1
  python3 - "${samples_file}" <<'PY'
import json, sys
path = sys.argv[1]
with open(path, encoding="utf-8") as f:
    vals = sorted(float(line) for line in f if line.strip())
if not vals:
    print(json.dumps({"n": 0, "p50_ms": 0, "p95_ms": 0, "max_ms": 0}))
    raise SystemExit(0)

def pct(p):
    if len(vals) == 1:
        return vals[0]
    idx = min(len(vals) - 1, max(0, int(round(p * (len(vals) - 1)))))
    return vals[idx]

print(json.dumps({
    "n": len(vals),
    "p50_ms": round(pct(0.50) * 1000, 3),
    "p95_ms": round(pct(0.95) * 1000, 3),
    "max_ms": round(vals[-1] * 1000, 3),
}))
PY
}
