#!/usr/bin/env bash
# yardb MCP bridge smoke tests against a local yardb instance.
#
# Usage:
#   ./tests/mcp/smoke.sh [--jsonl] [--case NAME]
#
# Requires: ./tools/CB.sh debug build, python3

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "${SCRIPT_DIR}/lib.sh"

SELECTED_CASE=""
START_MS=$(python3 - <<'PY'
import time
print(int(time.time() * 1000))
PY
)

while [[ $# -gt 0 ]]; do
  case "$1" in
    --jsonl) JSONL_MODE=1 ;;
    --case) shift; SELECTED_CASE="${1:-}" ;;
    --help|-h)
      echo "usage: smoke.sh [--jsonl] [--case NAME]"
      echo "cases: tools_list, probes, crud, filter, indexes, sse"
      echo "note: sse requires pip install -r tools/requirements-mcp-sse.txt"
      echo "      (or MCP_SSE_PYTHON=/path/to/venv/bin/python)"
      exit 0
      ;;
    *)
      echo "unknown argument: $1" >&2
      exit 2
      ;;
  esac
  shift
done

should_run() {
  [[ -z "${SELECTED_CASE}" || "${SELECTED_CASE}" == "$1" ]]
}

consume_case_output() {
  local name=$1
  local status=$2
  local output=$3
  local stats_line="" checks=0 case_failures=0 skipped=0

  while IFS= read -r line; do
    [[ -n "${line}" ]] || continue
    if [[ "${line}" == *'"type":"smoke_case_stats"'* ]] || [[ "${line}" == *'"type": "smoke_case_stats"'* ]]; then
      stats_line="${line}"
      continue
    fi
    jsonl_emit "${line}"
  done <<<"${output}"

  if [[ -n "${stats_line}" ]]; then
    checks="$(python3 -c 'import json,sys; print(json.loads(sys.argv[1])["checks"])' "${stats_line}")"
    case_failures="$(python3 -c 'import json,sys; print(json.loads(sys.argv[1])["failures"])' "${stats_line}")"
    skipped="$(python3 -c 'import json,sys; print(json.loads(sys.argv[1]).get("skipped", 0))' "${stats_line}")"
    TESTS_RUN=$((TESTS_RUN + checks))
    FAILURES=$((FAILURES + case_failures))
    if [[ "${skipped}" -ne 0 ]]; then
      log "SKIP: ${name} (optional deps not installed)"
      jsonl_emit "{\"type\":\"smoke_case_skipped\",\"name\":\"${name}\"}"
    fi
  elif [[ "${status}" -ne 0 ]]; then
    fail "case ${name} exited ${status} without stats"
  fi

  if [[ "${status}" -ne 0 && "${case_failures}" -eq 0 && "${skipped}" -eq 0 ]]; then
    fail "case ${name} failed"
  fi
}

run_case() {
  local name=$1
  should_run "${name}" || return 0
  begin_case "${name}"

  local output status=0 err
  err="$(mktemp "${TMPDIR:-/tmp}/yardb_mcp_smoke.XXXXXX.err")"
  output="$(run_mcp_case "${name}" 2>"${err}")" || status=$?

  if [[ -s "${err}" ]]; then
    cat "${err}" >&2
  fi
  rm -f "${err}"

  consume_case_output "${name}" "${status}" "${output}"
  end_case "${name}"
}

run_sse_case() {
  should_run sse || return 0
  begin_case sse

  local output status=0 err
  err="$(mktemp "${TMPDIR:-/tmp}/yardb_mcp_smoke.XXXXXX.err")"
  output="$(run_mcp_sse_case 2>"${err}")" || status=$?

  if [[ -s "${err}" ]]; then
    cat "${err}" >&2
  fi
  rm -f "${err}"

  consume_case_output sse "${status}" "${output}"
  end_case sse
}

main() {
  require_mcp_bins
  trap stop_yardb EXIT

  jsonl_emit '{"type":"smoke_start","schema":"mcp-smoke","version":1}'
  log "mcp smoke tests (build=${BUILD_DIR}, bridge=${MCP_PY})"

  start_yardb

  run_case tools_list
  run_case probes
  run_case crud
  run_case filter
  run_case indexes
  run_sse_case

  local end_ms duration_ms passed
  end_ms=$(python3 - <<'PY'
import time
print(int(time.time() * 1000))
PY
)
  duration_ms=$((end_ms - START_MS))
  passed=$([[ "${FAILURES}" -eq 0 ]] && echo true || echo false)

  jsonl_emit "{\"type\":\"smoke_summary\",\"tests_run\":${TESTS_RUN},\"failures\":${FAILURES},\"passed\":${passed},\"duration_ms\":${duration_ms}}"
  jsonl_emit "{\"type\":\"smoke_end\",\"passed\":${passed},\"duration_ms\":${duration_ms}}"

  if [[ "${FAILURES}" -gt 0 ]]; then
    log "FAILED: ${FAILURES} assertion(s) failed (${TESTS_RUN} checks run)"
    exit 1
  fi

  log "OK: all ${TESTS_RUN} checks passed (${duration_ms}ms)"
}

main "$@"
