# Shared helpers for yardb MCP smoke tests.

set -euo pipefail

MCP_LIB_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=../yarsh/lib.sh
source "${MCP_LIB_DIR}/../yarsh/lib.sh"

MCP_PY="${MCP_PY:-${YARSH_ROOT_DIR}/tools/yardb_mcp.py}"

require_mcp_bins() {
  if [[ ! -x "${YARDB_BIN}" ]]; then
    log "yardb not found at ${YARDB_BIN} — run ./tools/CB.sh debug build first"
    exit 1
  fi
  if [[ ! -f "${MCP_PY}" ]]; then
    log "MCP bridge not found at ${MCP_PY}"
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
