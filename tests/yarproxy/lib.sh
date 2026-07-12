# Shared helpers for yarproxy smoke tests.

set -euo pipefail

YARPROXY_LIB_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=../yarsh/lib.sh
source "${YARPROXY_LIB_DIR}/../yarsh/lib.sh"

YARPROXY_BIN="${YARPROXY_BIN:-${YARSH_ROOT_DIR}/${BUILD_DIR}/bin/yarproxy}"

REPLICA_PIDS=()
REPLICA_DBS=()
REPLICA_PORTS=()
REPLICA_URLS=()
YARPROXY_PID=""
YARPROXY_PORT=""
PROXY_URL=""
YARPROXY_LOG=""

wait_for_url() {
  local url=$1
  local pid=$2
  local label=$3
  local attempt

  for attempt in $(seq 1 60); do
    if curl -sf "${url}/" >/dev/null 2>&1; then
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
  "${YARDB_BIN}" --clog --file="${db}" "${port}" >"${db}.log" 2>&1 &
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

  local pid db
  for pid in "${REPLICA_PIDS[@]}"; do
    if kill -0 "${pid}" 2>/dev/null; then
      kill "${pid}" 2>/dev/null || true
      wait "${pid}" 2>/dev/null || true
    fi
  done
  REPLICA_PIDS=()

  for db in "${REPLICA_DBS[@]}"; do
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

assert_round_robin_markers() {
  local marker_a=$1
  local marker_b=$2
  local out_a=$3
  local out_b=$4
  local label=${5:-read_round_robin}
  TESTS_RUN=$((TESTS_RUN + 1))

  if python3 - "${marker_a}" "${marker_b}" "${out_a}" "${out_b}" <<'PY'
import sys

marker_a, marker_b, out_a, out_b = sys.argv[1:5]
needle_a = f'"marker" : "{marker_a}"'
needle_b = f'"marker" : "{marker_b}"'

has_a = lambda text: needle_a in text
has_b = lambda text: needle_b in text

if has_a(out_a) and has_b(out_b):
    raise SystemExit(0)
if has_a(out_b) and has_b(out_a):
    raise SystemExit(0)

print("responses did not alternate replica markers", file=sys.stderr)
print(f"out_a has_a={has_a(out_a)} has_b={has_b(out_a)}", file=sys.stderr)
print(f"out_b has_a={has_a(out_b)} has_b={has_b(out_b)}", file=sys.stderr)
raise SystemExit(1)
PY
  then
    jsonl_emit "{\"type\":\"smoke_assert_passed\",\"matcher\":\"${label}\"}"
    return 0
  fi

  fail "expected alternating GET responses with markers ${marker_a} and ${marker_b}"
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