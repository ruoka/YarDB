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
      echo "cases: no_replicas, help, proxy_crud, write_fanout, read_round_robin, header_forward_auth, header_forward_correlation, partial_backend_drain, empty_backends_502, head_no_hang, sse_no_poison, truncated_body_no_hang, truncated_backend_no_poison, transfer_encoding_no_smuggle"
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

test_header_forward_auth() {
  should_run header_forward_auth || return 0
  begin_case header_forward_auth
  local pat coll saved_pat
  pat="smoke-proxy-pat-${RANDOM}"
  coll="$(collection hauth)"
  saved_pat="${REPLICA_PAT}"
  REPLICA_PAT="${pat}"

  stop_cluster
  CLUSTER_STARTED=0
  ensure_cluster

  run_yarsh_proxy "$(cat <<EOF
GET /
EXIT
EOF
)"
  assert_contains " 401 " "proxy_unauthorized_without_pat"

  run_yarsh_proxy "$(cat <<EOF
GET /
@Authorization: Bearer ${pat}
EXIT
EOF
)"
  assert_contains " 200 " "proxy_authorized_list"

  run_yarsh_proxy "$(cat <<EOF
POST /${coll}
@Authorization: Bearer ${pat}
{"name":"proxied"}
EXIT
EOF
)"
  assert_contains " 201 " "proxy_post_authorized"
  assert_contains '"name" : "proxied"' "proxy_post_body"

  REPLICA_PAT="${saved_pat}"
  end_case header_forward_auth
}

test_header_forward_correlation() {
  should_run header_forward_correlation || return 0
  begin_case header_forward_correlation
  local pat coll trace_id saved_pat
  pat="smoke-proxy-pat-${RANDOM}"
  trace_id="smoke-trace-${RANDOM}"
  coll="$(collection hcorr)"
  saved_pat="${REPLICA_PAT}"
  REPLICA_PAT="${pat}"

  stop_cluster
  CLUSTER_STARTED=0
  ensure_cluster

  run_yarsh_proxy "$(cat <<EOF
POST /${coll}
@Authorization: Bearer ${pat}
@X-Correlation-ID: ${trace_id}
{"marker":"corr"}
EXIT
EOF
)"
  assert_contains " 201 " "proxy_post_created"
  assert_all_replica_logs_contain "${trace_id}" "correlation_id_in_replica_logs"

  REPLICA_PAT="${saved_pat}"
  end_case header_forward_correlation
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

test_partial_backend_drain() {
  should_run partial_backend_drain || return 0
  begin_case partial_backend_drain
  # Auth cases leave a PAT-protected cluster running; restart without PAT so
  # unauthenticated GET / is meaningful for the stale-response check.
  stop_cluster
  CLUSTER_STARTED=0
  ensure_cluster
  local coll dead_pid last_replica_index
  coll="$(collection partial)"

  # Kill only the last replica so fan-out writes the first backend, then fails.
  # Without draining that backend's response, the next GET would read the stale
  # 201 Created off the keep-alive socket.
  last_replica_index=$((${#REPLICA_PIDS[@]} - 1))
  dead_pid="${REPLICA_PIDS[${last_replica_index}]}"
  if kill -0 "${dead_pid}" 2>/dev/null; then
    kill "${dead_pid}" 2>/dev/null || true
    wait "${dead_pid}" 2>/dev/null || true
  fi
  unset "REPLICA_PIDS[${last_replica_index}]"

  run_yarsh_proxy "$(cat <<EOF
POST /${coll}
{"name":"stale-trap"}
EXIT
EOF
)" || true
  assert_contains " 201 " "partial_fanout_succeeds"

  run_yarsh_proxy "$(cat <<EOF
GET /
EXIT
EOF
)" || true
  assert_contains " 200 " "surviving_backend_get_ok"
  if [[ "${LAST_OUTPUT}" == *" 201 "* ]]; then
    fail "stale_post_response_on_get"
  fi
  assert_not_contains '"name" : "stale-trap"' "get_not_stale_post_body"

  # Fresh cluster for later cases that expect a full replica set.
  stop_cluster
  CLUSTER_STARTED=0
  end_case partial_backend_drain
}

test_empty_backends_502() {
  should_run empty_backends_502 || return 0
  begin_case empty_backends_502
  ensure_cluster

  # Drop every backend while yarproxy keeps running. Dead fan-out must return
  # 502 (not echo the client request). A follow-up request after remove_if
  # empties the replica list must not UB on rotate(++begin(empty)).
  local pid
  for pid in "${REPLICA_PIDS[@]}"; do
    if kill -0 "${pid}" 2>/dev/null; then
      kill "${pid}" 2>/dev/null || true
      wait "${pid}" 2>/dev/null || true
    fi
  done
  REPLICA_PIDS=()

  run_yarsh_proxy "$(cat <<EOF
GET /
EXIT
EOF
)" || true
  assert_contains " 502 " "dead_backends_bad_gateway"

  if ! kill -0 "${YARPROXY_PID}" 2>/dev/null; then
    fail "yarproxy exited after dead-backend 502"
    end_case empty_backends_502
    return 0
  fi

  # Second request: replica list may now be empty after remove_if.
  LAST_OUTPUT="$(printf '%s\n' "GET /
EXIT" | run_with_timeout 15 "${YARSH_BIN}" "${PROXY_URL}" 2>&1)" || true

  if ! kill -0 "${YARPROXY_PID}" 2>/dev/null; then
    fail "yarproxy crashed on empty-replica request"
    end_case empty_backends_502
    return 0
  fi

  assert_contains " 502 " "empty_backends_bad_gateway"
  end_case empty_backends_502
}

