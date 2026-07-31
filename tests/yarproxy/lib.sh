# Shared helpers for yarproxy smoke tests.

set -euo pipefail

YARPROXY_LIB_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=../yarsh/lib.sh
source "${YARPROXY_LIB_DIR}/../yarsh/lib.sh"

YARPROXY_BIN="${YARPROXY_BIN:-${YARSH_ROOT_DIR}/${BUILD_DIR}/bin/yarproxy}"
REPLICA_PAT="${REPLICA_PAT:-}"

REPLICA_PIDS=()
REPLICA_DBS=()
REPLICA_PORTS=()
REPLICA_URLS=()
YARPROXY_PID=""
YARPROXY_PORT=""
PROXY_URL=""
YARPROXY_LOG=""
MOCK_BACKEND_PID=""
MOCK_BACKEND_LOG=""

wait_for_url() {
  local url=$1
  local pid=$2
  local label=$3
  local attempt
  local curl_args=(-sf)

  if [[ -n "${REPLICA_PAT}" ]]; then
    curl_args+=(-H "Authorization: Bearer ${REPLICA_PAT}")
  fi

  for attempt in $(seq 1 60); do
    if curl "${curl_args[@]}" "${url}/" >/dev/null 2>&1; then
      log "${label} ready (pid=${pid})"
      return 0
    fi
    if ! kill -0 "${pid}" 2>/dev/null; then
      log "${label} exited before becoming ready"
      return 1
    fi
    sleep 0.1
  done

  log "${label} did not become ready in time"
  return 1
}

start_replica() {
  local port db url pid
  port="$(pick_port)"
  db="$(mktemp "${TMPDIR:-/tmp}/yarproxy_smoke.XXXXXX").db"
  url="http://127.0.0.1:${port}"

  log "Starting replica on ${url} (db=${db})"
  local yardb_args=(--clog --file="${db}")
  if [[ -n "${REPLICA_PAT}" ]]; then
    yardb_args+=(--pat="${REPLICA_PAT}")
  fi
  "${YARDB_BIN}" "${yardb_args[@]}" "${port}" >"${db}.log" 2>&1 &
  pid=$!
  wait_for_url "${url}" "${pid}" "replica"

  REPLICA_PIDS+=("${pid}")
  REPLICA_DBS+=("${db}")
  REPLICA_PORTS+=("${port}")
  REPLICA_URLS+=("${url}")
}

start_yarproxy() {
  local args=() url
  for url in "${REPLICA_URLS[@]}"; do
    args+=(--replica="${url}")
  done

  YARPROXY_PORT="$(pick_port)"
  PROXY_URL="http://127.0.0.1:${YARPROXY_PORT}"
  YARPROXY_LOG="$(mktemp "${TMPDIR:-/tmp}/yarproxy_proxy.XXXXXX").log"

  log "Starting yarproxy on ${PROXY_URL} (${#REPLICA_URLS[@]} replicas)"
  "${YARPROXY_BIN}" --clog "${args[@]}" "${YARPROXY_PORT}" >"${YARPROXY_LOG}" 2>&1 &
  YARPROXY_PID=$!
  wait_for_url "${PROXY_URL}" "${YARPROXY_PID}" "yarproxy"
}

stop_cluster() {
  if [[ -n "${YARPROXY_PID}" ]] && kill -0 "${YARPROXY_PID}" 2>/dev/null; then
    kill "${YARPROXY_PID}" 2>/dev/null || true
    wait "${YARPROXY_PID}" 2>/dev/null || true
  fi
  YARPROXY_PID=""

  if [[ -n "${MOCK_BACKEND_PID}" ]] && kill -0 "${MOCK_BACKEND_PID}" 2>/dev/null; then
    kill "${MOCK_BACKEND_PID}" 2>/dev/null || true
    wait "${MOCK_BACKEND_PID}" 2>/dev/null || true
  fi
  MOCK_BACKEND_PID=""
  if [[ -n "${MOCK_BACKEND_LOG}" ]]; then
    rm -f "${MOCK_BACKEND_LOG}"
  fi
  MOCK_BACKEND_LOG=""

  local pid db
  for pid in "${REPLICA_PIDS[@]-}"; do
    if kill -0 "${pid}" 2>/dev/null; then
      kill "${pid}" 2>/dev/null || true
      wait "${pid}" 2>/dev/null || true
    fi
  done
  REPLICA_PIDS=()

  for db in "${REPLICA_DBS[@]-}"; do
    rm -f "${db}" "${db}.pid" "${db}.log"
  done
  REPLICA_DBS=()
  REPLICA_PORTS=()
  REPLICA_URLS=()

  if [[ -n "${YARPROXY_LOG}" ]]; then
    rm -f "${YARPROXY_LOG}"
  fi
  YARPROXY_LOG=""
}

