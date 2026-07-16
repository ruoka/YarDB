#!/usr/bin/env bash
# yarexport / yarimport smoke tests: seed yardb, stop server, export/import, validate.
#
# Usage:
#   ./tests/yarexport/smoke.sh [--jsonl] [--case NAME]
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
      echo "cases: export_empty, export_seeded, export_live, compact_roundtrip, force_preserves_on_bad_input, force_overwrite_ok, missing_file, help"
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

test_export_empty() {
  should_run export_empty || return 0
  begin_case export_empty

  stop_yardb
  start_yardb
  stop_yardb_keep_db

  run_yarexport "${YARDB_DB}"
  assert_export_status 0 "export_ok"
  assert_export_empty "no_records"

  cleanup_yardb_files
  end_case export_empty
}

test_export_seeded() {
  should_run export_seeded || return 0
  begin_case export_seeded
  local coll
  coll="$(collection export)"

  stop_yardb
  start_yardb

  run_yarsh "$(cat <<EOF
POST /${coll}
{"name":"alpha","value":1}
POST /${coll}
{"name":"beta","value":2}
EXIT
EOF
)"
  assert_contains " 201 " "seed_created"

  stop_yardb_keep_db

  run_yarexport "${YARDB_DB}"
  assert_export_status 0 "export_ok"
  assert_valid_jsonl "jsonl_syntax"
  assert_jsonl_record_shape "record_shape"
  assert_jsonl_line_count_at_least 2 "at_least_two_lines"
  assert_jsonl_contains_document "${coll}" "name" "alpha" "alpha_document"
  assert_jsonl_contains_document "${coll}" "name" "beta" "beta_document"

  cleanup_yardb_files
  end_case export_seeded
}

test_export_live() {
  should_run export_live || return 0
  begin_case export_live
  local coll
  coll="$(collection live)"

  stop_yardb
  start_yardb

  run_yarsh "$(cat <<EOF
POST /${coll}
{"name":"keep","email":"keep@example.com"}
POST /${coll}
{"name":"mutate","email":"old@example.com"}
POST /${coll}
{"name":"drop","email":"drop@example.com"}
EXIT
EOF
)"
  assert_contains " 201 " "seed_created"

  local mutate_id drop_id
  mutate_id="$(python3 -c 'import re,sys; ids=re.findall(r"\"_id\"\s*:\s*(\d+)", sys.argv[1]); print(ids[1] if len(ids)>=2 else "")' "${LAST_OUTPUT}")"
  drop_id="$(python3 -c 'import re,sys; ids=re.findall(r"\"_id\"\s*:\s*(\d+)", sys.argv[1]); print(ids[2] if len(ids)>=3 else "")' "${LAST_OUTPUT}")"

  run_yarsh "$(cat <<EOF
PUT /_db/${coll}
{"keys":["email"]}
PATCH /${coll}/${mutate_id}
{"email":"new@example.com"}
DELETE /${coll}/${drop_id}
EXIT
EOF
)"
  assert_contains " 200 " "index_or_patch_ok"
  assert_contains " 204 " "delete_ok"

  stop_yardb_keep_db

  run_yarexport "${YARDB_DB}"
  assert_export_status 0 "full_export_ok"
  local full_count
  full_count="$(printf '%s\n' "${LAST_EXPORT_OUTPUT}" | sed '/^[[:space:]]*$/d' | wc -l | tr -d ' ')"

  run_yarexport "${YARDB_DB}" --live
  assert_export_status 0 "live_export_ok"
  assert_valid_jsonl "live_jsonl_syntax"
  assert_live_jsonl_record_shape "live_shape"
  assert_jsonl_contains_document "${coll}" "name" "keep" "keep_live"
  assert_jsonl_contains_document "${coll}" "email" "new@example.com" "mutated_live"
  assert_jsonl_lacks_document "${coll}" "name" "drop" "deleted_absent"
  assert_jsonl_lacks_document "${coll}" "email" "old@example.com" "old_version_absent"
  assert_jsonl_contains_document "_db" "collection" "${coll}" "db_index_config"

  TESTS_RUN=$((TESTS_RUN + 1))
  local live_count
  live_count="$(printf '%s\n' "${LAST_EXPORT_OUTPUT}" | sed '/^[[:space:]]*$/d' | wc -l | tr -d ' ')"
  if [[ "${live_count}" -lt "${full_count}" ]]; then
    jsonl_emit "{\"type\":\"smoke_assert_passed\",\"matcher\":\"live_fewer_than_full\"}"
  else
    fail "expected live export (${live_count}) < full export (${full_count})"
  fi

  cleanup_yardb_files
  end_case export_live
}