test_head_no_hang() {
  should_run head_no_hang || return 0
  begin_case head_no_hang
  ensure_cluster
  local coll status
  coll="$(collection head)"

  run_yarsh_proxy "$(cat <<EOF
POST /${coll}
{"name":"head-via-proxy"}
EXIT
EOF
)"
  assert_contains " 201 " "seed_for_head"

  # yardb HEAD responses include Content-Length for the would-be body but send
  # no octets. Pre-fix yarproxy waited for those bytes on the keep-alive
  # backend socket while holding the replica mutex (proxy-wide hang).
  status=0
  LAST_OUTPUT="$(printf '%s\n' "HEAD /${coll}/1
EXIT" | run_with_timeout 10 "${YARSH_BIN}" "${PROXY_URL}" 2>&1)" || status=$?
  if [[ "${status}" -ne 0 ]]; then
    fail "HEAD through yarproxy timed out or failed (exit=${status})"
    end_case head_no_hang
    return 0
  fi
  assert_contains " 200 " "head_status_ok"
  assert_contains "  Content-Length: " "head_content_length"
  assert_not_contains "Response Body:" "head_no_body"

  # Mutex must be released: a follow-up GET on a fresh client must succeed.
  run_yarsh_proxy "$(cat <<EOF
GET /${coll}/1
EXIT
EOF
)"
  assert_contains " 200 " "get_after_head_ok"
  assert_contains '"name" : "head-via-proxy"' "get_after_head_body"
  end_case head_no_hang
}

test_sse_no_poison() {
  should_run sse_no_poison || return 0
  begin_case sse_no_poison
  ensure_cluster
  local coll status sse_out
  coll="$(collection sse)"

  run_yarsh_proxy "$(cat <<EOF
POST /${coll}
{"name":"sse-via-proxy"}
EXIT
EOF
)"
  assert_contains " 201 " "seed_for_sse"

  # yardb enables native MCP by default: GET /sse returns text/event-stream with
  # Connection: keep-alive and no Content-Length, then streams events. Pre-fix
  # yarproxy treated that as a zero-length body, returned the backend to the
  # pool with unread SSE bytes, and the next GET hung on the status line while
  # holding the replica mutex.
  status=0
  sse_out="$(run_with_timeout 5 curl -sS -D - -o /dev/null \
    -H 'Accept: text/event-stream' \
    "${PROXY_URL}/sse" 2>&1)" || status=$?
  LAST_OUTPUT="${sse_out}"
  # Proxy must not hang under the mutex: either 502 (unframed rejected) or a
  # finite response. A hung proxy makes the follow-up GET time out.
  if [[ "${status}" -eq 124 ]]; then
    fail "GET /sse through yarproxy timed out (likely held replica mutex)"
    end_case sse_no_poison
    return 0
  fi

  status=0
  LAST_OUTPUT="$(printf '%s\n' "GET /${coll}/1
EXIT" | run_with_timeout 10 "${YARSH_BIN}" "${PROXY_URL}" 2>&1)" || status=$?
  if [[ "${status}" -ne 0 ]]; then
    fail "GET after /sse timed out or failed (exit=${status}) — backend pool poisoned"
    end_case sse_no_poison
    return 0
  fi
  assert_contains " 200 " "get_after_sse_ok"
  assert_contains '"name" : "sse-via-proxy"' "get_after_sse_body"
  end_case sse_no_poison
}

