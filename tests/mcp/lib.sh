# Shared helpers for yardb MCP smoke tests.

set -euo pipefail

MCP_LIB_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=../yarsh/lib.sh
source "${MCP_LIB_DIR}/../yarsh/lib.sh"

MCP_PY="${MCP_PY:-${YARSH_ROOT_DIR}/tools/yardb_mcp.py}"
MCP_SSE_PY="${MCP_SSE_PY:-${YARSH_ROOT_DIR}/tools/yardb_mcp_sse.py}"
# Optional venv/interpreter with mcp+uvicorn+starlette (SSE smoke skips if unset/unusable).
MCP_SSE_PYTHON="${MCP_SSE_PYTHON:-}"

require_mcp_bins() {
  if [[ ! -x "${YARDB_BIN}" ]]; then
    log "yardb not found at ${YARDB_BIN} — run ./tools/CB.sh debug build first"
    exit 1
  fi
  if [[ ! -f "${MCP_PY}" ]]; then
    log "MCP bridge not found at ${MCP_PY}"
    exit 1
  fi
  if [[ ! -f "${MCP_SSE_PY}" ]]; then
    log "MCP SSE bridge not found at ${MCP_SSE_PY}"
    exit 1
  fi
  if ! command -v python3 >/dev/null 2>&1; then
    log "python3 is required for MCP smoke tests"
    exit 1
  fi
  if ! command -v curl >/dev/null 2>&1; then
    log "curl is required for readiness checks"
    exit 1
  fi
}

mcp_sse_python() {
  if [[ -n "${MCP_SSE_PYTHON}" ]]; then
    printf '%s\n' "${MCP_SSE_PYTHON}"
    return 0
  fi
  if [[ -x "${YARSH_ROOT_DIR}/tools/.venv-mcp-sse/bin/python" ]]; then
    printf '%s\n' "${YARSH_ROOT_DIR}/tools/.venv-mcp-sse/bin/python"
    return 0
  fi
  printf '%s\n' "python3"
}

# Returns 0 if SSE deps (mcp, uvicorn, starlette) import cleanly.
mcp_sse_deps_available() {
  local py
  py="$(mcp_sse_python)"
  "${py}" - <<'PY' >/dev/null 2>&1
import starlette
import mcp.server.sse
import uvicorn
PY
}

