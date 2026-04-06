#!/usr/bin/env python3
"""LABIOS MCP Server.

Exposes LABIOS runtime operations as MCP tools for coding agents.
Connects to DragonflyDB (warehouse) and reads/writes the same keys
the C++ runtime uses. Runs inside Docker Compose on the same network.
"""
import asyncio
import json
import os
import time
from typing import Any

import redis
from mcp.server import Server
from mcp.server.stdio import stdio_server
from mcp.types import TextContent, Tool

REDIS_HOST = os.environ.get("LABIOS_REDIS_HOST", "redis")
REDIS_PORT = int(os.environ.get("LABIOS_REDIS_PORT", "6379"))

app = Server("labios")
r = redis.Redis(host=REDIS_HOST, port=REDIS_PORT, decode_responses=False)


def _text(data: Any) -> list[TextContent]:
    """Wrap a response as MCP TextContent."""
    if isinstance(data, (dict, list)):
        return [TextContent(type="text", text=json.dumps(data, indent=2))]
    return [TextContent(type="text", text=str(data))]


# ── Workspace key helpers (match C++ workspace.cpp key patterns) ──────────

def _ws_data_key(scope: str, key: str) -> str:
    return f"labios:ws:{scope}:{key}"

def _ws_version_key(scope: str, key: str, version: int) -> str:
    return f"labios:ws:{scope}:{key}:v{version}"

def _ws_meta_key(scope: str, key: str) -> str:
    return f"labios:ws:{scope}:_meta:{key}"

def _ws_index_key(scope: str) -> str:
    return f"labios:ws:{scope}:_index"


# ── Tool definitions ──────────────────────────────────────────────────────

@app.list_tools()
async def list_tools() -> list[Tool]:
    return [
        Tool(
            name="labios_observe",
            description=(
                "Query LABIOS runtime state. Returns JSON with system metrics. "
                "Queries: 'queue/depth', 'workers/scores', 'workers/count', "
                "'system/health', 'channels/list', 'workspaces/list', 'config/current'."
            ),
            inputSchema={
                "type": "object",
                "properties": {
                    "query": {
                        "type": "string",
                        "description": "What to observe (e.g. 'workers/scores', 'system/health')",
                    }
                },
                "required": ["query"],
            },
        ),
        Tool(
            name="labios_store",
            description=(
                "Store data in a LABIOS workspace. Data persists across sessions. "
                "Scope controls visibility: 'session/<id>', 'project/<hash>', "
                "'user/<name>', 'team/<name>'. Stores bulk data (code snapshots, "
                "diffs, build outputs, analysis results) not just small strings."
            ),
            inputSchema={
                "type": "object",
                "properties": {
                    "key": {
                        "type": "string",
                        "description": "Storage key (e.g. 'arch/dependency-graph', 'test-results/run-42')",
                    },
                    "data": {
                        "type": "string",
                        "description": "Data to store (text or base64-encoded binary)",
                    },
                    "scope": {
                        "type": "string",
                        "description": "Workspace scope: 'session/<id>', 'project/<hash>', 'user/<name>'",
                        "default": "project/default",
                    },
                    "intent": {
                        "type": "string",
                        "enum": [
                            "checkpoint", "cache", "tool_output", "final_result",
                            "intermediate", "shared_state", "embedding",
                            "model_weight", "kv_cache", "reasoning_trace",
                        ],
                        "description": "What this data is for (affects lifecycle and retrieval ranking)",
                        "default": "reasoning_trace",
                    },
                    "ttl_seconds": {
                        "type": "integer",
                        "description": "Auto-expire after N seconds (0 = permanent)",
                        "default": 0,
                    },
                },
                "required": ["key", "data"],
            },
        ),
        Tool(
            name="labios_retrieve",
            description=(
                "Retrieve data from a LABIOS workspace. Searches across scopes "
                "in priority order: session -> project -> user -> team. "
                "Returns the data and metadata (intent, version, size)."
            ),
            inputSchema={
                "type": "object",
                "properties": {
                    "key": {
                        "type": "string",
                        "description": "Storage key to retrieve",
                    },
                    "scope": {
                        "type": "string",
                        "description": "Specific scope to search, or 'all' to search all tiers",
                        "default": "all",
                    },
                },
                "required": ["key"],
            },
        ),
        Tool(
            name="labios_knowledge",
            description=(
                "Query what knowledge is stored across all accessible memory tiers. "
                "Without a query: returns summary stats (count per scope and intent). "
                "With a query: returns matching keys ranked by scope and recency."
            ),
            inputSchema={
                "type": "object",
                "properties": {
                    "query": {
                        "type": "string",
                        "description": "Search term to match against keys (prefix match). Empty for summary.",
                        "default": "",
                    },
                },
            },
        ),
    ]


