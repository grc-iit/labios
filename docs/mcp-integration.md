# LABIOS MCP Integration Guide

LABIOS ships an MCP (Model Context Protocol) server that gives coding agents
direct access to the runtime. When connected, Claude Code, Codex CLI, or any
MCP-compatible agent gains five tools for storing data, querying state, and
running pipelines at the storage layer.

## Prerequisites

- LABIOS runtime running (`docker compose up -d`)
- Claude Code, Codex CLI, or another MCP-compatible client

## Quick Setup

### 1. Start the Runtime

```bash
cd /path/to/labios
docker compose up -d
docker compose ps   # Verify all services healthy
```

### 2. Configure Your Agent

Add the LABIOS MCP server to your agent's configuration.

**Claude Code** (`~/.claude/settings.json` or project `.mcp.json`):

```json
{
  "mcpServers": {
    "labios": {
      "command": "/absolute/path/to/labios/mcp/connect.sh"
    }
  }
}
```

**Codex CLI** (`.codex/config.json`):

```json
{
  "mcpServers": {
    "labios": {
      "command": "/absolute/path/to/labios/mcp/connect.sh"
    }
  }
}
```

The `connect.sh` script runs `docker compose exec mcp python -m labios_mcp`
to attach the agent to the MCP container via stdio. The path must be absolute.

### 3. Verify Connection

Start a new Claude Code session and test the connection:

```
> Use labios_observe to check system health

labios_observe(query="system/health")
→ {"status": "healthy", "nats": "connected", "redis": "connected", ...}
```

## MCP Tools Reference

### `labios_observe`

Query runtime state without side effects.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `query` | string | yes | Observation target |

Supported queries:

| Query | Returns |
|-------|---------|
| `system/health` | NATS and Redis connection status, worker count |
| `system/queue_depth` | Labels waiting in the dispatch queue |
| `system/worker_scores` | Current scores for all registered workers |
| `system/channels` | Active channel names and subscriber counts |
| `system/workspaces` | Active workspace names and key counts |
| `system/config` | Current runtime configuration |

Example:
```json
labios_observe(query="system/worker_scores")
→ {
    "workers": [
        {"id": "worker-1", "speed": 5, "energy": 1, "score": 0.82},
        {"id": "worker-2", "speed": 3, "energy": 3, "score": 0.65},
        {"id": "worker-3", "speed": 1, "energy": 5, "score": 0.41}
    ]
  }
```

### `labios_store`

Store data in workspace memory with scope-based organization.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `key` | string | yes | Storage key (e.g., `model/weights`, `cache/embeddings`) |
| `value` | string | yes | Data to store |
| `scope` | string | no | Memory scope: `session`, `project`, `user`, `team` (default: `session`) |
| `metadata` | object | no | Additional metadata to attach |
| `ttl_seconds` | integer | no | Time-to-live in seconds (0 = permanent) |

Scopes control data visibility and persistence:

| Scope | Lifetime | Visibility |
|-------|----------|------------|
| `session` | Current agent session | This agent only |
| `project` | Project duration | All agents in project |
| `user` | Indefinite | All of this user's agents |
| `team` | Indefinite | All team members' agents |

Example:
```json
labios_store(
    key="analysis/findings",
    value="The codebase uses C++20 coroutines for all async paths...",
    scope="project/labios",
    metadata={"source": "code_review", "confidence": 0.95}
)
```

### `labios_retrieve`

Retrieve data from workspace memory. Searches scopes in priority order:
session, project, user, team.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `key` | string | yes | Storage key to retrieve |
| `scope` | string | no | Specific scope to search (default: searches all) |
| `version` | integer | no | Specific version (default: latest) |

Example:
```json
labios_retrieve(key="analysis/findings")
→ {
    "key": "analysis/findings",
    "value": "The codebase uses C++20 coroutines...",
    "scope": "project/labios",
    "version": 1,
    "metadata": {"source": "code_review", "confidence": 0.95}
  }
```

### `labios_process`

Process files through pipelines at the storage layer. Data flows through
pipeline stages without loading raw content into the agent context.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `source` | string | yes | Source path or URI |
| `pipeline` | array | yes | Ordered list of pipeline operations |
| `output_format` | string | no | Output format: `text`, `json`, `summary` (default: `text`) |

Available pipeline operations:

| Operation | Description | Example |
|-----------|-------------|---------|
| `grep:PATTERN` | Filter lines matching pattern | `grep:TODO` |
| `head:N` | Take first N lines | `head:20` |
| `tail:N` | Take last N lines | `tail:10` |
| `count` | Count lines | `count` |
| `wc` | Word count | `wc` |
| `sort` | Sort lines | `sort` |
| `uniq` | Remove duplicate lines | `uniq` |
| `filter:FIELD:VALUE` | Filter structured data by field | `filter:status:error` |
| `sample:N` | Random sample of N lines | `sample:5` |

Example:
```json
labios_process(
    source="/data/logs/app.log",
    pipeline=["grep:ERROR", "tail:50", "count"]
)
→ {"result": "17", "stages_executed": 3}
```

### `labios_knowledge`

Query stored knowledge across all memory tiers. Returns a summary of stored
data organized by scope and prefix.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `query` | string | no | Prefix filter (default: list everything) |
| `scope` | string | no | Limit to specific scope |

Example:
```json
labios_knowledge(query="analysis/")
→ {
    "entries": [
        {"key": "analysis/findings", "scope": "project/labios", "size": 1234},
        {"key": "analysis/metrics", "scope": "session", "size": 567}
    ],
    "total_entries": 2,
    "total_size": 1801
  }
```

## How It Works

The MCP server runs as a Python process inside a Docker container alongside the
LABIOS stack. It connects to the same DragonflyDB instance used by the C++
runtime, reading workspace data via the same key patterns:

```
labios:ws:{scope}:{key}           → data
labios:ws:{scope}:{key}:vN        → versioned data
labios:ws:{scope}:{key}:metadata  → JSON metadata
```

For `labios_observe`, the server queries NATS and DragonflyDB directly to
report system state. For `labios_process`, it reads files from the worker's
data volume (mounted read-only) and applies pipeline operations in Python.

## Architecture

```
Claude Code / Codex CLI
        │
        │ MCP stdio protocol
        │
        ▼
┌──────────────────┐
│  connect.sh      │
│  docker exec     │
└───────┬──────────┘
        │
        ▼
┌──────────────────┐     ┌──────────┐     ┌──────┐
│  labios_mcp      │────▶│DragonflyDB│    │ NATS  │
│  (Python MCP)    │     │  :6379   │     │:4222  │
│                  │────▶│          │     │       │
└──────────────────┘     └──────────┘     └──────┘
        │
        ▼
┌──────────────────┐
│  Worker data     │
│  volume (ro)     │
└──────────────────┘
```

## Troubleshooting

### "Connection refused" on startup

The runtime must be running before connecting. Start it with `docker compose up
-d` and wait for health checks to pass.

### MCP tools not appearing

Verify the path in your configuration is absolute and points to the correct
`connect.sh`:

```bash
# Test the connection script manually
/path/to/labios/mcp/connect.sh
# Should start the MCP server and wait for stdio input
```

### Redis connection errors in tool output

The MCP server connects to DragonflyDB lazily on first tool use. If DragonflyDB
is still starting, the first call may fail. Retry after a few seconds.

### Stale data after restart

`docker compose down -v` removes all data volumes. If you want to preserve
workspace data across restarts, use `docker compose down` (without `-v`).

## Running MCP Server Tests

```bash
cd mcp
python -m pytest tests/ -v
```

Tests mock the Redis connection and verify tool input/output contracts.
