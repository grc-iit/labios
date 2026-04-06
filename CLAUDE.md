# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What This Is

LABIOS is the first agent I/O runtime. Not a filesystem, not middleware, not an object store. It is the execution environment for I/O operations expressed as labels. US Patent 11,630,834 B2, NSF Award #2331480, HPDC'19 Best Paper Nominee.

The system converts all I/O into self-describing labels that flow through a distributed runtime where each component (shuffler, scheduler, worker) enriches the label as it passes. Labels are the information highway of the system. Agents produce labels (via SDK or transparent intercept). LABIOS routes, shuffles, schedules, transforms, and delivers them to workers that execute against any storage backend.

## Specification

**Read `LABIOS-SPEC.md` before writing any code.** It is the definitive specification for LABIOS 2.0 and the primary design authority. It defines:
- The label as a mutable information carrier (residual stream model)
- Triple decoupling (instruction/data, production/execution, scheduling/storage)
- Universal label routing via URI schemes (file://, s3://, vector://, graph://)
- Programmable data pipelines (SDS): labels carry DAGs of operations
- Three worker tiers: Databot (Tier 0), Pipeline (Tier 1), Agentic (Tier 2)
- Channels (streaming coordination) and Workspaces (persistent shared state)
- Observability labels and telemetry stream
- Per-tier elastic scaling
- Continuation execution (reactive I/O chaining)
- Scale-adaptive deployment model

`LABIOS-2.0.md` is the constitutional document that established the rewrite. The spec supersedes it for all forward design decisions.

## Tech Stack

| Component | Choice |
|---|---|
| Language | C++20 (coroutines, jthread, concepts) |
| Build | CMake 3.25+ with presets |
| Serialization | FlatBuffers |
| Label queue | NATS 2.10+ with JetStream |
| Warehouse + Metadata | DragonflyDB (Redis 7 wire-compatible, multi-threaded) |
| Async I/O | io_uring with POSIX fallback |
| Hashing | xxHash3 |
| Python bindings | pybind11 |
| Testing | Catch2 (C++) + pytest (Python) |
| Config | TOML |
| Containers | Docker + Docker Compose |
| CI | GitHub Actions (ASan, TSan, UBSan) |

## Development Workflow

```bash
docker compose up -d              # Full system on localhost
docker compose exec test bash     # Shell into test container
./run_tests.sh                    # Unit + integration tests
docker compose down               # Teardown
```

Every developer clones and has a working system in under 5 minutes. No manual dependency installation.

## Build (native, outside Docker)

```bash
cmake --preset dev
cmake --build build/dev -j$(nproc)
ctest --test-dir build/dev
```

## Architectural Boundaries (Do Not Violate)

**LABIOS is a runtime, not a storage system.** It orchestrates I/O operations. It does not own or manage the storage systems that data ultimately lands on.

**Internal plumbing vs external backends.** These are completely separate concerns:

| Layer | What | Purpose | Examples |
|-------|------|---------|---------|
| **Internal plumbing** | DragonflyDB (warehouse), NATS (label queue) | Stage data in transit, route labels between components, store catalog metadata | `labios:data:{id}`, `labios.labels` subject |
| **External backends** | Storage systems the user already has | Final execution target for label I/O operations | User's filesystem, user's Redis, user's PostgreSQL, user's ChromaDB |

Never confuse these. The warehouse is not a backend. A backend is not plumbing. When adding a KV backend (`kv://`), it connects to the USER's Redis instance, not to LABIOS's internal DragonflyDB. When adding a vector backend (`vector://`), it connects to the USER's vector DB, not to anything LABIOS owns.

**All intelligence lives in the runtime.** Shuffling, scheduling, pipelines, coordination, caching, and elastic scaling happen BEFORE the label reaches the backend. Backends are thin adapters that translate the label's operation into the external system's native API. The backend's job is narrow: last-mile execution.

**BackendStore receives the full label** (spec S4.4) so it CAN use metadata (intent, isolation, priority) if the external system supports it. But the backend is not where optimization happens. The runtime already optimized everything upstream.

**For testing**, we add containers (Redis, ChromaDB, PostgreSQL) to docker-compose.yml to simulate user infrastructure. In production, backends connect to whatever the user already runs.

## Code Conventions

- C++20. Modules are a stretch goal, not required.
- `std::jthread` + `std::stop_token` for cooperative shutdown. No `kill` booleans.
- Coroutines (`co_await`) for async. No spin-polling, no busy-wait loops.
- `std::filesystem` for all path ops. No `std::system()`, no `popen()`.
- Smart pointers only. No raw `new`/`delete`.
- Concepts for constraining solver, backend, and serializer interfaces.
- Every component has Catch2 unit tests. No exceptions.

## Source Layout

- `src/labios/` — core library
  - `label.h/cpp` — label struct, serialization, snowflake IDs
  - `client.h/cpp` — client with sync/async/label-level/channel/workspace APIs
  - `dispatcher.h/cpp` — shuffler + scheduler
  - `channel.h/cpp` — streaming pub/sub channels
  - `workspace.h/cpp` — persistent shared state with ACL
  - `continuation.h/cpp` — reactive I/O chaining on label completion
  - `observability.h/cpp` — OBSERVE label handler (7 query types)
  - `telemetry.h/cpp` — continuous metrics publisher
  - `uri.h/cpp` — URI parser for label routing
  - `worker_manager.h/cpp` — bucket-sorted registry, tier tracking
  - `content_manager.h/cpp` — warehouse staging, small-I/O cache
  - `catalog_manager.h/cpp` — metadata, label status, file locations
  - `solver/` — Round Robin, Random, Constraint-based, MinMax DP
  - `backend/` — BackendStore concept, PosixBackend, BackendRegistry
  - `sds/` — program repository, 8 builtins, pipeline executor
  - `elastic/` — decision engine, Docker client, orchestrator (per-tier)
  - `transport/` — NATS JetStream, Redis/DragonflyDB connections
- `src/services/` — labios-dispatcher, labios-worker, labios-manager
- `src/drivers/` — POSIX intercept (LD_PRELOAD)
- `schemas/label.fbs` — FlatBuffers schema
- `tests/unit/` — 180 Catch2 tests
- `conf/profiles/` — weight profiles (low_latency, energy_savings, high_bandwidth, agentic)

## Architecture

```
Agent Frameworks (LangChain, CrewAI, custom)  |  HPC Apps (MPI/POSIX)
         |                                              |
         | SDK / LD_PRELOAD intercept                   |
         v                                              v
┌─────────────────────────────────────────────────────────────┐
│                    LABIOS Client                             │
│  Label Manager + Content Manager + Catalog Manager          │
│  Channels + Workspaces + Observability                      │
└──────────────────────────┬──────────────────────────────────┘
                           |
         NATS JetStream (label queue) + DragonflyDB (warehouse)
                           |
┌──────────────────────────v──────────────────────────────────┐
│              Label Dispatcher                                │
│  OBSERVE handler | Shuffler → Scheduler | Telemetry          │
│  Continuation processor                                      │
└──────────────────────────┬──────────────────────────────────┘
                           |
┌──────────────────────────v──────────────────────────────────┐
│              Worker Manager (leader-elected)                  │
│  Bucket-sorted registry | Per-tier elastic scaling           │
│  Commission/decommission via Docker/K8s/IPMI                 │
└──────────────────────────┬──────────────────────────────────┘
                           |
┌──────────────────────────v──────────────────────────────────┐
│              Workers (elastic pool)                           │
│  Tier 0: Databot (single I/O ops, stateless)                 │
│  Tier 1: Pipeline (SDS DAG execution, program repository)    │
│  Tier 2: Agentic (reasoning, tools, skills)                  │
│           ↓                                                  │
│  BackendRegistry → URI scheme resolution                     │
│  file:// | s3:// | vector:// | graph:// | kv:// | pfs://    │
└──────────────────────────────────────────────────────────────┘
```

Clients never talk to workers. The dispatcher is the only bridge. This invariant holds across all deployment configurations.

## Current Status (as of 2026-04-06)

353 tests (224 unit, 68 smoke, 15 kernel, 41 bench, 5 integration). Docker
Compose stack runs with NATS 2.10 (JetStream), DragonflyDB, Redis-KV, 1
dispatcher, 3 workers, 1 Worker Manager, and 1 MCP server.

**Implemented capabilities (mapped to LABIOS-SPEC.md sections):**

| Capability | Spec Section | Status |
|-----------|-------------|--------|
| Label as mutable information carrier (all accumulation fields, timestamps, score_snapshot, result) | S2 | Done |
| Shuffler (aggregation, RAW/WAW/WAR, supertasks, read-locality, aggregation metadata) | S6 | Done |
| Four scheduling policies (RR, Random, Constraint, MinMax DP) | S7.5 | Done |
| Extensible worker scoring (5 baseline + tier + skills/compute/reasoning) | S7.3 | Done |
| Intent-aware scheduling (profile adjustment per label intent) | S7.4 | Done |
| Three worker tiers (Databot/Pipeline/Agentic) | S7.1 | Done |
| URI-based routing with BackendStore concept (label-based, per spec S4.4) | S4 | Done |
| POSIX backend for file:// scheme | S4 | Done |
| KV backend for kv:// scheme (connects to user's Redis) | S4 | Done |
| SQLite backend for sqlite:// scheme | S4 | Done |
| SDS programmable pipelines (program repository, 11 builtins, DAG-aware executor) | S5 | Done |
| Channels (streaming pub/sub, backpressure, TTL, auto-destroy) | S8.2 | Done |
| Workspaces (persistent shared state with ACL and versioning) | S8.3 | Done |
| Observability labels (OBSERVE type, 8 query endpoints including data/location) | S10.2 | Done |
| Telemetry stream (continuous metrics, p50/p95/p99 latency, per-lane throughput) | S10.3 | Done |
| Per-tier elastic scaling (tier-aware, energy budget trigger) | S9 | Done |
| Continuation execution (Notify/Chain/Conditional on completion) | S2.7 | Done |
| Elastic workers via Docker Engine API | S9.3 | Done |
| POSIX intercept (LD_PRELOAD, 26 POSIX + 4 stdio entry points) | S12.1 | Done |
| Async client API (async_write, async_read, wait) | S12.2 | Done |
| Label-level API (create_label, publish) | S12.2 | Done |
| C API header (labios.h) for FFI consumers | S12.2 | Done |
| Python SDK (pybind11 bindings, all 8 API layers) | S12.2 | Done |
| Runtime config.set() for dynamic parameter adjustment | S12.2 | Done |
| Snowflake ID generation | S2.2 | Done |
| Small-I/O cache with timer-based flush | S6.1 | Done |
| DragonflyDB warehouse (~20x throughput over Redis) | Internal | Done |
| Routing, timestamps, score_snapshot populated on every label | S2.2 | Done |
| MCP server for coding agent integration (5 tools: observe, store, retrieve, process, knowledge) | S12.3 | Done |
| Vanilla-vs-LABIOS comparison benchmarks (5 scenarios) | S13 | Done |
| Codex deep review hardening (5 iterations: safety, spec compliance, coverage, perf, coherence) | QA | Done |

**Not yet implemented:**
- Additional backend adapters (vector, graph, S3, parallel FS)
- Agent-specific benchmark suite (remaining 3 benchmarks from spec S13)
- FUSE filesystem mount

## Documentation

- `docs/getting-started.md` — 5-minute quickstart
- `docs/sdk-guide.md` — full API reference (C++, Python, C, 8 layers)
- `docs/deployment.md` — Docker Compose topology, scaling, multi-node
- `docs/configuration.md` — every TOML field, weight profiles, env vars
- `docs/backends.md` — BackendStore concept, writing new backends, URI schemes
- `docs/mcp-integration.md` — MCP server setup and tool reference
- `docs/architecture.md` — complete implementation reference

## Reference

- `LABIOS-SPEC.md` — definitive specification for LABIOS 2.0 (primary design authority)
- `LABIOS-2.0.md` — constitutional document (established the rewrite)
- `docs/superpowers/specs/2026-04-06-agent-integration-design.md` — agent integration design
- `.planning/reference/original-paper/labios.md` — HPDC'19 paper
- Tag `v1.0-archive` — old 2018 prototype (reference only)
