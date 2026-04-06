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

## Current Status (as of 2026-04-05)

180 unit tests pass. Docker Compose stack runs with NATS 2.10 (JetStream),
DragonflyDB, 1 dispatcher, 3 workers, and 1 Worker Manager.

**Implemented capabilities (mapped to LABIOS-SPEC.md sections):**

| Capability | Spec Section | Status |
|-----------|-------------|--------|
| Label as mutable information carrier (accumulation fields, state, URIs, continuations) | S2 | Done |
| Shuffler (aggregation, RAW/WAW/WAR, supertasks, read-locality) | S6 | Done |
| Four scheduling policies (RR, Random, Constraint, MinMax DP) | S7.5 | Done |
| Extensible worker scoring (5 baseline + tier variable) | S7.3 | Done |
| Three worker tiers (Databot/Pipeline/Agentic) | S7.1 | Done |
| URI-based routing with BackendStore concept | S4 | Done |
| POSIX backend for file:// scheme | S4 | Done |
| SDS programmable pipelines (program repository, 8 builtins, executor) | S5 | Done |
| Channels (streaming pub/sub coordination) | S8.2 | Done |
| Workspaces (persistent shared state with ACL and versioning) | S8.3 | Done |
| Observability labels (OBSERVE type, 7 query endpoints) | S10.2 | Done |
| Telemetry stream (continuous metrics via NATS) | S10.3 | Done |
| Per-tier elastic scaling (tier-aware commission/decommission) | S9 | Done |
| Continuation execution (Notify/Chain/Conditional on completion) | S2.7 | Done |
| Elastic workers via Docker Engine API | S9.3 | Done |
| POSIX intercept (LD_PRELOAD) | S12.1 | Done |
| Async client API (async_write, async_read, wait) | S12.2 | Done |
| Label-level API (create_label, publish) | S12.2 | Done |
| C API header (labios.h) for FFI consumers | S12.2 | Done |
| Snowflake ID generation | S2.2 | Done |
| Small-I/O cache with timer-based flush | S6.1 | Done |
| DragonflyDB warehouse (~20x throughput over Redis) | S4 | Done |

**Not yet implemented:**
- Python SDK (pybind11 bindings)
- Agent-specific benchmark suite (8 benchmarks from spec S13)
- Additional backend implementations (S3, vector, graph)
- FUSE filesystem mount
- Configuration hot-reload

## Reference

- `LABIOS-SPEC.md` — definitive specification for LABIOS 2.0 (primary design authority)
- `LABIOS-2.0.md` — constitutional document (established the rewrite)
- `.planning/reference/original-paper/labios.md` — HPDC'19 paper
- `docs/superpowers/specs/architecture-current.md` — M0-M4 implementation snapshot
- Tag `v1.0-archive` — old 2018 prototype (reference only)
