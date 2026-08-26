# LABIOS: The First Agent I/O Runtime

LABIOS converts all I/O into self-describing labels that flow through a distributed
runtime of shufflers, schedulers, and workers. Each component enriches the label
as it passes. Labels are the information highway of the system.

**US Patent 11,630,834 B2 | NSF Award #2313154 | HPDC'19 Karsten Schwan Best Paper Award**

**Release status:** LABIOS 2.1.0-rc.1 candidate. See
[CHANGELOG.md](CHANGELOG.md) for scope and compatibility notes.

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
    │ NATS (labels) + DragonflyDB (data staging)
    ▼
Dispatcher (Shuffler → Scheduler → Continuation Processor)
    │
    ▼
Workers (Tier 0: Databot | Tier 1: Pipeline | Tier 2: Agentic)
    │
    ▼
Backends (file:// | sqlite:// | optional kv:// | planned S3/vector/graph)
```

Clients never talk to workers. The dispatcher is the only bridge.

> **Implementation status:** This README describes the intended system. Several
> capabilities are component-level or prototype rather than verified end to end
> (for example, Tier 2 workers are not yet reasoning-capable, cross-process
> channels/workspaces are not implemented, and elastic scaling is off by
> default). See the
> internal planning authority `.planning/LABIOS-2.1.md` for the engineering
> truth baseline and project direction.

## Quick Start

```bash
git clone https://github.com/grc-iit/labios.git && cd labios
docker compose up -d --build --wait
```

Run the packaged Python Label I/O golden path:

```bash
docker compose run --rm --build test \
  python3 /opt/labios/examples/python/golden.py
```

The example uses only public APIs and verifies exact source → pipeline →
destination bytes.

See [docs/getting-started.md](docs/getting-started.md) for the full 5-minute walkthrough
with C++, C, and MCP examples.

## Capabilities

| Feature | Description |
|---------|-------------|
| Label routing | URI-based routing to any backend (file, KV, SQLite, and more) |
| Shuffler | Write aggregation, RAW/WAW/WAR dependency detection, supertask creation |
| 4 schedulers | Round Robin, Random, Constraint-based, MinMax DP with weight profiles |
| 3 worker tiers | Databot (stateless I/O) and Pipeline (DAG execution) are behavioral; Agentic/Tier 2 reasoning is planned (enum + scoring only today) |
| SDS pipelines | 11 built-in byte-level operations, programmable DAGs, pipeline-at-storage execution |
| Channels | Streaming pub/sub with TTL and ordered delivery (registries are process-local today) |
| Workspaces | Shared state (Redis) with per-key versioning; ACLs are process-local today |
| Elastic scaling | Per-tier auto-scaling via Docker Engine API (off by default; enable via `docker-compose.elastic.yml`) |
| POSIX intercept | LD_PRELOAD transparent interception of 30 POSIX and stdio calls |
| MCP server | Public Label I/O frontend for observe, typed store/retrieve, and registered pipelines; workspace-backed knowledge is not available in this candidate |
| Observability | 8 query endpoints, continuous telemetry with p50/p95/p99 latencies |
| Continuations | Reactive I/O chaining (Notify, Chain, Conditional) on label completion |

## Client APIs

C++ and Python expose the headline Label I/O lifecycle. Python binds the same
owning `Operation`, typed resources/labels, label-level publication, URI I/O,
intent/priority, pipelines, completion/cancellation/lifecycle results, public
placement history, and Observe queries. C++ additionally exposes the current
process-local channel/workspace handle APIs and process-local `set_config`.

| Layer | C++ | Python | C |
|-------|-----|--------|---|
| Sync I/O | `write()` / `read()` | `write()` / `read()` | `labios_write()` / `labios_read()` |
| Async I/O | owning `Operation`: `test()` / `wait_for()` / `cancel()` | owning `Operation`: `test()` / `wait_for()` / `cancel()` | owning status: `labios_test()` / `labios_wait_for()` / `labios_cancel()` |
| Label-level | `create_label()` / `publish()` | `create_label()` / `publish()` | |
| URI-based | `write_to("kv://...")` | `write_to("kv://...")` | |
| Intent-driven | `write_with_intent()` | `write_with_intent()` | |
| Channels | `publish_to_channel()` | `publish_to_channel()` | |
| Workspaces | `workspace_put()` / `workspace_get()` | `workspace_put()` / `workspace_get()` | |
| Observability | `observe()` / `inspect_label()` | `observe()` / `inspect_label()` | |

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

The agent gains five MCP tool names: `labios_observe`, `labios_store`,
`labios_retrieve`, `labios_process`, and `labios_knowledge`. Core I/O compiles to
typed labels and traverses the public Python Client, dispatcher, scheduler,
worker, and external backend. `labios_knowledge` returns an explicit
`unsupported_feature` response rather than using a direct-store fallback. See
[docs/mcp-integration.md](docs/mcp-integration.md) for schemas, stable error
payloads, and migration breaks.

## Build

```bash
# Docker (recommended)
docker compose up -d              # Full stack
docker compose exec test bash     # Shell into test container

# Native
cmake --preset dev
cmake --build build/dev -j$(nproc)
ctest --test-dir build/dev -N     # list discovered tests
ctest --test-dir build/dev        # run discovered C++ tests
```

## Test Suite

CTest discovery from the build tree is the authoritative source for C++ test
counts. Python SDK and MCP tests are collected by pytest in their package
directories.

| Label | Infrastructure | Scope |
|-------|----------------|-------|
| Unit | None | Components in isolation |
| Smoke | NATS + DragonflyDB | Live runtime paths |
| Kernel | NATS + DragonflyDB | Science application replays |
| Benchmark | None | Unit-level vanilla-vs-LABIOS comparisons |
| Integration | Full stack | Cross-service behavior |
| MCP | Python + MCP dependencies | Coding-agent tool server |

```bash
ctest --test-dir build/dev -N           # discovered C++ tests
ctest --test-dir build/dev -L unit      # Fast, no infrastructure
ctest --test-dir build/dev -L smoke     # Needs live cluster
ctest --test-dir build/dev -L bench     # Vanilla-vs-LABIOS comparisons
python3 -m pytest tests/python
PYTHONPATH="$PWD/build/dev/python:$PWD/mcp" python3 -m pytest mcp/tests -m "not live"
# With Compose running:
docker compose exec -T -e LABIOS_MCP_LIVE=1 mcp python -m pytest tests -m live
```

## Tech Stack

C++20 (`std::jthread`, concepts) | FlatBuffers | NATS 2.10 (server runs with
JetStream enabled; labels use durable JetStream delivery) | DragonflyDB |
POSIX file I/O (io_uring planned) | xxHash3 | pybind11 | Catch2 | Docker Compose |
CMake 3.25+ | GitHub Actions (strict native/unit/Python/MCP and Compose golden path)

## Documentation

| Document | Purpose |
|----------|---------|
| [Getting Started](docs/getting-started.md) | 5-minute quickstart |
| [SDK Guide](docs/sdk-guide.md) | Full API reference (C++, Python, C) |
| [Deployment](docs/deployment.md) | Docker Compose, scaling, multi-node |
| [Configuration](docs/configuration.md) | TOML fields, weight profiles, env vars |
| [Backends](docs/backends.md) | BackendStore concept, writing new backends |
| [MCP Integration](docs/mcp-integration.md) | Connecting coding agents via MCP |
| [Architecture](docs/architecture.md) | Intended architecture reference |

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for setup instructions, coding conventions,
and how to submit pull requests.

## Publications

1. A. Kougkas, H. Devarajan, J. Lofstead, X.-H. Sun. "LABIOS: A Distributed Label-Based I/O System." HPDC'19.
2. US Patent 11,630,834 B2. "Label-Based Data Representation I/O Process and System."

## Team

**PI:** Dr. Xian-He Sun | **Co-PI:** Dr. Anthony Kougkas | Illinois Institute of Technology

## License

BSD 3-Clause. See `COPYING`.
