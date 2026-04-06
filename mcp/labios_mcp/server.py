#!/usr/bin/env python3
"""LABIOS MCP Server.

Exposes LABIOS runtime operations as MCP tools for coding agents.
Connects to DragonflyDB (warehouse) and reads/writes the same keys
the C++ runtime uses. Runs inside Docker Compose on the same network.
"""
import asyncio
import glob as globmod
import json
import os
import pathlib
import random
import re
import time
from typing import Any

import redis.asyncio as aioredis
from mcp.server import Server
from mcp.server.stdio import stdio_server
from mcp.types import TextContent, Tool

REDIS_HOST = os.environ.get("LABIOS_REDIS_HOST", "redis")
REDIS_PORT = int(os.environ.get("LABIOS_REDIS_PORT", "6379"))
NATS_URL = os.environ.get("LABIOS_NATS_URL", "nats://nats:4222")

app = Server("labios")

# Lazy-initialized async Redis connection
_redis: aioredis.Redis | None = None


async def get_redis() -> aioredis.Redis:
    global _redis
    if _redis is None:
        _redis = aioredis.Redis(
            host=REDIS_HOST, port=REDIS_PORT, decode_responses=False
        )
    return _redis


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


# ── Pipeline operations (pure functions, no I/O) ─────────────────────────


def _apply_pipeline_op(op: str, lines: list[str], filename: str) -> list[str]:
    """Apply a single pipeline operation to lines."""
    parts = op.split(":", 1)
    cmd = parts[0]
    arg = parts[1] if len(parts) > 1 else ""

    if cmd == "grep":
        try:
            pattern = re.compile(arg, re.IGNORECASE)
            return [line for line in lines if pattern.search(line)]
        except re.error:
            return [line for line in lines if arg in line]
    elif cmd == "head":
        n = int(arg) if arg else 10
        return lines[:n]
    elif cmd == "tail":
        n = int(arg) if arg else 10
        return lines[-n:]
    elif cmd == "count":
        return [str(len(lines))]
    elif cmd == "wc":
        word_count = sum(len(line.split()) for line in lines)
        char_count = sum(len(line) for line in lines)
        return [f"lines: {len(lines)}, words: {word_count}, chars: {char_count}"]
    elif cmd == "sort":
        return sorted(lines)
    elif cmd == "uniq":
        seen: set[str] = set()
        result = []
        for line in lines:
            if line not in seen:
                seen.add(line)
                result.append(line)
        return result
    elif cmd == "filter":
        return [line for line in lines if arg in line]
    elif cmd == "sample":
        n = int(arg) if arg else 10
        return random.sample(lines, min(n, len(lines)))
    return lines


# ── Tool definitions ──────────────────────────────────────────────────────


