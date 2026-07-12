#!/usr/bin/env bash
# yarproxy smoke tests: multi-replica yardb cluster behind the proxy.
#
# Usage:
#   ./tests/yarproxy/smoke.sh [--jsonl] [--case NAME] [--replicas=N]
#
# Requires: ./tools/CB.sh debug build

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "${SCRIPT_DIR}/lib.sh"

SELECTED_CASE=""
CLUSTER_STARTED=0
REPLICA_COUNT="${REPLICA_COUNT:-2}"
ROUND_ROBIN_MARKERS=()
ROUND_ROBIN_OUTPUTS=()
START_MS=$(python3 - <<'PY'
import time
print(int(time.time() * 1000))
PY
)

while [[ $# -gt 0 ]]; do
  case "$1" in
    --jsonl) JSONL_MODE=1 ;;
    --case) shift; SELECTED_CASE="${1:-}" ;;
    --replicas)
      shift
      REPLICA_COUNT="${1:-}"
      ;;
    --replicas=*)
      REPLICA_COUNT="${1#--replicas=}"
      ;;
    --help|-h)
      echo "usage: smoke.sh [--jsonl] [--case NAME] [--replicas=N]"
      echo "cases: no_replicas, help, proxy_crud, write_fanout, read_round_robin"
      echo "default replicas: 2 (override with --replicas=N or REPLICA_COUNT=N)"
      exit 0
      ;;
    *)
      echo "unknown argument: $1" >&2
      exit 2
      ;;
  esac
  shift
done

if ! [[ "${REPLICA_COUNT}" =~ ^[0-9]+$ ]] || [[ "${REPLICA_COUNT}" -lt 2 ]]; then
  echo "replicas must be an integer >= 2 (got: ${REPLICA_COUNT})" >&2
  exit 2
fi

should_run() {
  [[ -z "${SELECTED_CASE}" || "${SELECTED_CASE}" == "$1" ]]
}

collection() {
  printf '%s%s' "${RUN_ID}" "$1"
}

ensure_cluster() {
  local i
  if [[ "${CLUSTER_STARTED}" -eq 1 ]]; then
    return 0
  fi
  for i in $(seq 1 "${REPLICA_COUNT}"); do
    start_replica
  done
  start_yarproxy
  CLUSTER_STARTED=1
}

test_no_replicas() {
  should_run no_replicas || return 0
  begin_case no_replicas
  local output status
  set +e
  output="$("${YARPROXY_BIN}" 2>&1)"
  status=$?
  set -e
  LAST_OUTPUT="${output}"
  assert_exit_status 1 "${status}" "no_replicas_nonzero"
  assert_contains "--replica=" "usage_shows_replica"
  end_case no_replicas
}

test_help() {
  should_run help || return 0
  begin_case help
  LAST_OUTPUT="$("${YARPROXY_BIN}" --help 2>&1)"
  assert_contains "yarproxy" "help_banner"
  assert_contains "--replica=" "help_replica_option"
  end_case help
}

test_proxy_crud() {
  should_run proxy_crud || return 0
  begin_case proxy_crud
  ensure_cluster
  local coll
  coll="$(collection crud)"

  run_yarsh_proxy "$(cat <<EOF
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
  end_case proxy_crud
}

test_write_fanout() {
  should_run write_fanout || return 0
  begin_case write_fanout
  ensure_cluster
  local coll url
  coll="$(collection fanout)"

  run_yarsh_proxy "$(cat <<EOF
POST /${coll}
{"name":"fanout","value":42}
EXIT
EOF
)"
  assert_contains " 201 " "proxy_post_created"

  for url in "${REPLICA_URLS[@]}"; do
    run_yarsh_at "${url}" "$(cat <<EOF
GET /${coll}/1
EXIT
EOF
)"
    assert_contains " 200 " "replica_status_ok"
    assert_contains '"name" : "fanout"' "replica_body_name"
    assert_contains '"value" : 42' "replica_body_value"
  done
  end_case write_fanout
}

test_read_round_robin() {
  should_run read_round_robin || return 0
  begin_case read_round_robin
  ensure_cluster
  local coll i marker
  coll="$(collection rr)"
  ROUND_ROBIN_MARKERS=()
  ROUND_ROBIN_OUTPUTS=()

  for i in "${!REPLICA_URLS[@]}"; do
    marker="replica${i}"
    ROUND_ROBIN_MARKERS+=("${marker}")
    run_yarsh_at "${REPLICA_URLS[$i]}" "$(cat <<EOF
POST /${coll}
{"marker":"${marker}"}
EXIT
EOF
)"
    assert_contains " 201 " "seed_${marker}"
  done

  for i in "${!REPLICA_URLS[@]}"; do
    run_yarsh_proxy "$(cat <<EOF
GET /${coll}
EXIT
EOF
)"
    assert_contains " 200 " "proxy_get_ok_${i}"
    ROUND_ROBIN_OUTPUTS+=("${LAST_OUTPUT}")
  done

  assert_round_robin_all_markers "read_round_robin_cycle"
  end_case read_round_robin
}

main() {
  require_bins
  trap stop_cluster EXIT

  RUN_ID="smoke$(date +%s)${RANDOM}"

  jsonl_emit "{\"type\":\"smoke_start\",\"schema\":\"yarproxy-smoke\",\"version\":1}"
  log "yarproxy smoke tests (build=${BUILD_DIR}, replicas=${REPLICA_COUNT})"

  test_no_replicas
  test_help
  test_proxy_crud
  test_write_fanout
  test_read_round_robin

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