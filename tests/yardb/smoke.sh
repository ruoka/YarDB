#!/usr/bin/env bash
# yardb bind-policy smoke tests.

set -euo pipefail

YARDB_SMOKE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
YARDB_ROOT_DIR="$(cd "${YARDB_SMOKE_DIR}/../.." && pwd)"

BUILD_DIR="${BUILD_DIR:-}"
if [[ -z "${BUILD_DIR}" ]]; then
  case "$(uname -s)" in
    Darwin) BUILD_DIR="build-darwin-debug" ;;
    Linux) BUILD_DIR="build-linux-debug" ;;
    *) BUILD_DIR="build-debug" ;;
  esac
fi

YARDB_BIN="${YARDB_BIN:-${YARDB_ROOT_DIR}/${BUILD_DIR}/bin/yardb}"

JSONL_MODE=0
FAILURES=0
TESTS_RUN=0

log() {
  printf '%s\n' "$*" >&2
}

fail() {
  FAILURES=$((FAILURES + 1))
  log "FAIL: $*"
}

jsonl_emit() {
  [[ "${JSONL_MODE}" -eq 1 ]] || return 0
  printf '%s\n' "$1"
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

run_test() {
  local name=$1
  shift
  TESTS_RUN=$((TESTS_RUN + 1))
  log "TEST: ${name}"
  if "$@"; then
    jsonl_emit "{\"type\":\"smoke_test\",\"name\":\"${name}\",\"passed\":true}"
    return 0
  fi
  fail "${name}"
  jsonl_emit "{\"type\":\"smoke_test\",\"name\":\"${name}\",\"passed\":false}"
  return 1
}

test_refuse_public_bind_without_pat() {
  local output
  if output="$("${YARDB_BIN}" --bind=0.0.0.0 29999 2>&1)"; then
    log "expected non-zero exit for public bind without PAT"
    log "${output}"
    return 1
  fi
  [[ "${output}" == *"refusing to bind to 0.0.0.0 without PAT"* ]]
}

test_default_loopback_bind() {
  local port db pid attempt curl_args=(-sf)
  port="$(pick_port)"
  db="$(mktemp "${TMPDIR:-/tmp}/yardb_bind_smoke.XXXXXX.db")"

  "${YARDB_BIN}" --clog --file="${db}" "${port}" >"${db}.log" 2>&1 &
  pid=$!

  for attempt in $(seq 1 60); do
    if curl "${curl_args[@]}" "http://127.0.0.1:${port}/health" >/dev/null 2>&1; then
      kill "${pid}" 2>/dev/null || true
      wait "${pid}" 2>/dev/null || true
      rm -f "${db}" "${db}.pid" "${db}.log"
      return 0
    fi
    if ! kill -0 "${pid}" 2>/dev/null; then
      log "yardb exited before becoming ready"
      cat "${db}.log" >&2 || true
      rm -f "${db}" "${db}.pid" "${db}.log"
      return 1
    fi
    sleep 0.1
  done

  kill "${pid}" 2>/dev/null || true
  wait "${pid}" 2>/dev/null || true
  rm -f "${db}" "${db}.pid" "${db}.log"
  log "yardb did not become ready on 127.0.0.1"
  return 1
}

test_public_bind_with_pat() {
  local port db pid attempt pat="bind-smoke-pat"
  port="$(pick_port)"
  db="$(mktemp "${TMPDIR:-/tmp}/yardb_bind_smoke.XXXXXX.db")"

  "${YARDB_BIN}" --clog --bind=0.0.0.0 --pat="${pat}" --file="${db}" "${port}" >"${db}.log" 2>&1 &
  pid=$!

  for attempt in $(seq 1 60); do
    if curl -sf -H "Authorization: Bearer ${pat}" "http://127.0.0.1:${port}/health" >/dev/null 2>&1; then
      kill "${pid}" 2>/dev/null || true
      wait "${pid}" 2>/dev/null || true
      rm -f "${db}" "${db}.pid" "${db}.log"
      return 0
    fi
    if ! kill -0 "${pid}" 2>/dev/null; then
      log "yardb exited before becoming ready"
      cat "${db}.log" >&2 || true
      rm -f "${db}" "${db}.pid" "${db}.log"
      return 1
    fi
    sleep 0.1
  done

  kill "${pid}" 2>/dev/null || true
  wait "${pid}" 2>/dev/null || true
  rm -f "${db}" "${db}.pid" "${db}.log"
  log "yardb did not become ready with public bind + PAT"
  return 1
}

main() {
  for arg in "$@"; do
    if [[ "${arg}" == "--jsonl" ]]; then
      JSONL_MODE=1
    fi
  done

  if [[ ! -x "${YARDB_BIN}" ]]; then
    log "yardb not found at ${YARDB_BIN} — run ./tools/CB.sh debug build first"
    exit 1
  fi

  jsonl_emit '{"type":"smoke_start","suite":"yardb-bind"}'

  run_test "refuse_public_bind_without_pat" test_refuse_public_bind_without_pat || true
  run_test "default_loopback_bind" test_default_loopback_bind || true
  run_test "public_bind_with_pat" test_public_bind_with_pat || true

  local passed=true
  if [[ "${FAILURES}" -ne 0 ]]; then
    passed=false
  fi

  jsonl_emit "{\"type\":\"smoke_end\",\"suite\":\"yardb-bind\",\"tests_run\":${TESTS_RUN},\"failures\":${FAILURES},\"passed\":${passed}}"

  if [[ "${FAILURES}" -ne 0 ]]; then
    exit 1
  fi
}

main "$@"
