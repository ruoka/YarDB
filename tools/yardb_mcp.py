#!/usr/bin/env python3
# Copyright (c) 2025-2026 Kaius Ruokonen. All rights reserved.
# SPDX-License-Identifier: MIT
# See the LICENSE file in the project root for full license text.
"""
MCP stdio bridge to a running yardb HTTP/OData API (stdlib only).

Environment:
  YARDB_URL   Base URL (default http://127.0.0.1:2112)
  YARDB_PAT   Optional Bearer token when yardb --pat is configured

Wire: JSON-RPC 2.0 with LSP-style Content-Length framing on stdin/stdout.

Debug only via stderr — never print to stdout (corrupts MCP framing).
"""

from __future__ import annotations

import json
import os
import sys
import urllib.error
import urllib.parse
import urllib.request
from typing import Any, Callable

BASE = (os.environ.get("YARDB_URL") or "http://127.0.0.1:2112").rstrip("/")
PAT = (os.environ.get("YARDB_PAT") or "").strip()


def _log(message: str) -> None:
    print(message, file=sys.stderr, flush=True)


def _as_json_value(value: Any) -> Any:
    """Accept a raw JSON object/array from the client, or a JSON string."""
    if isinstance(value, (dict, list)):
        return value
    if isinstance(value, str):
        return json.loads(value)
    raise TypeError(f"expected object, array, or JSON string; got {type(value).__name__}")


def _headers(*, content_type: str | None = None) -> dict[str, str]:
    headers = {"Accept": "application/json"}
    if content_type:
        headers["Content-Type"] = content_type
    if PAT:
        headers["Authorization"] = f"Bearer {PAT}"
    return headers