# Run one MCP case against the live yardb. Case logic lives in the Python driver.
# Emits smoke_assert_* JSONL lines on stdout when JSONL_MODE=1.
run_mcp_case() {
  local case_name=$1
  JSONL_MODE="${JSONL_MODE}" python3 - "${case_name}" "${YARDB_URL}" "${MCP_PY}" <<'PY'
from __future__ import annotations

import json
import os
import subprocess
import sys

CASE, BASE, MCP_PY = sys.argv[1], sys.argv[2], sys.argv[3]
JSONL = os.environ.get("JSONL_MODE", "0") == "1"
failures = 0
checks = 0


def emit(obj: dict) -> None:
    if JSONL:
        print(json.dumps(obj, separators=(",", ":")), flush=True)


def log(msg: str) -> None:
    print(msg, file=sys.stderr, flush=True)


def ok(cond: bool, label: str, detail: str = "") -> None:
    global checks, failures
    checks += 1
    if cond:
        emit({"type": "smoke_assert_passed", "matcher": label})
        return
    failures += 1
    emit({"type": "smoke_assert_failed", "matcher": label, "message": detail or label})
    log(f"FAIL: {label}" + (f" — {detail}" if detail else ""))


def frame(obj: dict) -> bytes:
    data = json.dumps(obj).encode()
    return f"Content-Length: {len(data)}\r\n\r\n".encode() + data


def read_one(buf):
    length = None
    while True:
        line = b""
        while not line.endswith(b"\n"):
            ch = buf.read(1)
            if not ch:
                return None
            line += ch
        if line in (b"\r\n", b"\n"):
            break
        if line.lower().startswith(b"content-length:"):
            length = int(line.split(b":", 1)[1])
    if length is None:
        return None
    return json.loads(buf.read(length))


def first_doc(body):
    if isinstance(body, list) and body:
        return body[0]
    if isinstance(body, dict):
        return body
    return {}


class Mcp:
    def __init__(self):
        self.proc = subprocess.Popen(
            [sys.executable, MCP_PY],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            env={**os.environ, "YARDB_URL": BASE, "YARDB_PAT": os.environ.get("YARDB_PAT", "")},
        )
        self._id = 0
        self.call(
            {
                "jsonrpc": "2.0",
                "id": self._next_id(),
                "method": "initialize",
                "params": {
                    "protocolVersion": "2024-11-05",
                    "capabilities": {},
                    "clientInfo": {"name": "yardb-mcp-smoke", "version": "0"},
                },
            }
        )
        self.notify({"jsonrpc": "2.0", "method": "notifications/initialized"})

    def _next_id(self) -> int:
        self._id += 1
        return self._id

    def notify(self, msg: dict) -> None:
        assert self.proc.stdin is not None
        self.proc.stdin.write(frame(msg))
        self.proc.stdin.flush()

    def call(self, msg: dict) -> dict:
        assert self.proc.stdin is not None and self.proc.stdout is not None
        self.proc.stdin.write(frame(msg))
        self.proc.stdin.flush()
        reply = read_one(self.proc.stdout)
        if reply is None:
            raise RuntimeError("MCP server closed stdout")
        return reply

    def tool(self, name: str, arguments: dict | None = None) -> dict:
        reply = self.call(
            {
                "jsonrpc": "2.0",
                "id": self._next_id(),
                "method": "tools/call",
                "params": {"name": name, "arguments": arguments or {}},
            }
        )
        result = reply.get("result") or {}
        if result.get("isError"):
            text = (result.get("content") or [{}])[0].get("text", "tool error")
            return {"status": 0, "error": text, "body": None}
        text = (result.get("content") or [{}])[0].get("text", "{}")
        return json.loads(text)

    def close(self) -> None:
        if self.proc.stdin:
            self.proc.stdin.close()
        try:
            self.proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.wait(timeout=3)


def case_tools_list(mcp: Mcp) -> None:
    reply = mcp.call({"jsonrpc": "2.0", "id": mcp._next_id(), "method": "tools/list"})
    tools = (reply.get("result") or {}).get("tools") or []
    names = {t.get("name") for t in tools}
    expected = {
        "health",
        "ready",
        "list_collections",
        "metadata",
        "query_collection",
        "get_document",
        "create_document",
        "replace_document",
        "update_document",
        "delete_document",
        "reindex",
        "configure_indexes",
    }
    ok(len(tools) == 12, "tools_count_12", f"got {len(tools)}")
    ok(names == expected, "tools_names", f"got {sorted(names)}")


def case_probes(mcp: Mcp) -> None:
    ok(mcp.tool("health").get("status") == 200, "health_200")
    ok(mcp.tool("ready").get("status") == 200, "ready_200")


def case_crud(mcp: Mcp) -> None:
    coll = f"mcp{os.getpid()}crud"
    created = mcp.tool("create_document", {"collection": coll, "document": {"name": "Ada", "age": 36}})
    ok(created.get("status") == 201, "create_201", str(created.get("status")))
    doc_id = str(first_doc(created.get("body")).get("_id", ""))
    ok(bool(doc_id), "create_has_id")

    got = mcp.tool("get_document", {"collection": coll, "document_id": doc_id})
    ok(got.get("status") == 200 and first_doc(got.get("body")).get("name") == "Ada", "get_ada")

    patched = mcp.tool(
        "update_document",
        {"collection": coll, "document_id": doc_id, "patch": {"age": 37}},
    )
    ok(patched.get("status") == 200 and first_doc(patched.get("body")).get("age") == 37, "patch_age")

    replaced = mcp.tool(
        "replace_document",
        {"collection": coll, "document_id": doc_id, "document": {"name": "Ada", "age": 38}},
    )
    ok(replaced.get("status") == 200 and first_doc(replaced.get("body")).get("age") == 38, "replace_age")

    as_string = mcp.tool(
        "create_document",
        {"collection": coll, "document": json.dumps({"name": "Grace", "age": 40})},
    )
    ok(as_string.get("status") == 201, "create_json_string")

    deleted = mcp.tool("delete_document", {"collection": coll, "document_id": doc_id})
    ok(deleted.get("status") == 204, "delete_204")
    missing = mcp.tool("get_document", {"collection": coll, "document_id": doc_id})
    ok(missing.get("status") == 404, "get_404")


def case_filter(mcp: Mcp) -> None:
    coll = f"mcp{os.getpid()}filter"
    mcp.tool("create_document", {"collection": coll, "document": {"name": "Ada", "age": 36}})
    mcp.tool("create_document", {"collection": coll, "document": {"name": "Bob", "age": 20}})
    q = mcp.tool(
        "query_collection",
        {"collection": coll, "odata_query": "$filter=age gt 30&$top=10"},
    )
    body = q.get("body")
    rows = body if isinstance(body, list) else (body or {}).get("value", [])
    ok(q.get("status") == 200, "filter_200")
    ok(any(d.get("name") == "Ada" for d in rows), "filter_includes_ada")
    ok(not any(d.get("name") == "Bob" for d in rows), "filter_excludes_bob")


def case_indexes(mcp: Mcp) -> None:
    coll = f"mcp{os.getpid()}idx"
    mcp.tool("create_document", {"collection": coll, "document": {"name": "Ada"}})
    idx = mcp.tool("configure_indexes", {"collection": coll, "keys": ["name"]})
    ok(idx.get("status") in (200, 201, 204), "configure_indexes", str(idx.get("status")))
    ok(mcp.tool("metadata").get("status") == 200, "metadata_200")
    ok(mcp.tool("list_collections").get("status") == 200, "list_collections_200")
    reindexed = mcp.tool("reindex")
    ok(reindexed.get("status") in (200, 204), "reindex", str(reindexed.get("status")))


CASES = {
    "tools_list": case_tools_list,
    "probes": case_probes,
    "crud": case_crud,
    "filter": case_filter,
    "indexes": case_indexes,
}

if CASE not in CASES:
    log(f"unknown mcp case: {CASE}")
    sys.exit(2)

mcp = Mcp()
try:
    CASES[CASE](mcp)
finally:
    mcp.close()

# Communicate check count to the shell via a final marker line on stderr-safe channel:
# print counts as a single JSON object on the last stdout line always (bash parses it).
print(json.dumps({"type": "smoke_case_stats", "name": CASE, "checks": checks, "failures": failures}), flush=True)
sys.exit(1 if failures else 0)
PY
}