run_yarsh_at() {
  local url=$1
  local script=$2
  local status=0
  LAST_OUTPUT="$(printf '%s\n' "${script}" | run_with_timeout 45 "${YARSH_BIN}" "${url}" 2>&1)" || status=$?
  return "${status}"
}

run_yarsh_proxy() {
  run_yarsh_at "${PROXY_URL}" "$1"
}

assert_exit_status() {
  local expected=$1
  local actual=$2
  local label=${3:-exit_status}
  TESTS_RUN=$((TESTS_RUN + 1))
  if [[ "${actual}" -eq "${expected}" ]]; then
    jsonl_emit "{\"type\":\"smoke_assert_passed\",\"matcher\":\"${label}\"}"
    return 0
  fi
  fail "expected exit status ${expected}, got ${actual}"
  return 0
}

assert_all_replica_logs_contain() {
  local needle=$1
  local label=${2:-replica_logs}
  local db missing=0

  TESTS_RUN=$((TESTS_RUN + 1))
  for db in "${REPLICA_DBS[@]}"; do
    if [[ ! -f "${db}.log" ]] || ! grep -qF "${needle}" "${db}.log"; then
      missing=$((missing + 1))
      log "log miss: ${db}.log does not contain ${needle}"
    fi
  done

  if [[ "${missing}" -eq 0 ]]; then
    jsonl_emit "{\"type\":\"smoke_assert_passed\",\"matcher\":\"${label}\"}"
    return 0
  fi

  fail "expected all replica logs to contain: ${needle}"
  return 0
}

assert_round_robin_all_markers() {
  local label=${1:-read_round_robin}
  local markers_json outputs_json
  TESTS_RUN=$((TESTS_RUN + 1))

  markers_json="$(python3 - "${ROUND_ROBIN_MARKERS[@]}" <<'PY'
import json, sys
print(json.dumps(sys.argv[1:]))
PY
)"
  outputs_json="$(python3 - "${ROUND_ROBIN_OUTPUTS[@]}" <<'PY'
import json, sys
print(json.dumps(sys.argv[1:]))
PY
)"

  if python3 - "${markers_json}" "${outputs_json}" <<'PY'
import json, sys

markers = json.loads(sys.argv[1])
outputs = json.loads(sys.argv[2])

if len(outputs) != len(markers):
    print(f"expected {len(markers)} GET responses, got {len(outputs)}", file=sys.stderr)
    raise SystemExit(1)

seen = set()
for i, out in enumerate(outputs):
    found = [m for m in markers if f'"marker" : "{m}"' in out]
    if len(found) != 1:
        print(f"response {i}: expected exactly one marker, found {found}", file=sys.stderr)
        raise SystemExit(1)
    seen.add(found[0])

if seen != set(markers):
    missing = set(markers) - seen
    print(f"round-robin missed markers: {sorted(missing)}", file=sys.stderr)
    raise SystemExit(1)
PY
  then
    jsonl_emit "{\"type\":\"smoke_assert_passed\",\"matcher\":\"${label}\"}"
    return 0
  fi

  fail "expected ${#ROUND_ROBIN_MARKERS[@]} round-robin GET responses covering all replica markers"
  return 0
}

require_bins() {
  if [[ ! -x "${YARDB_BIN}" ]]; then
    log "yardb not found at ${YARDB_BIN} — run ./tools/CB.sh debug build first"
    exit 1
  fi
  if [[ ! -x "${YARSH_BIN}" ]]; then
    log "yarsh not found at ${YARSH_BIN} — run ./tools/CB.sh debug build first"
    exit 1
  fi
  if [[ ! -x "${YARPROXY_BIN}" ]]; then
    log "yarproxy not found at ${YARPROXY_BIN} — run ./tools/CB.sh debug build first"
    exit 1
  fi
  if ! command -v curl >/dev/null 2>&1; then
    log "curl is required for readiness checks"
    exit 1
  fi
}