test_truncated_body_no_hang() {
  should_run truncated_body_no_hang || return 0
  begin_case truncated_body_no_hang
  ensure_cluster
  local coll status truncated_out
  coll="$(collection trunc)"

  run_yarsh_proxy "$(cat <<EOF
POST /${coll}
{"name":"trunc-seed"}
EXIT
EOF
)"
  assert_contains " 201 " "seed_for_trunc"

  # Client advertises Content-Length larger than the body then half-closes.
  # Pre-fix yarproxy forwarded the short request; backends blocked waiting for
  # the missing octets while the handler held the replica mutex (proxy-wide hang).
  status=0
  truncated_out="$(
    PROXY_URL="${PROXY_URL}" COLL="${coll}" run_with_timeout 5 python3 - <<'PY' 2>&1
import os, socket, urllib.parse
url = urllib.parse.urlparse(os.environ["PROXY_URL"])
coll = os.environ["COLL"]
body = b'{"name":"short"}'
req = (
    f"POST /{coll} HTTP/1.1\r\n"
    f"Host: {url.hostname}\r\n"
    f"Content-Type: application/json\r\n"
    f"Content-Length: {len(body) + 50}\r\n"
    f"\r\n"
).encode() + body
with socket.create_connection((url.hostname, url.port), timeout=3) as sock:
    sock.sendall(req)
    sock.shutdown(socket.SHUT_WR)
    sock.settimeout(3)
    chunks = []
    try:
        while True:
            data = sock.recv(4096)
            if not data:
                break
            chunks.append(data)
    except TimeoutError:
        pass
    print(b"".join(chunks).decode("utf-8", "replace"))
PY
  )" || status=$?
  LAST_OUTPUT="${truncated_out}"
  if [[ "${status}" -eq 124 ]]; then
    fail "truncated body request timed out (likely held replica mutex waiting on backends)"
    end_case truncated_body_no_hang
    return 0
  fi
  assert_contains " 400 " "truncated_body_bad_request"

  # Mutex must be released: a follow-up GET on a fresh client must succeed.
  status=0
  LAST_OUTPUT="$(printf '%s\n' "GET /${coll}/1
EXIT" | run_with_timeout 10 "${YARSH_BIN}" "${PROXY_URL}" 2>&1)" || status=$?
  if [[ "${status}" -ne 0 ]]; then
    fail "GET after truncated body timed out or failed (exit=${status})"
    end_case truncated_body_no_hang
    return 0
  fi
  assert_contains " 200 " "get_after_trunc_ok"
  assert_contains '"name" : "trunc-seed"' "get_after_trunc_body"
  end_case truncated_body_no_hang
}

test_truncated_backend_no_poison() {
  should_run truncated_backend_no_poison || return 0
  begin_case truncated_backend_no_poison
  # Custom topology: a backend that advertises a large Content-Length then
  # closes after one body byte, plus one healthy yardb. Pre-fix GET retries
  # clobbered the shared request buffer with that partial response and forwarded
  # it to the healthy replica (which then blocked on the forged Content-Length
  # while yarproxy held the replica mutex).
  stop_cluster
  CLUSTER_STARTED=0
  REPLICA_PAT=""
  start_replica

  local coll mock_port status yardb_url
  coll="$(collection truncbe)"
  yardb_url="${REPLICA_URLS[0]}"
  run_yarsh_at "${yardb_url}" "$(cat <<EOF
POST /${coll}
{"name":"trunc-backend-seed"}
EXIT
EOF
)"
  assert_contains " 201 " "seed_healthy_replica"

  mock_port="$(pick_port)"
  MOCK_BACKEND_LOG="$(mktemp "${TMPDIR:-/tmp}/yarproxy_trunc_backend.XXXXXX").log"
  python3 - "${mock_port}" >>"${MOCK_BACKEND_LOG}" 2>&1 <<'PY' &
import socket, sys
port = int(sys.argv[1])
sock = socket.socket()
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
sock.bind(("127.0.0.1", port))
sock.listen(8)
while True:
    conn, _ = sock.accept()
    try:
        conn.settimeout(2)
        data = b""
        while b"\r\n\r\n" not in data:
            chunk = conn.recv(4096)
            if not chunk:
                break
            data += chunk
        # Truncated framed body: claim 1MiB, send one byte, close.
        conn.sendall(
            b"HTTP/1.1 200 OK\r\n"
            b"Content-Length: 1048576\r\n"
            b"\r\n"
            b"x"
        )
    finally:
        conn.close()
PY
  MOCK_BACKEND_PID=$!
  # yarproxy connect()s every replica at startup; wait until the mock listens.
  for _ in $(seq 1 50); do
    if python3 - "${mock_port}" <<'PY'
import socket, sys
s = socket.socket()
try:
    s.settimeout(0.2)
    s.connect(("127.0.0.1", int(sys.argv[1])))
except OSError:
    raise SystemExit(1)
finally:
    s.close()