def _request(
    method: str,
    path: str,
    *,
    body: dict[str, Any] | list[Any] | None = None,
) -> dict[str, Any]:
    url = BASE + path
    data = None
    headers = _headers(content_type="application/json" if body is not None else None)
    if body is not None:
        data = json.dumps(body).encode("utf-8")
    req = urllib.request.Request(url, data=data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(req, timeout=60) as resp:
            raw = resp.read()
            text = raw.decode("utf-8") if raw else ""
            parsed: Any = json.loads(text) if text else None
            return {
                "status": resp.status,
                "headers": {k.lower(): v for k, v in resp.headers.items()},
                "body": parsed,
            }
    except urllib.error.HTTPError as e:
        raw = e.read()
        text = raw.decode("utf-8") if raw else ""
        try:
            parsed = json.loads(text) if text else None
        except json.JSONDecodeError:
            parsed = text
        return {
            "status": e.code,
            "headers": {k.lower(): v for k, v in e.headers.items()},
            "body": parsed,
            "error": e.reason,
        }
    except urllib.error.URLError as e:
        return {"status": 0, "error": str(e.reason)}


def _dump(result: dict[str, Any]) -> str:
    return json.dumps(result, indent=2, ensure_ascii=False)


def _text(result: dict[str, Any]) -> dict[str, Any]:
    return {"content": [{"type": "text", "text": _dump(result)}]}


def tool_health(_: dict[str, Any]) -> dict[str, Any]:
    return _text(_request("GET", "/health"))


def tool_ready(_: dict[str, Any]) -> dict[str, Any]:
    return _text(_request("GET", "/ready"))


def tool_list_collections(_: dict[str, Any]) -> dict[str, Any]:
    return _text(_request("GET", "/"))


def tool_metadata(_: dict[str, Any]) -> dict[str, Any]:
    return _text(_request("GET", "/$metadata"))


def tool_query_collection(args: dict[str, Any]) -> dict[str, Any]:
    collection = str(args["collection"])
    odata_query = str(args.get("odata_query") or "").strip().lstrip("?")
    path = f"/{urllib.parse.quote(collection, safe='')}"
    if odata_query:
        # Encode spaces etc.; keep OData delimiters and common filter punctuation.
        path += "?" + urllib.parse.quote(odata_query, safe="=&$(),'/")
    return _text(_request("GET", path))


def tool_get_document(args: dict[str, Any]) -> dict[str, Any]:
    path = (
        f"/{urllib.parse.quote(str(args['collection']), safe='')}/"
        f"{urllib.parse.quote(str(args['document_id']), safe='')}"
    )
    return _text(_request("GET", path))


def tool_create_document(args: dict[str, Any]) -> dict[str, Any]:
    body = _as_json_value(args["document"])
    if not isinstance(body, dict):
        raise TypeError("document must be a JSON object")
    path = f"/{urllib.parse.quote(str(args['collection']), safe='')}"
    return _text(_request("POST", path, body=body))


def tool_replace_document(args: dict[str, Any]) -> dict[str, Any]:
    body = _as_json_value(args["document"])
    if not isinstance(body, dict):
        raise TypeError("document must be a JSON object")
    path = (
        f"/{urllib.parse.quote(str(args['collection']), safe='')}/"
        f"{urllib.parse.quote(str(args['document_id']), safe='')}"
    )
    return _text(_request("PUT", path, body=body))


def tool_update_document(args: dict[str, Any]) -> dict[str, Any]:
    body = _as_json_value(args["patch"])
    if not isinstance(body, dict):
        raise TypeError("patch must be a JSON object")
    path = (
        f"/{urllib.parse.quote(str(args['collection']), safe='')}/"
        f"{urllib.parse.quote(str(args['document_id']), safe='')}"
    )
    return _text(_request("PATCH", path, body=body))


def tool_delete_document(args: dict[str, Any]) -> dict[str, Any]:
    path = (
        f"/{urllib.parse.quote(str(args['collection']), safe='')}/"
        f"{urllib.parse.quote(str(args['document_id']), safe='')}"
    )
    return _text(_request("DELETE", path))


def tool_reindex(_: dict[str, Any]) -> dict[str, Any]:
    return _text(_request("GET", "/_reindex"))


def tool_configure_indexes(args: dict[str, Any]) -> dict[str, Any]:
    body: Any = _as_json_value(args["keys"])
    if isinstance(body, list):
        body = {"keys": body}
    elif not isinstance(body, dict):
        raise TypeError("keys must be a JSON object or array")
    path = f"/_db/{urllib.parse.quote(str(args['collection']), safe='')}"
    method = "PATCH" if args.get("incremental") else "PUT"
    return _text(_request(method, path, body=body))


TOOLS: dict[str, tuple[dict[str, Any], Callable[[dict[str, Any]], dict[str, Any]]]] = {
    "health": (
        {
            "name": "health",
            "description": "GET /health — liveness probe",
            "inputSchema": {"type": "object", "properties": {}, "additionalProperties": False},
        },
        tool_health,
    ),
    "ready": (
        {
            "name": "ready",
            "description": "GET /ready — readiness probe",
            "inputSchema": {"type": "object", "properties": {}, "additionalProperties": False},
        },
        tool_ready,
    ),
    "list_collections": (
        {
            "name": "list_collections",
            "description": "GET / — list collection names",
            "inputSchema": {"type": "object", "properties": {}, "additionalProperties": False},
        },
        tool_list_collections,
    ),
    "metadata": (
        {
            "name": "metadata",
            "description": "GET /$metadata — OData 4.01 JSON CSDL",
            "inputSchema": {"type": "object", "properties": {}, "additionalProperties": False},
        },
        tool_metadata,
    ),
    "query_collection": (
        {
            "name": "query_collection",
            "description": (
                "GET /{collection} with optional OData query (no leading '?'). "
                "Examples: $top=10, $filter=age gt 25, $count=true"
            ),
            "inputSchema": {
                "type": "object",
                "properties": {
                    "collection": {"type": "string"},
                    "odata_query": {"type": "string", "default": ""},
                },
                "required": ["collection"],
                "additionalProperties": False,
            },
        },
        tool_query_collection,
    ),
    "get_document": (
        {
            "name": "get_document",
            "description": "GET /{collection}/{id}",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "collection": {"type": "string"},
                    "document_id": {"type": "string"},
                },
                "required": ["collection", "document_id"],
                "additionalProperties": False,
            },
        },
        tool_get_document,
    ),
    "create_document": (
        {
            "name": "create_document",
            "description": "POST /{collection}",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "collection": {"type": "string"},
                    "document": {
                        "type": "object",
                        "description": "JSON object to insert",
                    },
                },
                "required": ["collection", "document"],
                "additionalProperties": False,
            },
        },
        tool_create_document,
    ),
    "replace_document": (
        {
            "name": "replace_document",
            "description": "PUT /{collection}/{id} upsert",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "collection": {"type": "string"},
                    "document_id": {"type": "string"},
                    "document": {
                        "type": "object",
                        "description": "JSON object to store",
                    },
                },
                "required": ["collection", "document_id", "document"],
                "additionalProperties": False,
            },
        },
        tool_replace_document,
    ),
    "update_document": (
        {
            "name": "update_document",
            "description": "PATCH /{collection}/{id} (404 if missing)",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "collection": {"type": "string"},
                    "document_id": {"type": "string"},
                    "patch": {
                        "type": "object",
                        "description": "JSON fields to merge",
                    },
                },
                "required": ["collection", "document_id", "patch"],
                "additionalProperties": False,
            },
        },
        tool_update_document,
    ),
    "delete_document": (
        {
            "name": "delete_document",
            "description": "DELETE /{collection}/{id}",
            "inputSchema": {
                "type": "object",
                "properties": {
                    "collection": {"type": "string"},
                    "document_id": {"type": "string"},
                },
                "required": ["collection", "document_id"],
                "additionalProperties": False,
            },
        },
        tool_delete_document,
    ),
    "reindex": (
        {
            "name": "reindex",
            "description": "GET /_reindex — rebuild indexes",
            "inputSchema": {"type": "object", "properties": {}, "additionalProperties": False},
        },
        tool_reindex,
    ),
    "configure_indexes": (
        {
            "name": "configure_indexes",
            "description": (
                "PUT/PATCH /_db/{collection}; keys is {\"keys\":[\"field\"]} or [\"field\"]; "
                "incremental=true uses PATCH"
            ),
            "inputSchema": {
                "type": "object",
                "properties": {
                    "collection": {"type": "string"},
                    "keys": {
                        "description": 'Index keys as {"keys":["field"]} or ["field"]',
                    },
                    "incremental": {"type": "boolean", "default": False},
                },
                "required": ["collection", "keys"],
                "additionalProperties": False,
            },
        },
        tool_configure_indexes,
    ),
}