test_compact_roundtrip() {
  should_run compact_roundtrip || return 0
  begin_case compact_roundtrip
  local coll compact_db live_jsonl
  coll="$(collection compact)"
  compact_db="$(mktemp "${TMPDIR:-/tmp}/yarcompact.XXXXXX.db")"
  live_jsonl="$(mktemp "${TMPDIR:-/tmp}/yarcompact.XXXXXX.jsonl")"
  rm -f "${compact_db}"

  stop_yardb
  start_yardb

  run_yarsh "$(cat <<EOF
POST /${coll}
{"name":"alpha","email":"alpha@example.com"}
POST /${coll}
{"name":"beta","email":"beta@example.com"}
EXIT
EOF
)"
  assert_contains " 201 " "seed_created"

  local beta_id
  beta_id="$(python3 -c 'import re,sys; ids=re.findall(r"\"_id\"\s*:\s*(\d+)", sys.argv[1]); print(ids[1] if len(ids)>=2 else "")' "${LAST_OUTPUT}")"

  run_yarsh "$(cat <<EOF
PUT /_db/${coll}
{"keys":["email"]}
PATCH /${coll}/${beta_id}
{"email":"beta2@example.com"}
PATCH /${coll}/${beta_id}
{"email":"beta3@example.com"}
EXIT
EOF
)"
  assert_contains " 200 " "history_created"

  stop_yardb_keep_db
  local source_db="${YARDB_DB}"

  run_yarexport "${source_db}" --live
  assert_export_status 0 "live_export_ok"
  printf '%s\n' "${LAST_EXPORT_OUTPUT}" >"${live_jsonl}"

  run_yarimport "${compact_db}" "${live_jsonl}"
  assert_import_status 0 "import_ok"
  assert_file_smaller_than "${compact_db}" "${source_db}" "compact_smaller"

  start_yardb_with_db "${compact_db}"

  run_yarsh "$(cat <<EOF
GET /${coll}?\$filter=email eq 'beta3@example.com'
EXIT
EOF
)"
  assert_contains "beta3@example.com" "index_query_works"

  run_yarsh "$(cat <<EOF
GET /${coll}?\$count=true
EXIT
EOF
)"
  assert_contains "Response Body:" "count_body_section"
  if ! printf '%s\n' "${LAST_OUTPUT}" | awk '/Response Body:/{getline; if ($0 == "2") found=1} END{exit !found}'; then
    fail "expected count body to be 2 after compact import"
  fi
  TESTS_RUN=$((TESTS_RUN + 1))
  jsonl_emit '{"type":"smoke_assert_passed","matcher":"live_count_two"}'

  stop_yardb_keep_db
  rm -f "${source_db}" "${source_db}.pid" "${source_db}.log" "${live_jsonl}"
  cleanup_yardb_files
  end_case compact_roundtrip
}

test_force_preserves_on_bad_input() {
  should_run force_preserves_on_bad_input || return 0
  begin_case force_preserves_on_bad_input
  local coll marker_db bad_jsonl before_sha
  coll="$(collection forcebad)"
  marker_db="$(mktemp "${TMPDIR:-/tmp}/yarforce.XXXXXX.db")"
  bad_jsonl="$(mktemp "${TMPDIR:-/tmp}/yarforce.XXXXXX.jsonl")"
  rm -f "${marker_db}"

  stop_yardb
  start_yardb

  run_yarsh "$(cat <<EOF
POST /${coll}
{"name":"keep-me","marker":"original-db"}
EXIT
EOF
)"
  assert_contains " 201 " "seed_created"

  stop_yardb_keep_db
  cp "${YARDB_DB}" "${marker_db}"
  before_sha="$(sha256sum "${marker_db}" | awk '{print $1}')"
  printf '%s\n' '{"collection":"broken","document":{"name":"x"},"status":"deleted"}' >"${bad_jsonl}"

  run_yarimport "${marker_db}" "${bad_jsonl}" --force
  assert_import_status 1 "force_bad_input_fails"
  LAST_OUTPUT="${LAST_IMPORT_OUTPUT}"
  assert_contains "refusing to import status=deleted" "force_bad_input_message"

  TESTS_RUN=$((TESTS_RUN + 1))
  local after_sha
  after_sha="$(sha256sum "${marker_db}" | awk '{print $1}')"
  if [[ "${before_sha}" == "${after_sha}" ]]; then
    jsonl_emit "{\"type\":\"smoke_assert_passed\",\"matcher\":\"force_preserves_original_db\"}"
  else
    fail "expected --force failure to leave original database bytes unchanged"
  fi

  TESTS_RUN=$((TESTS_RUN + 1))
  if [[ ! -e "${marker_db}.yarimport.tmp" && ! -e "${marker_db}.yarimport.tmp.pid" ]]; then
    jsonl_emit "{\"type\":\"smoke_assert_passed\",\"matcher\":\"force_cleans_staging\"}"
  else
    fail "expected failed --force import to remove staging sidecar files"
  fi

  rm -f "${marker_db}" "${marker_db}.pid" "${bad_jsonl}"
  cleanup_yardb_files
  end_case force_preserves_on_bad_input
}

