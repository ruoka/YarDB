# Shared helpers for yarexport / yarimport smoke tests.

set -euo pipefail

YAREXPORT_LIB_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=../yarsh/lib.sh
source "${YAREXPORT_LIB_DIR}/../yarsh/lib.sh"

YAREXPORT_BIN="${YAREXPORT_BIN:-${YARSH_ROOT_DIR}/${BUILD_DIR}/bin/yarexport}"
YARIMPORT_BIN="${YARIMPORT_BIN:-${YARSH_ROOT_DIR}/${BUILD_DIR}/bin/yarimport}"
LAST_EXPORT_OUTPUT=""
LAST_EXPORT_STATUS=0
LAST_IMPORT_OUTPUT=""
LAST_IMPORT_STATUS=0

stop_yardb_keep_db() {
  if [[ -n "${YARDB_PID}" ]] && kill -0 "${YARDB_PID}" 2>/dev/null; then
    kill "${YARDB_PID}" 2>/dev/null || true
    wait "${YARDB_PID}" 2>/dev/null || true
  fi
  YARDB_PID=""
}

cleanup_yardb_files() {
  if [[ -n "${YARDB_DB}" ]]; then
    rm -f "${YARDB_DB}" "${YARDB_DB}.pid" "${YARDB_DB}.log"
  fi
}

stop_yardb() {
  stop_yardb_keep_db
  cleanup_yardb_files
}

