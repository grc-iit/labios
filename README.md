# LABIOS: Label-Based I/O System

A distributed I/O system that converts I/O requests into self-describing labels for intelligent routing and processing across HPC, Big Data, and AI/Agent workloads.

**US Patent 11,630,834 B2 | NSF Award #2331480 | HPDC'19 Best Paper Nominee**

## Status

LABIOS 2.0 is a ground-up rewrite of the original research prototype. The old codebase is archived at tag `v1.0-archive`.

See `LABIOS-2.0.md` for the full design document, implementation milestones, and engineering principles.

## Quick Start

```bash
git clone https://github.com/grc-iit/labios && cd labios
docker compose up -d
```

## Architecture

```
App/Agent → LABIOS Client → Label Queue (NATS JetStream)
  → Label Dispatcher (Shuffler + Scheduler) → Workers → Storage
```

Labels are immutable, self-describing units of I/O work. Each carries: operation type, source, destination, function pointer (SDS), priority, flags, and dependency chain. The dispatcher preprocesses labels (aggregation, dependency resolution, supertask creation) before scheduling them to workers via constraint-based or dynamic programming solvers.

## Tech Stack

C++20 | FlatBuffers | NATS JetStream | Redis 7 | io_uring | Docker | Catch2 + pytest | pybind11

## Publications

1. A. Kougkas, H. Devarajan, J. Lofstead, X.-H. Sun. "LABIOS: A Distributed Label-Based I/O System." HPDC'19.
2. US Patent 11,630,834 B2. "Label-Based Data Representation I/O Process and System."

## Team

**PI:** Dr. Xian-He Sun | **Co-PI:** Dr. Anthony Kougkas | Illinois Institute of Technology

## License

BSD 3-Clause. See `COPYING`.
