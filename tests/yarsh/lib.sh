#!/usr/bin/env bash
# Shared helpers for piped yarsh smoke tests.

set -euo pipefail

YARSH_LIB_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
YARSH_ROOT_DIR="$(cd "${YARSH_LIB_DIR}/../.." && pwd)"

BUILD_DIR="${BUILD_DIR:-}"
if [[ -z "${BUILD_DIR}" ]]; then
  case "$(uname -s)" in
    Darwin) BUILD_DIR="build-darwin-debug" ;;
    Linux) BUILD_DIR="build-linux-debug" ;;
    *) BUILD_DIR="build-debug" ;;
  esac
fi

YARDB_BIN="${YARDB_BIN:-${YARSH_ROOT_DIR}/${BUILD_DIR}/bin/yardb}"
YARSH_BIN="${YARSH_BIN:-${YARSH_ROOT_DIR}/${BUILD_DIR}/bin/yarsh}"

JSONL_MODE=0
LAST_OUTPUT=""
LAST_RESPONSE_BODY=""
FAILURES=0
TESTS_RUN=0
YARDB_PID=""
YARDB_DB=""
YARDB_PORT=""
YARDB_URL=""
RUN_ID=""

jsonl_emit() {
  [[ "${JSONL_MODE}" -eq 1 ]] || return 0
  printf '%s\n' "$1"
}

log() {
  printf '%s\n' "$*" >&2
}

fail() {
  FAILURES=$((FAILURES + 1))
  log "FAIL: $*"
  if [[ -n "${LAST_OUTPUT}" ]]; then
    log "--- yarsh output (last run) ---"
    printf '%s\n' "${LAST_OUTPUT}" >&2
    log "--- end output ---"
  fi
  jsonl_emit "{\"type\":\"smoke_assert_failed\",\"message\":\"assertion_failed\"}"
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

run_with_timeout() {
  local seconds=$1
  shift
  if command -v timeout >/dev/null 2>&1; then
    timeout "${seconds}" "$@"
  else
    "$@"
  fi
}

start_yardb() {
  YARDB_PORT="$(pick_port)"
  YARDB_URL="http://127.0.0.1:${YARDB_PORT}"
  YARDB_DB="$(mktemp "${TMPDIR:-/tmp}/yarsh_smoke.XXXXXX.db")"
  RUN_ID="smoke$(date +%s)${RANDOM}"

  log "Starting yardb on ${YARDB_URL} (db=${YARDB_DB})"
  "${YARDB_BIN}" --clog --file="${YARDB_DB}" "${YARDB_PORT}" >"${YARDB_DB}.log" 2>&1 &
  YARDB_PID=$!

  local attempt
  for attempt in $(seq 1 60); do
    if curl -sf "${YARDB_URL}/" >/dev/null 2>&1; then
      log "yardb ready (pid=${YARDB_PID})"
      return 0
    fi
    if ! kill -0 "${YARDB_PID}" 2>/dev/null; then
      log "yardb exited before becoming ready"
      return 1
    fi
    sleep 0.1
  done

  log "yardb did not become ready in time"
  return 1
}

stop_yardb() {
  if [[ -n "${YARDB_PID}" ]] && kill -0 "${YARDB_PID}" 2>/dev/null; then
    kill "${YARDB_PID}" 2>/dev/null || true
    wait "${YARDB_PID}" 2>/dev/null || true
  fi
  if [[ -n "${YARDB_DB}" ]]; then
    rm -f "${YARDB_DB}" "${YARDB_DB}.pid" "${YARDB_DB}.log"
  fi
}

run_yarsh() {
  local script=$1
  local status=0
  LAST_OUTPUT="$(printf '%s\n' "${script}" | run_with_timeout 45 "${YARSH_BIN}" "${YARDB_URL}" 2>&1)" || status=$?
  return "${status}"
}

assert_contains() {
  local needle=$1
  local label=${2:-contains}
  TESTS_RUN=$((TESTS_RUN + 1))
  if [[ "${LAST_OUTPUT}" == *"${needle}"* ]]; then
    jsonl_emit "{\"type\":\"smoke_assert_passed\",\"matcher\":\"${label}\"}"
    return 0
  fi
  fail "expected output to contain: ${needle}"
  return 0
}

assert_not_contains() {
  local needle=$1
  local label=${2:-not_contains}
  TESTS_RUN=$((TESTS_RUN + 1))
  if [[ "${LAST_OUTPUT}" != *"${needle}"* ]]; then
    jsonl_emit "{\"type\":\"smoke_assert_passed\",\"matcher\":\"${label}\"}"
    return 0
  fi
  fail "expected output NOT to contain: ${needle}"
  return 0
}

extract_last_response_body() {
  local status=0
  LAST_RESPONSE_BODY="$(python3 - "${LAST_OUTPUT}" <<'PY'
import sys
text = sys.argv[1]
marker = "Response Body:"
idx = text.rfind(marker)
if idx < 0:
    raise SystemExit(1)
body = text[idx + len(marker):]
for stop in ("\nEnter restful request:", "\nClosing connection..."):
    pos = body.find(stop)
    if pos >= 0:
        body = body[:pos]
print(body.strip())
PY
)" || status=$?
  return "${status}"
}

