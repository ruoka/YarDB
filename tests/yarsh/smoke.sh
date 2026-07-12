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
      echo "cases: crud, put, patch, count, top_skip, orderby, select, filter_eq_gt, filter_in, filter_ne, filter_or, filter_startswith, head, if_none_match, bad_json, auth_required, auth_crud"
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

test_put() {
  should_run put || return 0
  begin_case put
  local coll
  coll="$(collection put)"

  run_yarsh "$(cat <<EOF
POST /${coll}
{"name":"original","value":1}
PUT /${coll}/1
{"name":"replaced","value":99}
GET /${coll}/1
PUT /${coll}/999
{"name":"upserted","value":200}
GET /${coll}/999
EXIT
EOF
)"
  assert_contains " 201 " "post_created"
  assert_contains " 200 " "put_update_ok"
  assert_contains '"name" : "replaced"' "put_replace_name"
  assert_contains '"value" : 99' "put_replace_value"
  assert_contains " 201 " "put_upsert_created"
  assert_contains '"name" : "upserted"' "put_upsert_name"
  assert_contains '"value" : 200' "put_upsert_value"
  end_case put
}

test_patch() {
  should_run patch || return 0
  begin_case patch
  local coll
  coll="$(collection patch)"

  run_yarsh "$(cat <<EOF
POST /${coll}
{"name":"keep","value":10}
PATCH /${coll}/1
{"value":20}
GET /${coll}/1
EXIT
EOF
)"
  assert_contains " 201 " "post_created"
  assert_contains " 200 " "patch_update_ok"
  assert_contains '"name" : "keep"' "patch_preserves_name"
  assert_contains '"value" : 20' "patch_updates_value"
  end_case patch
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

test_top_skip() {
  should_run top_skip || return 0
  begin_case top_skip
  local coll
  coll="$(collection topskip)"

  run_yarsh "$(cat <<EOF
POST /${coll}
{"name":"item1","value":1}
POST /${coll}
{"name":"item2","value":2}
POST /${coll}
{"name":"item3","value":3}
POST /${coll}
{"name":"item4","value":4}
POST /${coll}
{"name":"item5","value":5}
EXIT
EOF
)"
  assert_contains " 201 " "seed_created"

  run_yarsh "$(cat <<EOF
GET /${coll}?\$top=2
EXIT
EOF
)"
  assert_contains " 200 " "top_status_ok"
  assert_last_json_array_length le 2 "top_limits_results"

  run_yarsh "$(cat <<EOF
GET /${coll}?\$skip=2&\$top=2
EXIT
EOF
)"
  assert_contains " 200 " "skip_top_status_ok"
  assert_last_json_array_length eq 2 "skip_top_page_size"

  run_yarsh "$(cat <<EOF
GET /${coll}?\$skip=2
EXIT
EOF
)"
  assert_contains " 200 " "skip_status_ok"
  assert_last_json_array_length eq 3 "skip_offset"
  end_case top_skip
}

test_orderby() {
  should_run orderby || return 0
  begin_case orderby
  local coll
  coll="$(collection orderby)"

  run_yarsh "$(cat <<EOF
POST /${coll}
{"name":"low","value":10}
POST /${coll}
{"name":"mid","value":20}
POST /${coll}
{"name":"high","value":30}
GET /${coll}?\$orderby=value%20desc
EXIT
EOF
)"
  assert_contains " 200 " "status_ok"
  assert_last_json_array_first_field value 30 "orderby_desc_first"
  end_case orderby
}

test_select() {
  should_run select || return 0
  begin_case select
  local coll
  coll="$(collection select)"

  run_yarsh "$(cat <<EOF
POST /${coll}
{"name":"TestUser","email":"test@example.com","age":30,"status":"active"}
GET /${coll}?\$select=name,email
EXIT
EOF
)"
  assert_contains " 200 " "status_ok"
  assert_last_json_projection "name,email" "age,status" "select_name_email"
  end_case select
}

test_filter_eq_gt() {
  should_run filter_eq_gt || return 0
  begin_case filter_eq_gt
  local coll
  coll="$(collection feqgt)"

  run_yarsh "$(cat <<EOF
POST /${coll}
{"name":"low","value":10,"status":"active"}
POST /${coll}
{"name":"high","value":40,"status":"active"}
POST /${coll}
{"name":"gone","value":50,"status":"deleted"}
EXIT
EOF
)"
  assert_contains " 201 " "seed_created"

  run_yarsh "$(cat <<EOF
GET /${coll}?\$filter=status%20eq%20'active'
EXIT
EOF
)"
  assert_contains " 200 " "filter_eq_ok"
  assert_last_json_array_length eq 2 "filter_eq_count"
  assert_last_json_array_excludes_field_value name gone "filter_eq_excludes_deleted"

  run_yarsh "$(cat <<EOF
GET /${coll}?\$filter=value%20gt%2025%20and%20status%20eq%20'active'
EXIT
EOF
)"
  assert_contains " 200 " "filter_gt_ok"
  assert_last_json_array_length eq 1 "filter_gt_count"
  assert_last_json_array_first_field value 40 "filter_gt_value"
  end_case filter_eq_gt
}

