# LABIOS: Label-Based I/O System

A distributed I/O system that converts I/O requests into self-describing labels for intelligent routing and processing across HPC, Big Data, and AI/Agent workloads.

**US Patent 11,630,834 B2 | NSF Award #2331480 | HPDC'19 Best Paper Nominee**

## Status

LABIOS 2.0 is a ground-up rewrite of the original research prototype. The old codebase is archived at tag `v1.0-archive`. See `LABIOS-2.0.md` for the full design document, milestones, and engineering principles.

**Completed milestones:**

| Milestone | Scope | Status |
|---|---|---|
| M0 | Skeleton, Docker Compose, CI, stub services | Done |
| M1 | Labels flow end-to-end, client architecture, POSIX intercept | Done |

**Current performance (Docker Compose, localhost):**

| Benchmark | Write | Read |
|---|---|---|
| 100MB sequential (1MB chunks) | 70 MB/s | 93 MB/s |
| 10MB split (10 labels, async) | 256 MB/s | 282 MB/s |
| 1000 small files (1KB each) | 111 IOPS | 128 IOPS |

## Quick Start

```bash
git clone https://github.com/grc-iit/labios && cd labios
docker compose up -d              # NATS, Redis, dispatcher, 3 workers, manager
docker compose run --rm --entrypoint labios-demo test   # Write 100MB, read back, verify
docker compose down -v
```

Build natively (requires C++20 compiler, CMake 3.25+):

```bash
cmake --preset dev
cmake --build build/dev -j$(nproc)
ctest --test-dir build/dev -L unit
```

## Architecture

```
App/Agent
  │ POSIX calls (LD_PRELOAD) or native Client API
  ▼
LABIOS Client
  ├─ LabelManager    (split/aggregate, async publish to NATS)
  ├─ ContentManager   (warehouse staging in Redis, small-I/O cache)
  └─ CatalogManager   (label lifecycle, file metadata in Redis)
  │
  ▼                          ▼                    ▼
Label Queue              Warehouse             Inventory
(NATS JetStream)         (Redis 7)             (Redis 7)
  │
  ▼
Label Dispatcher
  ├─ Read/Write locality routing
  └─ RoundRobinSolver (constraint-based + MinMax solvers in M3)
  │
  ▼
Workers (1..N)
  ├─ Execute WRITE: warehouse → local storage
  ├─ Execute READ:  local storage → warehouse
  └─ Completion reply via NATS
```

Labels are immutable, self-describing units of I/O work. Each carries: operation type, source/destination pointers, flags, priority, dependency chain (with RAW/WAW/WAR hazard types), file key for shuffler grouping, and child label IDs for supertask composition. The dispatcher preprocesses labels before scheduling them to workers.

**Key invariant:** Clients never talk to workers. The dispatcher is the only bridge. This enables all four deployment models from the paper (Accelerator, Forwarder, Buffering, Remote Distributed Storage).

## POSIX Intercept

Transparent I/O interception via `LD_PRELOAD`:

```bash
LD_PRELOAD=liblabios_intercept.so ./your_app
```

Intercepted calls: `open`, `close`, `read`, `write`, `pread`, `pwrite`, `lseek`, `fsync`, `fdatasync`, `stat`, `fstat`, `lstat`, `unlink`, `access`, `mkdir`, `rmdir`, `rename`, `ftruncate`, `dup`, `dup2`, plus `open64`, `lseek64`, `ftruncate64` and legacy `__xstat`/`__fxstat`/`__lxstat` variants. Non-LABIOS paths (outside configured prefixes) pass through to the real filesystem.

If NATS/Redis are unavailable, the intercept retries 3 times then falls through to the real filesystem instead of crashing the application.

## Source Layout

```
src/labios/           Core library (label, client, managers, solver, transport)
src/services/         Service entry points (dispatcher, worker, manager, demo)
src/drivers/          POSIX intercept (LD_PRELOAD)
include/labios/       Public headers
schemas/              FlatBuffers schema for Label serialization
tests/unit/           Catch2 unit tests (label, config, solver, fd_table, catalog, content)
tests/integration/    Integration tests (data path, intercept, benchmark)
conf/                 TOML configuration
```

## Tech Stack

C++20 | FlatBuffers | NATS JetStream | Redis 7 | io_uring (future) | Docker Compose | Catch2 | pybind11 (M7)

## Testing

```bash
# Unit tests (no infrastructure needed)
ctest --test-dir build/dev -L unit

# Full integration tests (requires Docker Compose stack)
docker compose up -d
docker compose run --rm --entrypoint labios-data-path-test test
docker compose run --rm --entrypoint labios-benchmark-test test
docker compose run --rm --entrypoint sh test \
  -c "LD_PRELOAD=/usr/local/lib/liblabios_intercept.so labios-intercept-test"
docker compose down -v
```

## Publications

1. A. Kougkas, H. Devarajan, J. Lofstead, X.-H. Sun. "LABIOS: A Distributed Label-Based I/O System." HPDC'19.
2. US Patent 11,630,834 B2. "Label-Based Data Representation I/O Process and System."

## Team

**PI:** Dr. Xian-He Sun | **Co-PI:** Dr. Anthony Kougkas | Illinois Institute of Technology

## License

BSD 3-Clause. See `COPYING`.
