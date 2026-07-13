#!/usr/bin/env bash
# yarexport smoke tests: seed yardb, stop server, export JSONL, validate output.
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
      echo "cases: export_empty, export_seeded, missing_file, help"
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
  assert_contains "JSONL" "help_jsonl_note"
  end_case help
}

main() {
  require_bins
  trap stop_yardb EXIT

  jsonl_emit "{\"type\":\"smoke_start\",\"schema\":\"yarexport-smoke\",\"version\":1}"
  log "yarexport smoke tests (build=${BUILD_DIR})"

  test_export_empty
  test_export_seeded
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