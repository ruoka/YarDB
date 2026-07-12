#!/usr/bin/env bash
# Piped yarsh smoke tests against a local yardb instance.
#
# Usage:
#   ./tests/yarsh/smoke.sh [--jsonl] [--case NAME]
#
# Requires: ./tools/CB.sh debug build

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
      echo "cases: crud, count, filter_ne, head, if_none_match, bad_json"
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

collection() {
  printf '%s%s' "${RUN_ID}" "$1"
}

test_crud() {
  should_run crud || return 0
  begin_case crud
  local coll
  coll="$(collection crud)"

  run_yarsh "$(cat <<EOF
POST /${coll}
{"name":"alpha","status":"active"}
GET /${coll}/1
DELETE /${coll}/1
EXIT
EOF
)"
  assert_contains " 201 " "status_created"
  assert_contains " 200 " "status_ok"
  assert_contains " 204 " "status_no_content"
  assert_contains '"name" : "alpha"' "body_name"
  end_case crud
}

test_count() {
  should_run count || return 0
  begin_case count
  local coll
  coll="$(collection count)"

  run_yarsh "$(cat <<EOF
POST /${coll}
{"name":"a","value":1}
POST /${coll}
{"name":"b","value":2}
POST /${coll}
{"name":"c","value":3}
GET /${coll}?\$count=true
EXIT
EOF
)"
  assert_contains "Response Body:" "count_body_section"
  if ! printf '%s\n' "${LAST_OUTPUT}" | awk '/Response Body:/{getline; if ($0 == "3") found=1} END{exit !found}'; then
    fail "expected count body to be 3 after Response Body:"
  fi
  TESTS_RUN=$((TESTS_RUN + 1))
  jsonl_emit '{"type":"smoke_assert_passed","matcher":"count_value_three"}'
  end_case count
}

test_filter_ne() {
  should_run filter_ne || return 0
  begin_case filter_ne
  local coll filtered_output
  coll="$(collection ne)"

  run_yarsh "$(cat <<EOF
POST /${coll}
{"name":"keep1","status":"active"}
POST /${coll}
{"name":"drop","status":"deleted"}
POST /${coll}
{"name":"keep2","status":"active"}
EXIT
EOF
)"
  run_yarsh "$(cat <<EOF
GET /${coll}?\$filter=status%20ne%20'deleted'
EXIT
EOF
)"
  filtered_output="${LAST_OUTPUT}"
  LAST_OUTPUT="${filtered_output}"
  assert_contains " 200 " "status_ok"
  assert_contains '"name" : "keep1"' "keep1"
  assert_contains '"name" : "keep2"' "keep2"
  assert_not_contains '"name" : "drop"' "exclude_deleted"
  end_case filter_ne
}

test_head() {
  should_run head || return 0
  begin_case head
  local coll head_output
  coll="$(collection head)"

  run_yarsh "$(cat <<EOF
POST /${coll}
{"name":"headtest"}
EXIT
EOF
)"
  run_yarsh "$(cat <<EOF
HEAD /${coll}/1
EXIT
EOF
)"
  head_output="${LAST_OUTPUT}"
  LAST_OUTPUT="${head_output}"
  assert_contains " 200 " "status_ok"
  assert_contains "  ETag: " "etag_header"
  assert_not_contains "Response Body:" "no_body"
  end_case head
}

test_if_none_match() {
  should_run if_none_match || return 0
  begin_case if_none_match
  local coll etag
  coll="$(collection inm)"

  run_yarsh "$(cat <<EOF
POST /${coll}
{"name":"cached"}
GET /${coll}/1
EXIT
EOF
)"
  assert_contains "  ETag: " "etag_present"
  etag="$(printf '%s\n' "${LAST_OUTPUT}" | sed -n 's/^  ETag: //p' | head -1 | tr -d '\r')"
  if [[ -z "${etag}" ]]; then
    fail "could not extract ETag from GET response"
    end_case if_none_match
    return 1
  fi

  run_yarsh "$(cat <<EOF
GET /${coll}/1
@If-None-Match: ${etag}
EXIT
EOF
)"
  assert_contains " 304 " "status_not_modified"
  assert_not_contains "Response Body:" "no_body_on_304"
  end_case if_none_match
}

test_bad_json() {
  should_run bad_json || return 0
  begin_case bad_json
  local coll
  coll="$(collection badjson)"

  run_yarsh "$(cat <<EOF
POST /${coll}
{not valid json}
GET /
EXIT
EOF
)"
  assert_contains "Invalid JSON body:" "invalid_json_message"
  assert_contains " 200 " "shell_continues_after_bad_json"
  end_case bad_json
}

main() {
  require_bins
  trap stop_yardb EXIT

  jsonl_emit "{\"type\":\"smoke_start\",\"schema\":\"yarsh-smoke\",\"version\":1}"
  log "yarsh smoke tests (build=${BUILD_DIR})"

  start_yardb

  test_crud
  test_count
  test_filter_ne
  test_head
  test_if_none_match
  test_bad_json

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