@app.list_tools()
async def list_tools() -> list[Tool]:
    return [
        Tool(
            name="labios_observe",
            description=(
                "Query LABIOS runtime state. Returns JSON with system metrics. "
                "Queries: 'queue/depth', 'workers/scores', 'workers/count', "
                "'system/health', 'channels/list', 'workspaces/list', "
                "'config/current', 'data/location?file=/path'."
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
        Tool(
            name="labios_process",
            description=(
                "Process data at the storage layer using a pipeline of operations. "
                "Source can be a file path or glob pattern. Pipeline operations "
                "execute without loading raw data into the agent's context. "
                "Example: labios_process(source='/repo/src/**/*.py', "
                "pipeline=['grep:TODO', 'head:20'])"
            ),
            inputSchema={
                "type": "object",
                "properties": {
                    "source": {
                        "type": "string",
                        "description": "File path or glob pattern to process",
                    },
                    "pipeline": {
                        "type": "array",
                        "items": {"type": "string"},
                        "description": (
                            "Pipeline operations: 'grep:<pattern>', 'head:<n>', "
                            "'tail:<n>', 'filter:<expr>', 'count', 'wc', 'sort', "
                            "'uniq', 'sample:<n>'"
                        ),
                    },
                    "intent": {
                        "type": "string",
                        "default": "tool_output",
                    },
                },
                "required": ["source", "pipeline"],
            },
        ),
    ]


# ── Tool dispatch ────────────────────────────────────────────────────────


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
    elif name == "labios_process":
        return await _process(arguments)
    return _text({"error": f"unknown tool: {name}"})


# ── Tool implementations ────────────────────────────────────────────────


async def _observe(query: str) -> list[TextContent]:
    """Query LABIOS runtime state from Redis keys the dispatcher publishes."""
    try:
        r = await get_redis()

        if query == "queue/depth":
            val = await r.get("labios:queue:depth")
            depth = int(val) if val else 0
            return _text({"queue_depth": depth, "timestamp_ms": int(time.time() * 1000)})

        if query == "system/health":
            try:
                await r.ping()
                redis_ok = True
            except Exception:
                redis_ok = False
            uptime_val = await r.get("labios:dispatcher:start_ms")
            uptime_s = 0
            if uptime_val:
                start_ms = int(uptime_val)
                uptime_s = (int(time.time() * 1000) - start_ms) // 1000
            return _text({
                "redis": "connected" if redis_ok else "disconnected",
                "uptime_seconds": uptime_s,
            })

        if query == "workers/scores":
            keys = [k async for k in r.scan_iter("labios:worker:score:*")]
            workers = []
            for key in keys:
                data = await r.hgetall(key)
                if data:
                    workers.append({
                        k.decode(): v.decode() for k, v in data.items()
                    })
            return _text({"workers": workers})

        if query == "workspaces/list":
            keys = [k async for k in r.scan_iter("labios:ws:*:_index")]
            names = []
            for key in keys:
                k_str = key.decode()
                parts = k_str.split(":")
                if len(parts) >= 3:
                    names.append(":".join(parts[2:-1]))
            return _text({"workspaces": names})

        if query == "channels/list":
            keys = [k async for k in r.scan_iter("labios:channel:*")]
            channel_names: set[str] = set()
            for key in keys:
                k_str = key.decode()
                parts = k_str.removeprefix("labios:channel:").split(":")
                if parts:
                    channel_names.add(parts[0])
            return _text({"channels": sorted(channel_names)})

        if query == "config/current":
            config_keys = [k async for k in r.scan_iter("labios:config:*")]
            config: dict[str, str] = {}
            for key in config_keys:
                val = await r.get(key)
                k_str = key.decode().removeprefix("labios:config:")
                config[k_str] = val.decode() if val else ""
            if not config:
                config = {"note": "no runtime config keys found"}
            return _text(config)

        if query.startswith("data/location"):
            file_path = ""
            if "?" in query:
                params = dict(
                    p.split("=", 1)
                    for p in query.split("?", 1)[1].split("&")
                    if "=" in p
                )
                file_path = params.get("file", "")
            if not file_path:
                return _text({"error": "missing file parameter"})
            loc = await r.get(f"labios:location:{file_path}")
            worker_id = int(loc) if loc else None
            return _text({"file": file_path, "worker_id": worker_id})

        if query == "workers/count":
            keys = [k async for k in r.scan_iter("labios:worker:score:*")]
            return _text({"worker_count": len(keys)})

        return _text({"error": f"unknown observe query: {query}"})

    except Exception as e:
        return _text({"error": f"Redis connection failed: {e}"})


async def _store(args: dict) -> list[TextContent]:
    """Store data in a LABIOS workspace (matches C++ workspace key patterns)."""
    try:
        r = await get_redis()
        key = args["key"]
        data = args["data"].encode("utf-8")
        scope = args.get("scope", "project/default")
        intent = args.get("intent", "reasoning_trace")
        ttl = args.get("ttl_seconds", 0)

        redis_key = _ws_data_key(scope, key)
        meta_key = _ws_meta_key(scope, key)
        now_us = int(time.time() * 1_000_000)
        index_key = _ws_index_key(scope)

        async with r.pipeline(transaction=True) as pipe:
            pipe.set(redis_key, data)
            pipe.hincrby(meta_key, "version", 1)
            pipe.hset(meta_key, mapping={
                "intent": intent,
                "size": len(data),
                "updated_us": str(now_us),
            })
            pipe.sadd(index_key, key)
            # version_key uses a placeholder; we fix it after execute
            # because we need the hincrby result for the version number.
            # Instead, do a two-phase approach: first get version, then store.
            results = await pipe.execute()
            version_val = results[1]  # hincrby returns the new value

        version_key = _ws_version_key(scope, key, version_val)
        async with r.pipeline(transaction=True) as pipe:
            pipe.set(version_key, data)
            pipe.hset(meta_key, "version", str(version_val))
            if ttl > 0:
                pipe.expire(redis_key, ttl)
                pipe.expire(meta_key, ttl)
                pipe.expire(version_key, ttl)
            await pipe.execute()

        return _text({
            "stored": True,
            "key": key,
            "scope": scope,
            "version": version_val,
            "size_bytes": len(data),
            "intent": intent,
        })

    except Exception as e:
        return _text({"error": f"Redis connection failed: {e}"})


async def _retrieve(args: dict) -> list[TextContent]:
    """Retrieve data from workspace, searching across tiers if scope='all'."""
    try:
        r = await get_redis()
        key = args["key"]
        scope = args.get("scope", "all")

        if scope != "all":
            return _text(await _get_from_scope(r, scope, key))

        scopes_to_search = []
        for pattern_prefix in ["session/", "project/", "user/", "team/"]:
            index_keys = [
                k async for k in r.scan_iter(f"labios:ws:{pattern_prefix}*:_index")
            ]
            for idx_key in index_keys:
                ws_name = idx_key.decode().removeprefix("labios:ws:").removesuffix(":_index")
                if await r.sismember(idx_key, key.encode()):
                    scopes_to_search.append(ws_name)

        if not scopes_to_search:
            return _text({"found": False, "key": key, "searched": "all tiers"})

        tier_order = {"session": 0, "project": 1, "user": 2, "team": 3}
        scopes_to_search.sort(key=lambda s: tier_order.get(s.split("/")[0], 99))

        result = await _get_from_scope(r, scopes_to_search[0], key)
        result["searched_scopes"] = scopes_to_search
        return _text(result)

    except Exception as e:
        return _text({"error": f"Redis connection failed: {e}"})


async def _get_from_scope(r: aioredis.Redis, scope: str, key: str) -> dict:
    redis_key = _ws_data_key(scope, key)
    data = await r.get(redis_key)
    if data is None:
        return {"found": False, "key": key, "scope": scope}

    meta_key = _ws_meta_key(scope, key)
    meta = await r.hgetall(meta_key)
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
    try:
        r = await get_redis()

        if not query:
            summary: dict[str, int] = {}
            for prefix in ["session/", "project/", "user/", "team/"]:
                index_keys = [
                    k async for k in r.scan_iter(f"labios:ws:{prefix}*:_index")
                ]
                for idx_key in index_keys:
                    ws_name = idx_key.decode().removeprefix("labios:ws:").removesuffix(":_index")
                    count = await r.scard(idx_key)
                    summary[ws_name] = count
            return _text({"knowledge_summary": summary, "total_tiers": len(summary)})

        results = []
        for prefix in ["session/", "project/", "user/", "team/"]:
            index_keys = [
                k async for k in r.scan_iter(f"labios:ws:{prefix}*:_index")
            ]
            for idx_key in index_keys:
                ws_name = idx_key.decode().removeprefix("labios:ws:").removesuffix(":_index")
                members = await r.smembers(idx_key)
                for member in members:
                    k = member.decode()
                    if query.lower() in k.lower():
                        meta_key = _ws_meta_key(ws_name, k)
                        meta = await r.hgetall(meta_key)
                        meta_dict = {
                            mk.decode(): mv.decode() for mk, mv in meta.items()
                        } if meta else {}
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

    except Exception as e:
        return _text({"error": f"Redis connection failed: {e}"})


async def _process(args: dict) -> list[TextContent]:
    """Process files through a pipeline at the storage layer."""
    try:
        source = args["source"]
        pipeline = args.get("pipeline", [])

        if ".." in source:
            return _text({"error": "path traversal not allowed"})

        search_root = pathlib.Path("/labios/data")
        if not search_root.exists():
            return _text({"error": "worker data volume not mounted at /labios/data"})

        # Resolve glob or single file
        if "*" in source or "?" in source:
            files = sorted(
                globmod.glob(str(search_root / source.lstrip("/")), recursive=True)
            )
        else:
            target = search_root / source.lstrip("/")
            files = [str(target)] if target.exists() else []

        if not files:
            return _text({
                "error": f"no files matched: {source}",
                "search_root": str(search_root),
            })

        # Process each file individually; accumulate only pipeline output
        output_lines: list[str] = []
        total_bytes = 0
        for f in files[:1000]:
            try:
                content = pathlib.Path(f).read_text(errors="replace")
                total_bytes += len(content.encode("utf-8"))
                lines = content.splitlines()
                for op in pipeline:
                    lines = _apply_pipeline_op(op, lines, str(f))
                if lines:
                    output_lines.append(f"=== {f} ===")
                    output_lines.extend(lines)
            except Exception as e:
                output_lines.append(f"=== {f} === ERROR: {e}")

        return _text({
            "files_matched": len(files),
            "bytes_processed": total_bytes,
            "output": "\n".join(output_lines[:5000]),
            "truncated": len(output_lines) > 5000,
        })

    except Exception as e:
        return _text({"error": f"processing failed: {e}"})


# ── Entry point ───────────────────────────────────────────────────────────


async def main():
    async with stdio_server() as (read_stream, write_stream):
        try:
            await app.run(read_stream, write_stream, app.create_initialization_options())
        finally:
            if _redis is not None:
                await _redis.aclose()


if __name__ == "__main__":
    asyncio.run(main())
