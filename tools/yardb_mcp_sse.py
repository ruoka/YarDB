#!/usr/bin/env python3
# Copyright (c) 2025-2026 Kaius Ruokonen. All rights reserved.
# SPDX-License-Identifier: MIT
# See the LICENSE file in the project root for full license text.
"""
MCP HTTP/SSE bridge to a running yardb HTTP/OData API.

Requires: pip install -r tools/requirements-mcp-sse.txt

Environment:
  YARDB_URL              Base yardb URL (default http://127.0.0.1:2112)
  YARDB_PAT              Optional Bearer token
  YARDB_MCP_SSE_HOST     Bind host (default 127.0.0.1; any-address aliases refused)
  YARDB_MCP_SSE_PORT     Bind port (default 8000)

Endpoints:
  GET  /sse        — client opens the SSE stream
  POST /messages/  — client posts JSON-RPC (session endpoint from SSE)
  GET  /health     — bridge liveness (not MCP)

Cursor / client config should point at the SSE URL, e.g.:
  http://127.0.0.1:8000/sse
"""

from __future__ import annotations

import os
import socket
import sys
from pathlib import Path

# Allow `import yardb_mcp` when launched as tools/yardb_mcp_sse.py
sys.path.insert(0, str(Path(__file__).resolve().parent))

from mcp.server import Server
from mcp.server.sse import SseServerTransport
from mcp.server.transport_security import TransportSecuritySettings
from mcp.types import TextContent, Tool
from starlette.applications import Starlette
from starlette.requests import Request
from starlette.responses import JSONResponse, Response
from starlette.routing import Mount, Route

import yardb_mcp

HOST = os.environ.get("YARDB_MCP_SSE_HOST") or "127.0.0.1"
PORT = int(os.environ.get("YARDB_MCP_SSE_PORT") or "8000")


def _normalize_bind_host(host: str) -> str:
    normalized = host.strip()
    if normalized.startswith("[") and normalized.endswith("]"):
        normalized = normalized[1:-1]
    return normalized


def _is_unspecified_bind_address(family: int, sockaddr: tuple) -> bool:
    """True for INADDR_ANY / in6addr_any / ::ffff:0.0.0.0 (matches net::acceptor)."""
    if family == socket.AF_INET:
        return sockaddr[0] == "0.0.0.0"
    if family == socket.AF_INET6:
        try:
            packed = socket.inet_pton(socket.AF_INET6, sockaddr[0])
        except OSError:
            return False
        if packed == bytes(16):
            return True
        # IPv4-mapped ::ffff:0.0.0.0 — Linux accepts IPv4 clients on that any.
        return packed[:12] == bytes.fromhex("00000000000000000000ffff") and packed[12:] == bytes(
            4
        )
    return False


def _is_wildcard_bind_host(host: str) -> bool:
    """
    Match yardb/net::is_wildcard_bind_host (AI_PASSIVE resolve).

    Literal string checks miss getaddrinfo aliases that still bind all
    interfaces (`0`, `0.0.0`, `::0`, `0000::`, …).
    """
    normalized = _normalize_bind_host(host)
    # Portable refuse: some platforms reject '*' even though Linux maps it to any.
    if normalized == "*":
        return True
    try:
        # Empty node + AI_PASSIVE → unspecified (same as net address_info).
        node = normalized if normalized else None
        infos = socket.getaddrinfo(
            node,
            "0",
            type=socket.SOCK_STREAM,
            flags=socket.AI_PASSIVE,
        )
    except socket.gaierror:
        return False
    return any(
        _is_unspecified_bind_address(family, sockaddr)
        for family, _type, _proto, _canon, sockaddr in infos
    )


def _is_loopback_bind_host(host: str) -> bool:
    normalized = _normalize_bind_host(host).lower()
    return normalized in {"127.0.0.1", "localhost", "::1"}