# ── Tool implementations ──────────────────────────────────────────────────

@app.call_tool()
async def call_tool(name: str, arguments: dict) -> list[TextContent]:
    if name == "labios_observe":
        return await _observe(arguments["query"])
    elif name == "labios_store":
        return await _store(arguments)
    elif name == "labios_retrieve":
        return await _retrieve(arguments)
    elif name == "labios_knowledge":
        return await _knowledge(arguments.get("query", ""))
    return _text({"error": f"unknown tool: {name}"})


async def _observe(query: str) -> list[TextContent]:
    """Query LABIOS runtime state from Redis keys the dispatcher publishes."""
    if query == "queue/depth":
        val = r.get("labios:queue:depth")
        depth = int(val) if val else 0
        return _text({"queue_depth": depth, "timestamp_ms": int(time.time() * 1000)})

    if query == "system/health":
        try:
            r.ping()
            redis_ok = True
        except Exception:
            redis_ok = False
        uptime_val = r.get("labios:dispatcher:start_ms")
        uptime_s = 0
        if uptime_val:
            start_ms = int(uptime_val)
            uptime_s = (int(time.time() * 1000) - start_ms) // 1000
        return _text({
            "redis": "connected" if redis_ok else "disconnected",
            "uptime_seconds": uptime_s,
        })

    if query == "workers/scores":
        keys = list(r.scan_iter("labios:worker:score:*"))
        workers = []
        for key in keys:
            data = r.hgetall(key)
            if data:
                workers.append({
                    k.decode(): v.decode() for k, v in data.items()
                })
        return _text({"workers": workers})

    if query == "workspaces/list":
        keys = list(r.scan_iter("labios:ws:*:_index"))
        names = []
        for key in keys:
            k = key.decode()
            parts = k.split(":")
            if len(parts) >= 3:
                names.append(":".join(parts[2:-1]))
        return _text({"workspaces": names})

    if query == "channels/list":
        keys = list(r.scan_iter("labios:channel:*"))
        channel_names = set()
        for key in keys:
            k = key.decode()
            parts = k.removeprefix("labios:channel:").split(":")
            if parts:
                channel_names.add(parts[0])
        return _text({"channels": sorted(channel_names)})

    return _text({"error": f"unknown observe query: {query}"})


async def _store(args: dict) -> list[TextContent]:
    """Store data in a LABIOS workspace (matches C++ workspace key patterns)."""
    key = args["key"]
    data = args["data"].encode("utf-8")
    scope = args.get("scope", "project/default")
    intent = args.get("intent", "reasoning_trace")
    ttl = args.get("ttl_seconds", 0)

    redis_key = _ws_data_key(scope, key)
    r.set(redis_key, data)

    meta_key = _ws_meta_key(scope, key)
    now_ms = int(time.time() * 1000)
    version_val = r.hincrby(meta_key, "version", 1)
    r.hset(meta_key, mapping={
        "intent": intent,
        "size": len(data),
        "updated_ms": str(now_ms),
        "version": str(version_val),
    })

    r.sadd(_ws_index_key(scope), key)

    version_key = _ws_version_key(scope, key, version_val)
    r.set(version_key, data)

    if ttl > 0:
        r.expire(redis_key, ttl)
        r.expire(meta_key, ttl)
        r.expire(version_key, ttl)

    return _text({
        "stored": True,
        "key": key,
        "scope": scope,
        "version": version_val,
        "size_bytes": len(data),
        "intent": intent,
    })