# Optional SSE smoke: starts yardb_mcp_sse.py and exercises tools/list + health via MCP SSE client.
# Emits smoke_assert_* / smoke_case_stats like run_mcp_case. Skips (exit 0, checks=0, skipped=1) if deps missing.
run_mcp_sse_case() {
  local py
  py="$(mcp_sse_python)"
  JSONL_MODE="${JSONL_MODE}" \
  YARDB_URL="${YARDB_URL}" \
  YARDB_PAT="${YARDB_PAT:-}" \
    "${py}" - "${MCP_SSE_PY}" <<'PY'
from __future__ import annotations

import asyncio
import json
import os
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request

MCP_SSE_PY = sys.argv[1]
BASE = (os.environ.get("YARDB_URL") or "http://127.0.0.1:2112").rstrip("/")
JSONL = os.environ.get("JSONL_MODE", "0") == "1"
failures = 0
checks = 0


def emit(obj: dict) -> None:
    if JSONL:
        print(json.dumps(obj, separators=(",", ":")), flush=True)


def log(msg: str) -> None:
    print(msg, file=sys.stderr, flush=True)


def ok(cond: bool, label: str, detail: str = "") -> None:
    global checks, failures
    checks += 1
    if cond:
        emit({"type": "smoke_assert_passed", "matcher": label})
        return
    failures += 1
    emit({"type": "smoke_assert_failed", "matcher": label, "message": detail or label})
    log(f"FAIL: {label}" + (f" — {detail}" if detail else ""))


def pick_port() -> int:
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


try:
    import starlette  # noqa: F401
    import uvicorn  # noqa: F401
    from mcp import ClientSession
    from mcp.client.sse import sse_client
except ImportError as exc:
    log(f"SSE deps missing ({exc}); skip sse case — pip install -r tools/requirements-mcp-sse.txt")
    print(
        json.dumps(
            {
                "type": "smoke_case_stats",
                "name": "sse",
                "checks": 0,
                "failures": 0,
                "skipped": 1,
            }
        ),
        flush=True,
    )
    sys.exit(0)

port = pick_port()
env = {
    **os.environ,
    "YARDB_URL": BASE,
    "YARDB_PAT": os.environ.get("YARDB_PAT", ""),
    "YARDB_MCP_SSE_HOST": "127.0.0.1",
    "YARDB_MCP_SSE_PORT": str(port),
}
# Avoid PIPE deadlock on uvicorn logs — keep stderr in a temp file.
err_path = os.environ.get("TMPDIR") or "/tmp"
err_file = open(os.path.join(err_path, f"yardb_mcp_sse_smoke_{os.getpid()}.err"), "w")
proc = subprocess.Popen(
    [sys.executable, MCP_SSE_PY],
    env=env,
    stdout=subprocess.DEVNULL,
    stderr=err_file,
)

ready = False
for _ in range(50):
    if proc.poll() is not None:
        err_file.flush()
        try:
            with open(err_file.name, encoding="utf-8", errors="replace") as f:
                log(f"SSE server exited early:\n{f.read()}")
        except OSError:
            log("SSE server exited early")
        break
    try:
        with urllib.request.urlopen(f"http://127.0.0.1:{port}/health", timeout=0.5) as resp:
            if resp.status == 200:
                ready = True
                break
    except Exception:
        time.sleep(0.1)

if not ready:
    proc.kill()
    proc.wait(timeout=3)
    err_file.close()
    try:
        os.unlink(err_file.name)
    except OSError:
        pass
    ok(False, "sse_server_ready")
    print(
        json.dumps({"type": "smoke_case_stats", "name": "sse", "checks": checks, "failures": failures}),
        flush=True,
    )
    sys.exit(1)

# DNS-rebinding protection: cross-origin browser clients must not drive the PAT proxy.
try:
    req = urllib.request.Request(
        f"http://127.0.0.1:{port}/sse",
        headers={"Origin": "http://evil.example", "Accept": "text/event-stream"},
    )
    with urllib.request.urlopen(req, timeout=2) as resp:
        ok(False, "sse_rejects_evil_origin", f"status={resp.status}")
except urllib.error.HTTPError as exc:
    ok(exc.code in (403, 421), "sse_rejects_evil_origin", f"status={exc.code}")
except Exception as exc:  # noqa: BLE001
    ok(False, "sse_rejects_evil_origin", str(exc))

# Wildcard binds publish an unauthenticated CRUD proxy — refuse like yardb's public-bind gate.
wild = subprocess.run(
    [sys.executable, MCP_SSE_PY],
    env={**env, "YARDB_MCP_SSE_HOST": "0.0.0.0", "YARDB_MCP_SSE_PORT": str(pick_port())},
    capture_output=True,
    text=True,
    timeout=5,
)
ok(wild.returncode != 0, "sse_refuses_wildcard_bind", f"exit={wild.returncode}")
ok(
    "refusing to bind MCP SSE" in (wild.stderr or "") + (wild.stdout or ""),
    "sse_wildcard_bind_message",
)


async def exercise() -> None:
    url = f"http://127.0.0.1:{port}/sse"
    async with sse_client(url) as (read, write):
        async with ClientSession(read, write) as session:
            await session.initialize()
            listed = await session.list_tools()
            names = {t.name for t in listed.tools}
            ok(len(listed.tools) == 12, "sse_tools_count_12", f"got {len(listed.tools)}")
            ok("health" in names and "query_collection" in names, "sse_tools_core")
            result = await session.call_tool("health", {})
            text = ""
            for block in result.content or []:
                text += getattr(block, "text", "") or ""
            payload = json.loads(text) if text.strip().startswith("{") else {}
            ok(payload.get("status") == 200, "sse_health_200", text[:200])


try:
    asyncio.run(asyncio.wait_for(exercise(), timeout=20))
except Exception as exc:  # noqa: BLE001
    ok(False, "sse_client_session", str(exc))
finally:
    proc.terminate()
    try:
        proc.wait(timeout=3)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=3)
    err_file.close()
    try:
        os.unlink(err_file.name)
    except OSError:
        pass

print(
    json.dumps({"type": "smoke_case_stats", "name": "sse", "checks": checks, "failures": failures}),
    flush=True,
)
sys.exit(1 if failures else 0)
PY
}