def _read_message() -> dict[str, Any] | None:
    headers: dict[str, str] = {}
    while True:
        line = sys.stdin.buffer.readline()
        if not line:
            return None
        if line in (b"\r\n", b"\n"):
            break
        key, _, value = line.decode("utf-8").partition(":")
        headers[key.strip().lower()] = value.strip()
    length = int(headers.get("content-length", "0"))
    if length <= 0:
        return None
    body = sys.stdin.buffer.read(length)
    if len(body) < length:
        return None
    return json.loads(body.decode("utf-8"))


def _write_message(message: dict[str, Any]) -> None:
    data = json.dumps(message, ensure_ascii=False).encode("utf-8")
    sys.stdout.buffer.write(f"Content-Length: {len(data)}\r\n\r\n".encode("ascii"))
    sys.stdout.buffer.write(data)
    sys.stdout.buffer.flush()


def _handle(msg: dict[str, Any]) -> dict[str, Any] | None:
    method = msg.get("method")
    msg_id = msg.get("id")
    params = msg.get("params") or {}

    if method == "initialize":
        return {
            "jsonrpc": "2.0",
            "id": msg_id,
            "result": {
                "protocolVersion": "2024-11-05",
                "capabilities": {"tools": {}},
                "serverInfo": {"name": "yardb", "version": "1.0.0"},
            },
        }

    if method == "notifications/initialized":
        return None

    if method == "ping":
        return {"jsonrpc": "2.0", "id": msg_id, "result": {}}

    if method == "tools/list":
        return {
            "jsonrpc": "2.0",
            "id": msg_id,
            "result": {"tools": [spec for spec, _ in TOOLS.values()]},
        }

    if method == "tools/call":
        name = params.get("name")
        args = params.get("arguments") or {}
        entry = TOOLS.get(name)
        if entry is None:
            return {
                "jsonrpc": "2.0",
                "id": msg_id,
                "error": {"code": -32601, "message": f"Unknown tool: {name}"},
            }
        try:
            result = entry[1](args)
            return {"jsonrpc": "2.0", "id": msg_id, "result": result}
        except Exception as exc:  # noqa: BLE001 — surface to MCP client
            return {
                "jsonrpc": "2.0",
                "id": msg_id,
                "result": {
                    "content": [{"type": "text", "text": f"error: {exc}"}],
                    "isError": True,
                },
            }

    if msg_id is None:
        return None

    return {
        "jsonrpc": "2.0",
        "id": msg_id,
        "error": {"code": -32601, "message": f"Method not found: {method}"},
    }


def main() -> None:
    while True:
        msg = _read_message()
        if msg is None:
            break
        reply = _handle(msg)
        if reply is not None:
            _write_message(reply)


if __name__ == "__main__":
    main()