async def _retrieve(args: dict) -> list[TextContent]:
    """Retrieve data from workspace, searching across tiers if scope='all'."""
    key = args["key"]
    scope = args.get("scope", "all")

    if scope != "all":
        return _text(_get_from_scope(scope, key))

    scopes_to_search = []
    for pattern_prefix in ["session/", "project/", "user/", "team/"]:
        index_keys = list(r.scan_iter(f"labios:ws:{pattern_prefix}*:_index"))
        for idx_key in index_keys:
            ws_name = idx_key.decode().removeprefix("labios:ws:").removesuffix(":_index")
            if r.sismember(idx_key, key.encode()):
                scopes_to_search.append(ws_name)

    if not scopes_to_search:
        return _text({"found": False, "key": key, "searched": "all tiers"})

    tier_order = {"session": 0, "project": 1, "user": 2, "team": 3}
    scopes_to_search.sort(key=lambda s: tier_order.get(s.split("/")[0], 99))

    result = _get_from_scope(scopes_to_search[0], key)
    result["searched_scopes"] = scopes_to_search
    return _text(result)


def _get_from_scope(scope: str, key: str) -> dict:
    redis_key = _ws_data_key(scope, key)
    data = r.get(redis_key)
    if data is None:
        return {"found": False, "key": key, "scope": scope}

    meta_key = _ws_meta_key(scope, key)
    meta = r.hgetall(meta_key)
    meta_dict = {k.decode(): v.decode() for k, v in meta.items()} if meta else {}

    return {
        "found": True,
        "key": key,
        "scope": scope,
        "data": data.decode("utf-8", errors="replace"),
        "intent": meta_dict.get("intent", "unknown"),
        "version": int(meta_dict.get("version", 1)),
        "size_bytes": len(data),
    }


async def _knowledge(query: str) -> list[TextContent]:
    """Query knowledge across all accessible tiers."""
    if not query:
        summary = {}
        for prefix in ["session/", "project/", "user/", "team/"]:
            index_keys = list(r.scan_iter(f"labios:ws:{prefix}*:_index"))
            for idx_key in index_keys:
                ws_name = idx_key.decode().removeprefix("labios:ws:").removesuffix(":_index")
                count = r.scard(idx_key)
                summary[ws_name] = count
        return _text({"knowledge_summary": summary, "total_tiers": len(summary)})

    results = []
    for prefix in ["session/", "project/", "user/", "team/"]:
        index_keys = list(r.scan_iter(f"labios:ws:{prefix}*:_index"))
        for idx_key in index_keys:
            ws_name = idx_key.decode().removeprefix("labios:ws:").removesuffix(":_index")
            members = r.smembers(idx_key)
            for member in members:
                k = member.decode()
                if query.lower() in k.lower():
                    meta_key = _ws_meta_key(ws_name, k)
                    meta = r.hgetall(meta_key)
                    meta_dict = {mk.decode(): mv.decode() for mk, mv in meta.items()} if meta else {}
                    results.append({
                        "key": k,
                        "scope": ws_name,
                        "intent": meta_dict.get("intent", "unknown"),
                        "size_bytes": int(meta_dict.get("size", 0)),
                        "version": int(meta_dict.get("version", 1)),
                    })

    tier_order = {"session": 0, "project": 1, "user": 2, "team": 3}
    results.sort(key=lambda x: tier_order.get(x["scope"].split("/")[0], 99))

    return _text({"query": query, "matches": results, "count": len(results)})


# ── Entry point ───────────────────────────────────────────────────────────

async def main():
    async with stdio_server() as (read_stream, write_stream):
        await app.run(read_stream, write_stream, app.create_initialization_options())

if __name__ == "__main__":
    asyncio.run(main())