test_filter_in() {
  should_run filter_in || return 0
  begin_case filter_in
  local coll
  coll="$(collection fin)"

  run_yarsh "$(cat <<EOF
POST /${coll}
{"name":"Alice","status":"active"}
POST /${coll}
{"name":"Bob","status":"pending"}
POST /${coll}
{"name":"Charlie","status":"inactive"}
EXIT
EOF
)"
  assert_contains " 201 " "seed_created"

  run_yarsh "$(cat <<EOF
GET /${coll}?\$filter=status%20in%20('active','pending')
EXIT
EOF
)"
  assert_contains " 200 " "filter_in_ok"
  assert_last_json_array_length eq 2 "filter_in_count"
  assert_last_json_array_includes_field_value name Alice "filter_in_alice"
  assert_last_json_array_includes_field_value name Bob "filter_in_bob"
  assert_last_json_array_excludes_field_value name Charlie "filter_in_excludes_charlie"
  end_case filter_in
}

test_filter_or() {
  should_run filter_or || return 0
  begin_case filter_or
  local coll
  coll="$(collection for)"

  run_yarsh "$(cat <<EOF
POST /${coll}
{"name":"young_inactive","age":20,"status":"inactive"}
POST /${coll}
{"name":"old_inactive","age":35,"status":"inactive"}
POST /${coll}
{"name":"young_active","age":20,"status":"active"}
EXIT
EOF
)"
  assert_contains " 201 " "seed_created"

  run_yarsh "$(cat <<EOF
GET /${coll}?\$filter=age%20gt%2025%20or%20status%20eq%20'active'
EXIT
EOF
)"
  assert_contains " 200 " "filter_or_ok"
  assert_last_json_array_length eq 2 "filter_or_count"
  assert_last_json_array_includes_field_value name old_inactive "filter_or_old_inactive"
  assert_last_json_array_includes_field_value name young_active "filter_or_young_active"
  assert_last_json_array_excludes_field_value name young_inactive "filter_or_excludes_young_inactive"
  end_case filter_or
}

test_filter_startswith() {
  should_run filter_startswith || return 0
  begin_case filter_startswith
  local coll
  coll="$(collection fsw)"

  run_yarsh "$(cat <<EOF
POST /${coll}
{"name":"Alice","email":"alice@example.com"}
POST /${coll}
{"name":"Bob","email":"bob@test.com"}
POST /${coll}
{"name":"Charlie","email":"charlie@example.org"}
POST /${coll}
{"name":"David","email":"david@test.org"}
EXIT
EOF
)"
  assert_contains " 201 " "seed_created"

  run_yarsh "$(cat <<EOF
GET /${coll}?\$filter=startswith(name,'A')
EXIT
EOF
)"
  assert_contains " 200 " "filter_startswith_ok"
  assert_last_json_array_length eq 1 "filter_startswith_count"
  assert_last_json_array_first_field name Alice "filter_startswith_alice"
  assert_last_json_array_excludes_field_value name Bob "filter_startswith_excludes_bob"
  end_case filter_startswith
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

test_auth_required() {
  should_run auth_required || return 0
  begin_case auth_required
  local pat
  pat="smoke-pat-${RANDOM}"

  stop_yardb
  YARDB_PAT="${pat}"
  start_yardb

  run_yarsh "$(cat <<EOF
GET /
EXIT
EOF
)"
  assert_contains " 401 " "unauthorized_without_pat"

  run_yarsh "$(cat <<EOF
GET /
@Authorization: Bearer ${pat}
EXIT
EOF
)"
  assert_contains " 200 " "authorized_with_pat"

  YARDB_PAT=""
  end_case auth_required
}

test_auth_crud() {
  should_run auth_crud || return 0
  begin_case auth_crud
  local pat coll
  pat="smoke-pat-${RANDOM}"
  coll="$(collection authcrud)"

  stop_yardb
  YARDB_PAT="${pat}"
  start_yardb

  run_yarsh "$(cat <<EOF
POST /${coll}
{"name":"secret"}
EXIT
EOF
)"
  assert_contains " 401 " "post_unauthorized_without_pat"

  run_yarsh "$(cat <<EOF
POST /${coll}
@Authorization: Bearer ${pat}
{"name":"secret"}
GET /${coll}/1
@Authorization: Bearer ${pat}
DELETE /${coll}/1
@Authorization: Bearer ${pat}
EXIT
EOF
)"
  assert_contains " 201 " "post_authorized"
  assert_contains " 200 " "get_authorized"
  assert_contains " 204 " "delete_authorized"
  assert_contains '"name" : "secret"' "body_name"

  YARDB_PAT=""
  end_case auth_crud
}

main() {
  require_bins
  trap stop_yardb EXIT

  jsonl_emit "{\"type\":\"smoke_start\",\"schema\":\"yarsh-smoke\",\"version\":1}"
  log "yarsh smoke tests (build=${BUILD_DIR})"

  if [[ -z "${SELECTED_CASE}" || ( "${SELECTED_CASE}" != "auth_required" && "${SELECTED_CASE}" != "auth_crud" ) ]]; then
    start_yardb
  fi

  test_crud
  test_put
  test_patch
  test_count
  test_top_skip
  test_orderby
  test_select
  test_filter_eq_gt
  test_filter_in
  test_filter_ne
  test_filter_or
  test_filter_startswith
  test_head
  test_if_none_match
  test_bad_json
  test_auth_required
  test_auth_crud

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