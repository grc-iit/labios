#!/usr/bin/env python3
"""Compose MCP golden path using only MCP tool calls and public SDK-backed APIs."""
from __future__ import annotations

import anyio
import base64
from datetime import timedelta
import json
import os
import sys
import uuid

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client


async def run() -> None:
    server = StdioServerParameters(
        command=sys.executable, args=["-m", "labios_mcp"], env=dict(os.environ))
    async with stdio_client(server) as (read_stream, write_stream):
        async with ClientSession(read_stream, write_stream) as session:
            await session.initialize()

            async def call(name: str, arguments: dict) -> dict:
                result = await session.call_tool(
                    name, arguments, read_timeout_seconds=timedelta(seconds=45))
                if not result.content:
                    raise RuntimeError(f"{name} returned no content")
                payload = json.loads(result.content[0].text)
                if not payload.get("ok"):
                    raise RuntimeError(f"{name} failed: {payload}")
                return payload

            token = uuid.uuid4().hex
            source = f"file:///mcp-golden/{token}.source"
            destination = f"sqlite:///mcp-golden/{token}.pipeline"
            expected = b"LABIOS MCP Label I/O\x00exact\xffbytes\n"
            encoded = base64.b64encode(expected).decode("ascii")

            stored = await call("labios_store", {
                "destination": source,
                "data": encoded,
                "encoding": "base64",
                "intent": "tool_output",
                "priority": 211,
            })
            retrieved = await call("labios_retrieve", {
                "source": source, "size": len(expected), "encoding": "base64"})
            assert base64.b64decode(retrieved["data"]) == expected

            processed = await call("labios_process", {
                "source": source,
                "destination": destination,
                "pipeline": [{"operation": "builtin://identity"}],
                "intent": "intermediate",
                "priority": 177,
            })
            transformed = await call("labios_retrieve", {
                "source": destination, "size": len(expected), "encoding": "base64"})
            assert base64.b64decode(transformed["data"]) == expected

            lifecycle = await call("labios_observe", {
                "query": "label/status", "label_id": processed["label_id"]})
            assert lifecycle["completion"]["lifecycle"] == "completed"
            inspected = await call("labios_observe", {
                "query": "label/inspect", "label_id": processed["label_id"]})
            label = inspected["label"]
            assert label["ir_version"] == 1
            assert label["source"]["family"] == "file_range"
            assert label["destination"]["family"] == "relational"
            assert label["pipeline"][0]["operation"] == "builtin://identity"
            assert label["placement_history"]

            for query in ("system/health", "workers/count", "config/current"):
                await call("labios_observe", {"query": query})

            print(
                "LABIOS MCP golden: verified "
                f"{len(expected)} exact store/retrieve and pipeline bytes; "
                f"labels={stored['label_id']},{processed['label_id']} ir_version=1"
            )


if __name__ == "__main__":
    anyio.run(run)
