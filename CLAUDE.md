# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What This Is

LABIOS (Label-Based I/O System) is a distributed I/O system that converts I/O requests into self-describing labels carrying metadata for intelligent routing and processing. It targets converged HPC, Big Data, and AI/Agent workloads. US Patent 11,630,834 B2, NSF Award #2331480.

This is a ground-up rewrite. The old 2018 prototype has been archived at tag `v1.0-archive`. All design authority comes from `LABIOS-2.0.md` and the original HPDC'19 paper in `.planning/reference/original-paper/`.

## Constitutional Document

**Read `LABIOS-2.0.md` before writing any code.** It contains:
- Section 2: The full IP from the HPDC'19 paper (label structure, shuffler, solvers, worker scoring, SDS, warehouse, malleability)
- Section 3: Agent-native extensions (intent tags, isolation, priority lanes)
- Section 4: Tech stack (C++20, FlatBuffers, NATS JetStream, Redis 7, io_uring, Catch2, pybind11)
- Section 5: Architecture and development environment (Docker Compose)
- Section 6: Ten milestones (M0-M9), each with a demo
- Section 7: Non-negotiable engineering principles

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

See `LABIOS-2.0.md` Section 4.2 for the full tree. Key directories:
- `src/labios/` — core library (label, client, dispatcher, warehouse, catalog, worker, solvers, backends, transport, SDS)
- `src/services/` — executable entry points (labios-dispatcher, labios-worker, labios-manager)
- `src/drivers/` — POSIX intercept (LD_PRELOAD), MPI wrapper
- `schemas/` — FlatBuffers schema for Label
- `tests/` — unit (Catch2), integration, benchmark, pytest
- `bindings/python/` — pybind11 SDK, pip-installable
- `conf/` — TOML config + scheduler weight profiles

## Architecture

```
App/Agent → Client (Label Manager + Content Manager + Catalog Manager)
  → NATS JetStream (label queue) + Redis (warehouse + inventory)
  → Dispatcher (Shuffler → Scheduler)
  → Worker Manager → Workers (io_uring + SDS execution)
  → Storage (POSIX / io_uring)
```

Clients never talk to workers. The dispatcher is the only bridge. This invariant enables all four deployment models.

## Current Status (as of 2026-04-04)

M0, M1, M2, and M3 are complete. 50+ unit tests pass. Docker Compose stack
runs with NATS 2.10 (JetStream), DragonflyDB (Redis-compatible), 1 dispatcher,
3 workers, and 1 real Worker Manager. All benchmarks verified (100MB write/read,
1000 small files, 10MB split). Scheduling demo shows different routing under
different weight profiles.

**What M2 delivered:**
- Shuffler with aggregation, RAW/WAW/WAR dependency detection, supertask creation
- Read-locality and write-locality routing in the dispatcher
- Batched catalog scheduling with Redis pipelining
- Small-I/O cache with timer-based flush in the Content Manager
- Aggregation completion fanout via NATS reply inboxes
- Snowflake-style label ID generation (41-bit ms + 10-bit node + 12-bit seq)
- DragonflyDB replaces Redis 7 for ~20x warehouse throughput
- Thread-safe RedisConnection with internal mutex

**What M3 delivered:**
- All four scheduling policies from the paper: Round Robin, Random, Constraint-based, MinMax DP
- Constraint-based solver scores workers by weight profile and distributes to top-N
- MinMax DP solver with real per-worker speed/energy profit function
- Worker score computation: all 5 variables (availability, capacity, load, speed, energy)
- Three weight profiles from Table 2: low_latency.toml, energy_savings.toml, high_bandwidth.toml
- Real Worker Manager service with NATS-based registration/deregistration
- Workers self-register on startup, deregister on shutdown
- Dispatcher dynamically queries manager for live workers per batch
- Solver selection via LABIOS_SCHEDULER_POLICY env var or TOML config

**What M4 will deliver:**
- Elastic worker commission/decommission
- Auto-suspend on idle queue with configurable timeout
- Worker activation via IPMI/SSH/Wake-on-LAN
- Energy-aware allocation strategy

## Reference

- `LABIOS-2.0.md` — constitutional document (milestones, architecture, principles)
- `.planning/reference/original-paper/labios.md` — HPDC'19 paper (the specification)
- `.planning/audits/deep-audit-2026-04-04.md` — comprehensive M0-M2 audit
- Tag `v1.0-archive` — old 2018 prototype (reference only, do not build on it)