def _transport_security_settings(host: str) -> TransportSecuritySettings:
    """
    Enable MCP DNS-rebinding protection (Host/Origin checks).

    The bridge holds YARDB_PAT and exposes full CRUD over unauthenticated HTTP;
    without these checks a browser/DNS-rebinding client can drive tools as the
    bridge. Mirrors FastMCP localhost defaults, plus the configured bind host.
    """
    allowed_hosts = ["127.0.0.1:*", "localhost:*", "[::1]:*"]
    allowed_origins = [
        "http://127.0.0.1:*",
        "http://localhost:*",
        "http://[::1]:*",
    ]
    if not _is_loopback_bind_host(host):
        # Exact host + any-port form so reverse-proxy / LAN binds still work.
        bare = host.strip()
        if bare.startswith("[") and bare.endswith("]"):
            allowed_hosts.append(f"{bare}:*")
            allowed_origins.append(f"http://{bare}:*")
        else:
            allowed_hosts.append(f"{bare}:*")
            allowed_hosts.append(bare)
            allowed_origins.append(f"http://{bare}:*")
            allowed_origins.append(f"http://{bare}")
    return TransportSecuritySettings(
        enable_dns_rebinding_protection=True,
        allowed_hosts=allowed_hosts,
        allowed_origins=allowed_origins,
    )


def _validate_bind_policy(host: str) -> None:
    # Unlike yardb, this process has no client auth of its own — a wildcard bind
    # would publish a PAT-injecting CRUD proxy to every interface.
    if _is_wildcard_bind_host(host):
        raise SystemExit(
            f"refusing to bind MCP SSE to {host} without bridge authentication; "
            "use YARDB_MCP_SSE_HOST=127.0.0.1 (default), or bind a specific "
            "non-wildcard address"
        )


# Fail closed even when launched via `uvicorn yardb_mcp_sse:app` (bypasses main).
_validate_bind_policy(HOST)

mcp_server = Server("yardb")
# Relative path advertised over SSE for JSON-RPC POSTs (must match Mount below).
sse_transport = SseServerTransport(
    "/messages/",
    security_settings=_transport_security_settings(HOST),
)


@mcp_server.list_tools()
async def handle_list_tools() -> list[Tool]:
    tools: list[Tool] = []
    for spec, _ in yardb_mcp.TOOLS.values():
        tools.append(
            Tool(
                name=spec["name"],
                description=spec.get("description") or "",
                inputSchema=spec.get("inputSchema") or {"type": "object", "properties": {}},
            )
        )
    return tools


@mcp_server.call_tool()
async def handle_call_tool(name: str, arguments: dict | None) -> list[TextContent]:
    entry = yardb_mcp.TOOLS.get(name)
    if entry is None:
        raise ValueError(f"Unknown tool: {name}")
    try:
        result = entry[1](arguments or {})
    except Exception as exc:  # noqa: BLE001 — surface to MCP client
        return [TextContent(type="text", text=f"error: {exc}")]
    content = result.get("content") or []
    texts = [
        TextContent(type="text", text=str(item.get("text", "")))
        for item in content
        if isinstance(item, dict)
    ]
    return texts or [TextContent(type="text", text="{}")]


async def handle_sse(request: Request) -> Response:
    """AI client connects here to establish the Server-Sent Events stream."""
    async with sse_transport.connect_sse(
        request.scope, request.receive, request._send
    ) as (read_stream, write_stream):
        await mcp_server.run(
            read_stream,
            write_stream,
            mcp_server.create_initialization_options(),
        )
    # Required by mcp.server.sse — avoid NoneType on client disconnect.
    return Response()


async def handle_health(_: Request) -> JSONResponse:
    return JSONResponse({"status": "ok", "yardb_url": yardb_mcp.BASE})


# Mount handle_post_message as a raw ASGI app — wrapping it in a FastAPI/Starlette
# route handler double-sends the HTTP response and breaks the MCP session.
app = Starlette(
    routes=[
        Route("/sse", endpoint=handle_sse, methods=["GET"]),
        Route("/health", endpoint=handle_health, methods=["GET"]),
        Mount("/messages/", app=sse_transport.handle_post_message),
    ],
)


def main() -> None:
    import uvicorn

    _validate_bind_policy(HOST)
    yardb_mcp._log(
        f"YarDB MCP SSE on http://{HOST}:{PORT}/sse (yardb={yardb_mcp.BASE})"
    )
    uvicorn.run(app, host=HOST, port=PORT, log_level="info")


if __name__ == "__main__":
    main()