PY
    then
      break
    fi
    sleep 0.05
  done

  REPLICA_URLS=("http://127.0.0.1:${mock_port}" "${yardb_url}")
  start_yarproxy
  CLUSTER_STARTED=1

  status=0
  LAST_OUTPUT="$(printf '%s\n' "GET /${coll}/1
EXIT" | run_with_timeout 10 "${YARSH_BIN}" "${PROXY_URL}" 2>&1)" || status=$?
  if [[ "${status}" -ne 0 ]]; then
    fail "GET via truncated backend timed out or failed (exit=${status})"
    end_case truncated_backend_no_poison
    return 0
  fi
  assert_contains " 200 " "get_retries_healthy_backend"
  assert_contains '"name" : "trunc-backend-seed"' "get_retries_healthy_body"

  # Healthy replica must remain usable: a follow-up GET on a fresh client.
  status=0
  LAST_OUTPUT="$(printf '%s\n' "GET /${coll}/1
EXIT" | run_with_timeout 10 "${YARSH_BIN}" "${PROXY_URL}" 2>&1)" || status=$?
  if [[ "${status}" -ne 0 ]]; then
    fail "GET after truncated backend timed out (exit=${status}) — replica pool poisoned"
    end_case truncated_backend_no_poison
    return 0
  fi
  assert_contains " 200 " "get_after_trunc_backend_ok"
  assert_contains '"name" : "trunc-backend-seed"' "get_after_trunc_backend_body"
  end_case truncated_backend_no_poison
}

test_transfer_encoding_no_smuggle() {
  should_run transfer_encoding_no_smuggle || return 0
  begin_case transfer_encoding_no_smuggle
  ensure_cluster
  local coll status te_out
  coll="$(collection tesmuggle)"

  run_yarsh_proxy "$(cat <<EOF
POST /${coll}
{"name":"te-seed"}
EXIT
EOF
)"
  assert_contains " 201 " "seed_for_te"

  # TE + Content-Length: 0 leaves the following bytes unread when TE is ignored.
  # Pre-fix yarproxy treated that leftover as a second request and fan-out
  # DELETE'd the seeded document on every replica.
  status=0
  te_out="$(
    PROXY_URL="${PROXY_URL}" COLL="${coll}" run_with_timeout 5 python3 - <<'PY' 2>&1
import os, socket, urllib.parse
url = urllib.parse.urlparse(os.environ["PROXY_URL"])
coll = os.environ["COLL"]
smuggled = (
    f"DELETE /{coll}/1 HTTP/1.1\r\n"
    f"Host: {url.hostname}\r\n"
    f"Content-Length: 0\r\n"
    f"\r\n"
).encode()
req = (
    f"POST /{coll} HTTP/1.1\r\n"
    f"Host: {url.hostname}\r\n"
    f"Content-Type: application/json\r\n"
    f"Content-Length: 0\r\n"
    f"Transfer-Encoding: chunked\r\n"
    f"\r\n"
).encode() + smuggled
with socket.create_connection((url.hostname, url.port), timeout=3) as sock:
    sock.sendall(req)
    sock.shutdown(socket.SHUT_WR)
    sock.settimeout(3)
    chunks = []
    try:
        while True:
            data = sock.recv(4096)
            if not data:
                break
            chunks.append(data)
    except TimeoutError:
        pass
    print(b"".join(chunks).decode("utf-8", "replace"))
PY
  )" || status=$?
  LAST_OUTPUT="${te_out}"
  if [[ "${status}" -eq 124 ]]; then
    fail "transfer-encoding request timed out"
    end_case transfer_encoding_no_smuggle
    return 0
  fi
  assert_contains " 400 " "transfer_encoding_bad_request"

  # Seeded document must still exist — smuggled DELETE must not have run.
  status=0
  LAST_OUTPUT="$(printf '%s\n' "GET /${coll}/1
EXIT" | run_with_timeout 10 "${YARSH_BIN}" "${PROXY_URL}" 2>&1)" || status=$?
  if [[ "${status}" -ne 0 ]]; then
    fail "GET after transfer-encoding timed out or failed (exit=${status})"
    end_case transfer_encoding_no_smuggle
    return 0
  fi
  assert_contains " 200 " "get_after_te_ok"
  assert_contains '"name" : "te-seed"' "get_after_te_body"
  end_case transfer_encoding_no_smuggle
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
  test_head_no_hang
  test_sse_no_poison
  test_truncated_body_no_hang
  test_truncated_backend_no_poison
  test_transfer_encoding_no_smuggle
  # Auth cases restart the cluster with --pat; empty_backends_502 kills replicas.
  test_header_forward_auth
  test_header_forward_correlation
  test_partial_backend_drain
  test_empty_backends_502

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