test_force_overwrite_ok() {
  should_run force_overwrite_ok || return 0
  begin_case force_overwrite_ok
  local coll target_db live_jsonl
  coll="$(collection forceok)"
  target_db="$(mktemp "${TMPDIR:-/tmp}/yarforce.XXXXXX.db")"
  live_jsonl="$(mktemp "${TMPDIR:-/tmp}/yarforce.XXXXXX.jsonl")"
  rm -f "${target_db}"

  stop_yardb
  start_yardb

  run_yarsh "$(cat <<EOF
POST /${coll}
{"name":"alpha","email":"alpha@example.com"}
EXIT
EOF
)"
  assert_contains " 201 " "seed_created"

  stop_yardb_keep_db
  cp "${YARDB_DB}" "${target_db}"

  run_yarexport "${YARDB_DB}" --live
  assert_export_status 0 "live_export_ok"
  printf '%s\n' "${LAST_EXPORT_OUTPUT}" >"${live_jsonl}"

  run_yarimport "${target_db}" "${live_jsonl}" --force
  assert_import_status 0 "force_overwrite_ok"

  TESTS_RUN=$((TESTS_RUN + 1))
  if [[ -s "${target_db}" && ! -e "${target_db}.yarimport.tmp" ]]; then
    jsonl_emit "{\"type\":\"smoke_assert_passed\",\"matcher\":\"force_target_rewritten\"}"
  else
    fail "expected successful --force import to rewrite target and remove staging sidecar"
  fi

  start_yardb_with_db "${target_db}"
  run_yarsh "$(cat <<EOF
GET /${coll}
EXIT
EOF
)"
  assert_contains "alpha@example.com" "force_overwrite_readable"
  assert_contains '"name" : "alpha"' "force_overwrite_document"

  stop_yardb_keep_db
  rm -f "${YARDB_DB}" "${YARDB_DB}.pid" "${YARDB_DB}.log" "${live_jsonl}"
  cleanup_yardb_files
  end_case force_overwrite_ok
}

test_missing_file() {
  should_run missing_file || return 0
  begin_case missing_file
  local missing
  missing="$(mktemp "${TMPDIR:-/tmp}/yarexport_missing.XXXXXX.db")"
  rm -f "${missing}"

  run_yarexport "${missing}"
  assert_export_status 1 "missing_file_nonzero"
  LAST_OUTPUT="${LAST_EXPORT_OUTPUT}"
  assert_contains "not found" "missing_file_message"

  end_case missing_file
}

test_help() {
  should_run help || return 0
  begin_case help
  LAST_OUTPUT="$("${YAREXPORT_BIN}" --help 2>&1)"
  assert_contains "yarexport" "help_banner"
  assert_contains "--file=" "help_file_option"
  assert_contains "--live" "help_live_option"
  assert_contains "JSONL" "help_jsonl_note"

  LAST_OUTPUT="$("${YARIMPORT_BIN}" --help 2>&1)"
  assert_contains "yarimport" "import_help_banner"
  assert_contains "--file=" "import_help_file"
  assert_contains "--input=" "import_help_input"
  end_case help
}

main() {
  require_bins
  trap stop_yardb EXIT

  jsonl_emit "{\"type\":\"smoke_start\",\"schema\":\"yarexport-smoke\",\"version\":1}"
  log "yarexport/yarimport smoke tests (build=${BUILD_DIR})"

  test_export_empty
  test_export_seeded
  test_export_live
  test_compact_roundtrip
  test_force_preserves_on_bad_input
  test_force_overwrite_ok
  test_missing_file
  test_help

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