assert_last_json_array_length() {
  local op=$1
  local expected=$2
  local label=${3:-array_length}
  TESTS_RUN=$((TESTS_RUN + 1))
  if ! extract_last_response_body; then
    fail "no response body for ${label}"
    return 0
  fi
  if python3 - "${op}" "${expected}" "${LAST_RESPONSE_BODY}" <<'PY'
import json, sys
op, expected, body = sys.argv[1], int(sys.argv[2]), sys.argv[3]
data = json.loads(body)
if not isinstance(data, list):
    print(f"expected JSON array, got {type(data).__name__}", file=sys.stderr)
    raise SystemExit(1)
n = len(data)
ok = (
    (op == "eq" and n == expected) or
    (op == "le" and n <= expected) or
    (op == "ge" and n >= expected)
)
if not ok:
    print(f"array length {n} failed {op} {expected}", file=sys.stderr)
    raise SystemExit(1)
PY
  then
    jsonl_emit "{\"type\":\"smoke_assert_passed\",\"matcher\":\"${label}\"}"
    return 0
  fi
  fail "expected last response array length ${op} ${expected}"
  return 0
}

assert_last_json_array_first_field() {
  local field=$1
  local expected=$2
  local label=${3:-array_first_field}
  TESTS_RUN=$((TESTS_RUN + 1))
  if ! extract_last_response_body; then
    fail "no response body for ${label}"
    return 0
  fi
  if python3 - "${field}" "${expected}" "${LAST_RESPONSE_BODY}" <<'PY'
import json, sys
field, expected, body = sys.argv[1], sys.argv[2], sys.argv[3]
data = json.loads(body)
if not data:
    print("empty array", file=sys.stderr)
    raise SystemExit(1)
actual = data[0].get(field)
if str(actual) != str(expected):
    print(f"first[{field}]={actual!r}, expected {expected!r}", file=sys.stderr)
    raise SystemExit(1)
PY
  then
    jsonl_emit "{\"type\":\"smoke_assert_passed\",\"matcher\":\"${label}\"}"
    return 0
  fi
  fail "expected first array item ${field}=${expected}"
  return 0
}

assert_last_json_array_excludes_field_value() {
  local field=$1
  local value=$2
  local label=${3:-excludes_field_value}
  TESTS_RUN=$((TESTS_RUN + 1))
  if ! extract_last_response_body; then
    fail "no response body for ${label}"
    return 0
  fi
  if python3 - "${field}" "${value}" "${LAST_RESPONSE_BODY}" <<'PY'
import json, sys
field, value, body = sys.argv[1], sys.argv[2], sys.argv[3]
data = json.loads(body)
for item in data:
    if str(item.get(field)) == str(value):
        print(f"found excluded {field}={value!r} in array", file=sys.stderr)
        raise SystemExit(1)
PY
  then
    jsonl_emit "{\"type\":\"smoke_assert_passed\",\"matcher\":\"${label}\"}"
    return 0
  fi
  fail "expected array to exclude ${field}=${value}"
  return 0
}

assert_last_json_projection() {
  local include_csv=$1
  local exclude_csv=$2
  local label=${3:-projection}
  TESTS_RUN=$((TESTS_RUN + 1))
  if ! extract_last_response_body; then
    fail "no response body for ${label}"
    return 0
  fi
  if python3 - "${include_csv}" "${exclude_csv}" "${LAST_RESPONSE_BODY}" <<'PY'
import json, sys
includes = [x for x in sys.argv[1].split(",") if x]
excludes = [x for x in sys.argv[2].split(",") if x]
data = json.loads(sys.argv[3])
if not data:
    print("empty array", file=sys.stderr)
    raise SystemExit(1)
for i, item in enumerate(data):
    if "_id" not in item:
        print(f"item {i} missing _id", file=sys.stderr)
        raise SystemExit(1)
    for key in includes:
        if key not in item:
            print(f"item {i} missing included field {key}", file=sys.stderr)
            raise SystemExit(1)
    for key in excludes:
        if key in item:
            print(f"item {i} still has excluded field {key}", file=sys.stderr)
            raise SystemExit(1)
PY
  then
    jsonl_emit "{\"type\":\"smoke_assert_passed\",\"matcher\":\"${label}\"}"
    return 0
  fi
  fail "projection mismatch (include=${include_csv}, exclude=${exclude_csv})"
  return 0
}

begin_case() {
  local name=$1
  log ""
  log "=== case: ${name} ==="
  jsonl_emit "{\"type\":\"smoke_case_start\",\"name\":\"${name}\"}"
}

end_case() {
  local name=$1
  jsonl_emit "{\"type\":\"smoke_case_end\",\"name\":\"${name}\"}"
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
  if ! command -v curl >/dev/null 2>&1; then
    log "curl is required for readiness checks"
    exit 1
  fi
}