start_yardb_with_db() {
  local db_file=$1
  YARDB_PORT="$(pick_port)"
  YARDB_URL="http://127.0.0.1:${YARDB_PORT}"
  YARDB_DB="${db_file}"

  log "Starting yardb on ${YARDB_URL} (db=${YARDB_DB})"
  local yardb_args=(--clog --file="${YARDB_DB}")
  if [[ -n "${YARDB_PAT}" ]]; then
    yardb_args+=(--pat="${YARDB_PAT}")
  fi
  "${YARDB_BIN}" "${yardb_args[@]}" "${YARDB_PORT}" >"${YARDB_DB}.log" 2>&1 &
  YARDB_PID=$!

  local attempt curl_args=(-sf)
  if [[ -n "${YARDB_PAT}" ]]; then
    curl_args+=(-H "Authorization: Bearer ${YARDB_PAT}")
  fi
  for attempt in $(seq 1 60); do
    if curl "${curl_args[@]}" "${YARDB_URL}/" >/dev/null 2>&1; then
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

run_yarexport() {
  local db_file=$1
  shift || true
  set +e
  LAST_EXPORT_OUTPUT="$("${YAREXPORT_BIN}" --file="${db_file}" "$@" 2>&1)"
  LAST_EXPORT_STATUS=$?
  set -e
  return 0
}

run_yarimport() {
  local out_file=$1
  local input_file=$2
  shift 2 || true
  set +e
  LAST_IMPORT_OUTPUT="$("${YARIMPORT_BIN}" --file="${out_file}" --input="${input_file}" "$@" 2>&1)"
  LAST_IMPORT_STATUS=$?
  set -e
  return 0
}

assert_export_status() {
  local expected=$1
  local label=${2:-export_status}
  TESTS_RUN=$((TESTS_RUN + 1))
  if [[ "${LAST_EXPORT_STATUS}" -eq "${expected}" ]]; then
    jsonl_emit "{\"type\":\"smoke_assert_passed\",\"matcher\":\"${label}\"}"
    return 0
  fi
  fail "expected yarexport exit status ${expected}, got ${LAST_EXPORT_STATUS}"
  return 0
}

assert_import_status() {
  local expected=$1
  local label=${2:-import_status}
  TESTS_RUN=$((TESTS_RUN + 1))
  if [[ "${LAST_IMPORT_STATUS}" -eq "${expected}" ]]; then
    jsonl_emit "{\"type\":\"smoke_assert_passed\",\"matcher\":\"${label}\"}"
    return 0
  fi
  fail "expected yarimport exit status ${expected}, got ${LAST_IMPORT_STATUS}: ${LAST_IMPORT_OUTPUT}"
  return 0
}

assert_valid_jsonl() {
  local label=${1:-valid_jsonl}
  TESTS_RUN=$((TESTS_RUN + 1))
  if python3 - "${LAST_EXPORT_OUTPUT}" <<'PY'
import json, sys
text = sys.argv[1]
if not text.strip():
    print("empty export output", file=sys.stderr)
    raise SystemExit(1)
for i, line in enumerate(text.splitlines(), 1):
    if line.endswith(","):
        print(f"line {i}: trailing comma", file=sys.stderr)
        raise SystemExit(1)
    try:
        json.loads(line)
    except json.JSONDecodeError as e:
        print(f"line {i}: {e}", file=sys.stderr)
        raise SystemExit(1)
PY
  then
    jsonl_emit "{\"type\":\"smoke_assert_passed\",\"matcher\":\"${label}\"}"
    return 0
  fi
  fail "export output is not valid JSONL"
  return 0
}

assert_jsonl_record_shape() {
  local label=${1:-jsonl_record_shape}
  TESTS_RUN=$((TESTS_RUN + 1))
  if python3 - "${LAST_EXPORT_OUTPUT}" <<'PY'
import json, sys
required = {"collection", "status", "timestamp", "position", "previous", "document"}
for i, line in enumerate(sys.argv[1].splitlines(), 1):
    obj = json.loads(line)
    missing = required - obj.keys()
    if missing:
        print(f"line {i}: missing keys {sorted(missing)}", file=sys.stderr)
        raise SystemExit(1)
PY
  then
    jsonl_emit "{\"type\":\"smoke_assert_passed\",\"matcher\":\"${label}\"}"
    return 0
  fi
  fail "export records missing required keys"
  return 0
}

assert_live_jsonl_record_shape() {
  local label=${1:-live_jsonl_record_shape}
  TESTS_RUN=$((TESTS_RUN + 1))
  if python3 - "${LAST_EXPORT_OUTPUT}" <<'PY'
import json, sys
required = {"collection", "document"}
forbidden = {"position", "previous"}
for i, line in enumerate(sys.argv[1].splitlines(), 1):
    obj = json.loads(line)
    missing = required - obj.keys()
    if missing:
        print(f"line {i}: missing keys {sorted(missing)}", file=sys.stderr)
        raise SystemExit(1)
    present = forbidden & obj.keys()
    if present:
        print(f"line {i}: unexpected keys {sorted(present)}", file=sys.stderr)
        raise SystemExit(1)
    if "status" in obj and obj["status"] != "created":
        print(f"line {i}: live export must not include status={obj['status']!r}", file=sys.stderr)
        raise SystemExit(1)
PY
  then
    jsonl_emit "{\"type\":\"smoke_assert_passed\",\"matcher\":\"${label}\"}"
    return 0
  fi
  fail "live export records have unexpected shape"
  return 0
}

assert_jsonl_contains_document() {
  local collection=$1
  local field=$2
  local value=$3
  local label=${4:-jsonl_contains_document}
  TESTS_RUN=$((TESTS_RUN + 1))
  if python3 - "${collection}" "${field}" "${value}" "${LAST_EXPORT_OUTPUT}" <<'PY'
import json, sys
collection, field, value, text = sys.argv[1:5]
for line in text.splitlines():
    obj = json.loads(line)
    if obj.get("collection") != collection:
        continue
    doc = obj.get("document") or {}
    if str(doc.get(field)) == value:
        raise SystemExit(0)
print(f"no document in {collection} with {field}={value!r}", file=sys.stderr)
raise SystemExit(1)
PY
  then
    jsonl_emit "{\"type\":\"smoke_assert_passed\",\"matcher\":\"${label}\"}"
    return 0
  fi
  fail "expected document in collection ${collection} with ${field}=${value}"
  return 0
}

assert_jsonl_lacks_document() {
  local collection=$1
  local field=$2
  local value=$3
  local label=${4:-jsonl_lacks_document}
  TESTS_RUN=$((TESTS_RUN + 1))
  if python3 - "${collection}" "${field}" "${value}" "${LAST_EXPORT_OUTPUT}" <<'PY'
import json, sys
collection, field, value, text = sys.argv[1:5]
for line in text.splitlines():
    obj = json.loads(line)
    if obj.get("collection") != collection:
        continue
    doc = obj.get("document") or {}
    if str(doc.get(field)) == value:
        print(f"found unexpected document in {collection} with {field}={value!r}", file=sys.stderr)
        raise SystemExit(1)
raise SystemExit(0)
PY
  then
    jsonl_emit "{\"type\":\"smoke_assert_passed\",\"matcher\":\"${label}\"}"
    return 0
  fi
  fail "did not expect document in collection ${collection} with ${field}=${value}"
  return 0
}

assert_export_empty() {
  local label=${1:-export_empty}
  TESTS_RUN=$((TESTS_RUN + 1))
  if [[ -z "$(printf '%s' "${LAST_EXPORT_OUTPUT}" | tr -d '[:space:]')" ]]; then
    jsonl_emit "{\"type\":\"smoke_assert_passed\",\"matcher\":\"${label}\"}"
    return 0
  fi
  fail "expected empty export output"
  return 0
}

assert_jsonl_line_count_at_least() {
  local min=$1
  local label=${2:-jsonl_line_count}
  TESTS_RUN=$((TESTS_RUN + 1))
  local count
  count="$(printf '%s\n' "${LAST_EXPORT_OUTPUT}" | sed '/^[[:space:]]*$/d' | wc -l | tr -d ' ')"
  if [[ "${count}" -ge "${min}" ]]; then
    jsonl_emit "{\"type\":\"smoke_assert_passed\",\"matcher\":\"${label}\"}"
    return 0
  fi
  fail "expected at least ${min} JSONL lines, got ${count}"
  return 0
}

assert_jsonl_line_count_eq() {
  local expected=$1
  local label=${2:-jsonl_line_count_eq}
  TESTS_RUN=$((TESTS_RUN + 1))
  local count
  count="$(printf '%s\n' "${LAST_EXPORT_OUTPUT}" | sed '/^[[:space:]]*$/d' | wc -l | tr -d ' ')"
  if [[ "${count}" -eq "${expected}" ]]; then
    jsonl_emit "{\"type\":\"smoke_assert_passed\",\"matcher\":\"${label}\"}"
    return 0
  fi
  fail "expected exactly ${expected} JSONL lines, got ${count}"
  return 0
}

assert_file_smaller_than() {
  local smaller=$1
  local larger=$2
  local label=${3:-file_smaller}
  TESTS_RUN=$((TESTS_RUN + 1))
  local small_size large_size
  small_size="$(wc -c <"${smaller}" | tr -d ' ')"
  large_size="$(wc -c <"${larger}" | tr -d ' ')"
  if [[ "${small_size}" -lt "${large_size}" ]]; then
    jsonl_emit "{\"type\":\"smoke_assert_passed\",\"matcher\":\"${label}\"}"
    return 0
  fi
  fail "expected ${smaller} (${small_size}) < ${larger} (${large_size})"
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
  if [[ ! -x "${YAREXPORT_BIN}" ]]; then
    log "yarexport not found at ${YAREXPORT_BIN} — run ./tools/CB.sh debug build first"
    exit 1
  fi
  if [[ ! -x "${YARIMPORT_BIN}" ]]; then
    log "yarimport not found at ${YARIMPORT_BIN} — run ./tools/CB.sh debug build first"
    exit 1
  fi
  if ! command -v curl >/dev/null 2>&1; then
    log "curl is required for readiness checks"
    exit 1
  fi
}
