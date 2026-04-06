# LABIOS: The First Agent I/O Runtime

LABIOS converts all I/O into self-describing labels that flow through a distributed
runtime of shufflers, schedulers, and workers. Each component enriches the label
as it passes. Labels are the information highway of the system.

**US Patent 11,630,834 B2 | NSF Award #2331480 | HPDC'19 Best Paper Nominee**

## What Is It

LABIOS is not a filesystem, not middleware, not an object store. It is the
execution environment for I/O operations expressed as labels. Agents, HPC
applications, and AI frameworks produce labels. LABIOS routes, shuffles,
schedules, transforms, and delivers them to workers that execute against any
storage backend.

```
Agent / HPC App
    │
    │ SDK, C API, Python, or LD_PRELOAD intercept
    ▼
LABIOS Client (Label Manager + Content Manager + Catalog Manager)
    │
    │ NATS JetStream (labels) + DragonflyDB (data staging)
    ▼
Dispatcher (Shuffler → Scheduler → Continuation Processor)
    │
    ▼
Workers (Tier 0: Databot | Tier 1: Pipeline | Tier 2: Agentic)
    │
    ▼
Backends (file:// | kv:// | sqlite:// | s3:// | vector:// | graph://)
```

Clients never talk to workers. The dispatcher is the only bridge.

## Quick Start

```bash
git clone https://github.com/akougkas/labios.git && cd labios
docker compose up -d
```

Write and read your first label (Python):

```python
import labios
client = labios.connect_to("nats://localhost:4222", "localhost", 6379)
client.write("/data/hello.dat", b"Hello from LABIOS!", 0)
print(client.read("/data/hello.dat", 0, 18))
```

See [docs/getting-started.md](docs/getting-started.md) for the full 5-minute walkthrough
with C++, C, and MCP examples.

## Capabilities

| Feature | Description |
|---------|-------------|
| Label routing | URI-based routing to any backend (file, KV, SQLite, and more) |
| Shuffler | Write aggregation, RAW/WAW/WAR dependency detection, supertask creation |
| 4 schedulers | Round Robin, Random, Constraint-based, MinMax DP with weight profiles |
| 3 worker tiers | Databot (stateless I/O), Pipeline (DAG execution), Agentic (reasoning) |
| SDS pipelines | 11 built-in operations, programmable DAGs, pipeline-at-storage execution |
| Channels | Streaming pub/sub with backpressure, TTL, and ordered delivery |
| Workspaces | Persistent shared state with per-key versioning and ACLs |
| Elastic scaling | Per-tier auto-scaling via Docker Engine API with energy budgets |
| POSIX intercept | LD_PRELOAD transparent interception of 30 POSIX and stdio calls |
| MCP server | 5 tools for coding agent integration (observe, store, retrieve, process, knowledge) |
| Observability | 8 query endpoints, continuous telemetry with p50/p95/p99 latencies |
| Continuations | Reactive I/O chaining (Notify, Chain, Conditional) on label completion |

## Client APIs

Eight layers of abstraction across three languages:

| Layer | C++ | Python | C |
|-------|-----|--------|---|
| Sync I/O | `write()` / `read()` | `write()` / `read()` | `labios_write()` / `labios_read()` |
| Async I/O | `async_write()` / `wait()` | `async_write()` / `wait()` | `labios_async_write()` / `labios_wait()` |
| Label-level | `create_label()` / `publish()` | `create_label()` / `publish()` | |
| URI-based | `write_to("kv://...")` | `write_to("kv://...")` | |
| Intent-driven | `write_with_intent()` | `write_with_intent()` | |
| Channels | `publish_to_channel()` | `publish_to_channel()` | |
| Workspaces | `workspace_put()` / `workspace_get()` | `workspace_put()` / `workspace_get()` | |
| Observability | `observe()` | `observe()` | |

## Agent Integration (MCP)

Connect Claude Code, Codex CLI, or any MCP-compatible agent:

```json
{
  "mcpServers": {
    "labios": {
      "command": "/path/to/labios/mcp/connect.sh"
    }
  }
}
```

The agent gains five MCP tools: `labios_observe`, `labios_store`,
`labios_retrieve`, `labios_process`, and `labios_knowledge`. See
[docs/mcp-integration.md](docs/mcp-integration.md) for the full reference.

## Build

```bash
# Docker (recommended)
docker compose up -d              # Full stack
docker compose exec test bash     # Shell into test container

# Native
cmake --preset dev
cmake --build build/dev -j$(nproc)
ctest --test-dir build/dev        # 353 tests
```

## Test Suite

353 tests across five categories:

| Category | Count | Infrastructure |
|----------|-------|----------------|
| Unit | 224 | None |
| Smoke | 68 | NATS + DragonflyDB |
| Kernel | 15 | NATS + DragonflyDB |
| Benchmark | 41 | None (unit-level comparison) |
| Integration | 5 | Full stack |

```bash
ctest --test-dir build/dev -L unit      # Fast, no infrastructure
ctest --test-dir build/dev -L smoke     # Needs live cluster
ctest --test-dir build/dev -L bench     # Vanilla-vs-LABIOS comparisons
```

## Tech Stack

C++20 (coroutines, jthread, concepts) | FlatBuffers | NATS 2.10 JetStream |
DragonflyDB | io_uring with POSIX fallback | xxHash3 | pybind11 | Catch2 |
Docker Compose | CMake 3.25+ | GitHub Actions (ASan, TSan, UBSan)

## Documentation

| Document | Purpose |
|----------|---------|
| [Getting Started](docs/getting-started.md) | 5-minute quickstart |
| [SDK Guide](docs/sdk-guide.md) | Full API reference (C++, Python, C) |
| [Deployment](docs/deployment.md) | Docker Compose, scaling, multi-node |
| [Configuration](docs/configuration.md) | TOML fields, weight profiles, env vars |
| [Backends](docs/backends.md) | BackendStore concept, writing new backends |
| [MCP Integration](docs/mcp-integration.md) | Connecting coding agents via MCP |
| [Architecture](docs/architecture.md) | Complete implementation reference |
| [LABIOS-SPEC.md](LABIOS-SPEC.md) | Definitive specification (design authority) |

## Publications

1. A. Kougkas, H. Devarajan, J. Lofstead, X.-H. Sun. "LABIOS: A Distributed Label-Based I/O System." HPDC'19.
2. US Patent 11,630,834 B2. "Label-Based Data Representation I/O Process and System."

## Team

**PI:** Dr. Xian-He Sun | **Co-PI:** Dr. Anthony Kougkas | Illinois Institute of Technology

## License

BSD 3-Clause. See `COPYING`.
