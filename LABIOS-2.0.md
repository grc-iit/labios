# LABIOS 2.0: Constitutional Document

**Origin:** HPDC'19 Best Paper Nominee, US Patent 11,630,834 B2, NSF Award #2331480
**Author:** Anthony Kougkas, Illinois Institute of Technology
**Status:** Ground-up rewrite of the LABIOS distributed label-based I/O system

---

## 1. Why This Rewrite Exists

The current codebase is a 2018 research prototype that was migrated from Bitbucket and simplified to the point where most of the published IP was stripped out. The HPDC'19 paper describes a system with label shuffling, dependency resolution, supertasks, constraint-based scheduling, software-defined storage via function pointers on labels, elastic worker commission/decommission, configurable label granularity with small-I/O aggregation, ephemeral data-sharing rooms, and four deployment models. The code implements almost none of this. What remains is the plumbing (NATS, Memcached, cereal, MPI) with a degenerate scheduler and no label intelligence.

LABIOS 2.0 restores the full intellectual property from the paper, modernizes the implementation to C++20, and extends the architecture for a world where AI agents (not just human-written MPI programs) are the primary producers and consumers of I/O traffic.

---

## 2. The Intellectual Property: What LABIOS Actually Is

These are the ideas that the patent and paper protect. Every one of them must be implemented correctly in 2.0.

### 2.1 The Label Abstraction

A Label is a self-describing, immutable, independently executable unit of I/O work. It is not a task, not an RDD, not an object. It is a storage-independent expression of intent to operate on data.

**Label structure (from the paper):**
```
Label {
    unique_id    : uint64    // origin + nanosecond timestamp, total ordering
    type         : enum      // READ, WRITE, DELETE, FLUSH, COMPOSITE
    source       : Pointer   // memory address, file path, server IP, network port
    destination  : Pointer   // same flexibility as source
    operation    : fn_ptr    // function from shared program repository (SDS)
    flags        : bitfield  // queued, scheduled, pending, cached, invalidated
    priority     : uint8     // for priority queue ordering
    app_id       : uint32    // ownership tracking
    dependencies : vec<id>   // for supertask chains
}
```

**What was lost:** The current code has `task { task_type, task_id, publish, addDataspace }`. No function pointers, no flags, no dependency tracking, no priority, no flexible pointer types.

### 2.2 Label Granularity Control

The paper specifies configurable min/max label sizes (e.g., 64KB to 4MB) driven by hardware characteristics (memory page size, cache size, network buffer size, storage block size).

**Three decomposition modes:**
- **1-to-N splitting**: A 10MB fwrite with 1MB max label size produces 10 labels
- **N-to-1 aggregation**: Ten 100KB fwrites with 1MB min label size aggregate into 1 label (memtable/SSTable pattern)
- **1-to-1 passthrough**: For synchronous reads that bypass thresholds

**What was lost:** The code has a hardcoded `MAX_IO_UNIT = 1MB`. No min size, no aggregation, no configurability.

### 2.3 Label Shuffling and Dependency Resolution

Before scheduling, the Label Dispatcher performs a shuffling phase:

1. **Data Aggregation**: Labels targeting consecutive offsets in the same file are merged into a single larger label to preserve locality. Configurable on/off.
2. **Dependency Detection**: Read-after-write, write-after-write, and write-after-read hazards are detected per configurable granularity (per-application, per-file, per-dataset).
3. **Supertask Creation**: Dependent labels are packaged into a supertask that executes its children in strictly increasing order on a single worker.
4. **Direct Assignment**: Read labels are sent directly to the worker holding the data, bypassing the solver entirely.

**What was lost:** The code's `task_scheduler::run()` subscribes from the queue and calls `solve()` with zero preprocessing. No shuffling, no aggregation, no dependency detection, no supertasks, no read locality routing.

### 2.4 Four Scheduling Policies

| Policy | Description | Cost | Guarantees |
|---|---|---|---|
| Round Robin | Distribute to available workers cyclically | Low | Fair distribution, no optimization |
| Random Select | Uniform random to all workers (including suspended) | Low | Uniform distribution, no performance guarantees |
| Constraint-based | Request top-N scored workers from manager, distribute evenly. Weighted score selects which constraint to optimize. | Medium | QoS-aware, policy-driven |
| MinMax | Multidimensional knapsack: maximize performance, minimize energy, subject to capacity/load. Approximate DP. | High | Near-optimal assignment |

**What was lost:** The Constraint-based policy is completely absent from the code. It is the most practical of the four for production use. The MinMax (DP) solver has a degenerate value function where all tasks receive the same profit because worker energy/speed are compile-time constants.

### 2.5 Worker Score System

Each worker independently computes and publishes a composite score:

```
Score_worker(i) = Σ(j=1..5) Weight_j × Variable_j
```

| Variable | Range | Dynamic? | Source |
|---|---|---|---|
| Availability | {0, 1} | Yes | Worker state (active/suspended/busy) |
| Capacity | [0.0, 1.0] | Yes | remaining_capacity / total_capacity |
| Load | [0.0, 1.0] | Yes | queue_size / max_queue_size |
| Speed | {1, 2, 3, 4, 5} | No (init) | Storage medium bandwidth class |
| Energy | {1, 2, 3, 4, 5} | No (init) | Power wattage class |

**Weight profiles (Table 2 from paper):**
| Priority | Availability | Capacity | Load | Speed | Energy |
|---|---|---|---|---|---|
| Low latency | 0.5 | 0.0 | 0.35 | 0.15 | 0.0 |
| Energy savings | 0.0 | 0.15 | 0.20 | 0.15 | 0.5 |
| High bandwidth | 0.0 | 0.15 | 0.15 | 0.70 | 0.0 |

The Worker Manager maintains a bucket-sorted list of workers by score using approximate bin sorting for O(bucket_size) update cost.

**What was lost:** The code hardcodes WORKER_ENERGY and WORKER_SPEED as constants, making 3 of 5 score variables static and identical across all workers. Weight profiles are not configurable. Bucket sorting is not implemented.

### 2.6 Software-Defined Storage (SDS)

Labels carry function pointers from a shared program repository accessible to all workers. Example from the paper (Figure 4):

```cpp
std::function<int(vector<int>)> fn = FindMedian;
Label label = client.CreateLabel(type, src, fn, flags);
Status status = client.IPublishLabel(label);
// ... compute ...
client.WaitLabel(&status);
int median = static_cast<int>(status.data);
```

Workers execute the function on the data, return the result, and the application never touches the raw data. This enables: deduplication, sorting, compression, filtering, statistical aggregation, and custom transforms at the storage layer.

**What was lost:** Entirely absent from the code. Labels carry no operations. Workers just do read/write.

### 2.7 Content Manager and Warehouse

The Warehouse is a distributed in-memory store (currently Memcached) with:
- **Application-specific tables**: configurable by number of apps, job size, dataset size, node count
- **Ephemeral rooms**: temporary shared regions for inter-process data sharing (the key enabler for the Montage 17x speedup)
- **Small-I/O cache**: requests below threshold accumulate locally and flush as aggregated labels (memtable/SSTable pattern)
- **Configurable placement**: one hashmap per node, per application, or per group

**What was lost:** The code uses Memcached as a flat key-value store with no room abstraction, no small-I/O cache, no configurable placement.

### 2.8 Storage Malleability

Workers can be dynamically commissioned and decommissioned:
- Auto-suspend on idle queue (configurable timeout)
- Activation via IPMI (`ipmitool --power on`), SSH, or Wake-on-LAN
- Elastic allocation strategy: activate more workers for I/O bursts, suspend for compute phases
- Energy-aware: suspended workers consume ~10% power (16W vs 165W in paper's testbed)

**What was lost:** No commission/decommission logic in the code. Workers run in a permanent loop.

### 2.9 Four Deployment Models

| Model | Placement | Best for |
|---|---|---|
| I/O Accelerator | On compute nodes | Node-local I/O, Hadoop workloads, fast distributed cache |
| I/O Forwarder | On I/O forwarding nodes | Async non-blocking I/O to remote storage |
| I/O Buffering | On burst buffer nodes | Temporary storage, data sharing, in-situ analysis |
| Remote Distributed Storage | Dedicated nodes | Full scalability, independent component scaling |

**What was lost:** The code assumes a single deployment model. No configurability.

### 2.10 Storage Bridging

LABIOS can unify multiple storage namespaces by connecting to external systems (PFS, HDFS, object stores). It becomes a client to external systems and queries their metadata services directly. This enables cross-system data access without data copying.

**What was lost:** Only `posix_client.cpp` exists. No HDFS, S3, or object store connectors.

---

## 3. What Changes for 2.0: Agents as First-Class Citizens

The 2019 paper was written for a world of MPI programs and Hadoop jobs. In 2026, the fastest-growing I/O producers are AI agents: LLM inference serving, retrieval-augmented generation, model checkpointing, KV cache management, distributed tool calls, and agentic workflows that read/write files as side effects of reasoning.

### 3.1 The Agent I/O Problem

Agents produce I/O that is fundamentally different from traditional HPC or BigData:
- **Unpredictable burst patterns**: An agent decides at runtime whether to read a file, write results, or call an external tool. There is no static I/O phase.
- **Mixed granularity**: A single agent session might write a 3KB JSON response and then checkpoint a 10GB model tensor.
- **Intent-rich**: The agent knows *why* it's doing I/O (checkpoint vs. cache vs. tool output vs. final result), but current I/O APIs have no way to express that intent.
- **Latency-sensitive control path**: The agent's reasoning loop stalls while waiting for I/O. Async is not optional.
- **Concurrent multi-agent**: Thousands of agents sharing a storage cluster, each with different priorities, data isolation requirements, and QoS needs.

LABIOS' label abstraction is a natural fit because labels already carry intent, priority, operation type, and destination. The 2.0 extension makes agents the primary API consumer alongside traditional applications.

### 3.2 Agent-Native Label API

```cpp
// Native label API for agent I/O
auto client = labios::connect(config);

// Express intent directly
auto label = client.create_label({
    .type = labios::LabelType::Write,
    .source = labios::memory_ptr(buffer, size),
    .destination = labios::path("/models/checkpoint_42.pt"),
    .operation = labios::builtin::compress_lz4,  // SDS: compress at storage layer
    .flags = labios::Flags::Async | labios::Flags::HighPriority,
    .intent = labios::Intent::Checkpoint,  // NEW: semantic intent for smart scheduling
    .ttl = std::chrono::hours(24),         // NEW: ephemeral data lifetime
    .isolation = labios::Isolation::Agent,  // NEW: per-agent data isolation
});

auto status = client.publish(label);
// ... agent continues reasoning ...
client.wait(status);
```

### 3.3 New Concepts for 2.0

**Intent Tags**: Labels carry semantic intent (Checkpoint, Cache, ToolOutput, FinalResult, Intermediate, SharedState) that the dispatcher uses for placement and lifecycle decisions. Checkpoints go to durable storage. Cache goes to fast ephemeral storage. Intermediate data with TTL auto-expires.

**Agent Isolation**: Each agent (or agent group) gets its own namespace partition. Labels cannot cross isolation boundaries unless explicitly shared via ephemeral rooms (warehouse rooms from the original design).

**Priority Lanes**: The label queue is partitioned into priority lanes. Control-path I/O (agent needs data to continue reasoning) gets a dedicated fast lane. Background I/O (checkpoints, logs) uses the bulk lane. This is a direct extension of the paper's priority queue with nanosecond timestamp ordering.

**Observability Labels**: Special read-only labels that agents use to query the state of the system (worker scores, queue depths, data locations) without side effects. Enables agent-driven I/O optimization.

---

## 4. Technical Modernization

### 4.1 Language Standard: C++20

The 2018 code uses C++17 with raw `new`/`delete`, `std::system("rm -rf ...")`, `popen("du -s ...")`, `#include "knapsack.cpp"`, and `reinterpret_cast` everywhere. 

LABIOS 2.0 targets C++20. Modules are a stretch goal, not a blocker.

Core C++20 features used throughout:
- **Coroutines (`co_await`)** for async I/O without spin-polling
- **`std::jthread`** and `std::stop_token` for cooperative worker shutdown (replaces the `kill` boolean)
- **Concepts** for constraining solver, storage backend, and serializer interfaces
- **`std::format`** for all string formatting (replaces stringstream chains)
- **`std::span`** for zero-copy buffer views in the Content Manager
- **`std::chrono`** throughout (replaces custom Timer class)
- **Smart pointers exclusively**: no raw `new`/`delete`
- **`std::filesystem`** for all path operations (replaces raw string manipulation)
- **Ranges** for label collection transformations in the shuffler

Stretch goals (adopt when compiler/toolchain support stabilizes):
- **Modules** (`import labios;`) for clean API boundaries and faster compilation
- **`std::expected<T, E>`** (C++23) for error handling (use a backport or `tl::expected` until then)
- **`std::flat_map`** / **`std::flat_set`** (C++23) for cache-friendly metadata containers

### 4.2 Build System: CMake 3.25+ with Presets

```
labios/
├── CMakeLists.txt
├── CMakePresets.json            # dev, release, sanitizer, benchmark presets
├── Dockerfile                   # Multi-stage: builder → dispatcher, worker, manager, test
├── docker-compose.yml           # Full system: NATS, Redis, dispatcher, 3 workers, manager
├── cmake/
│   ├── LabiosDependencies.cmake
│   └── LabiosInstall.cmake
├── schemas/
│   └── label.fbs                # FlatBuffers schema for Label
├── src/
│   ├── labios/                  # Core library
│   │   ├── label.h / label.cpp
│   │   ├── client.h / client.cpp
│   │   ├── dispatcher.h / dispatcher.cpp    # Shuffler + Scheduler
│   │   ├── warehouse.h / warehouse.cpp      # Content Manager + Warehouse
│   │   ├── catalog.h / catalog.cpp          # Catalog Manager (metadata)
│   │   ├── worker.h / worker.cpp
│   │   ├── worker_manager.h / worker_manager.cpp
│   │   ├── admin.h / admin.cpp
│   │   ├── solver/
│   │   │   ├── solver.h           # Concept-constrained interface
│   │   │   ├── round_robin.cpp
│   │   │   ├── random.cpp
│   │   │   ├── constraint.cpp     # RESTORED from paper
│   │   │   └── minmax.cpp         # Fixed: real value function
│   │   ├── backend/
│   │   │   ├── backend.h          # Concept-constrained interface
│   │   │   ├── posix.cpp
│   │   │   └── uring.cpp          # io_uring for async I/O
│   │   ├── transport/
│   │   │   ├── nats.cpp           # NATS JetStream (label queue)
│   │   │   └── redis.cpp          # Redis 7 (warehouse + metadata)
│   │   └── sds/
│   │       ├── program_repo.cpp   # dlopen-based function loading
│   │       └── builtins.cpp       # compress_lz4, filter, deduplicate, sum, median, sort
│   ├── services/
│   │   ├── labios-dispatcher.cpp
│   │   ├── labios-worker.cpp
│   │   ├── labios-manager.cpp
│   │   └── labios-admin.cpp
│   └── drivers/
│       ├── posix_intercept.cpp    # LD_PRELOAD interception
│       └── mpi_intercept.cpp      # Optional MPI wrapper
├── include/
│   └── labios/labios.h            # C API for FFI (Python, Rust, Go agents)
├── tests/
│   ├── unit/                      # Catch2
│   ├── integration/               # Full pipeline against live NATS/Redis
│   ├── benchmark/                 # CM1, HACC, Montage, K-means, Agent pipeline
│   └── conftest.py                # pytest fixtures for Python SDK
├── conf/
│   ├── labios.toml                # Main config (TOML, typed)
│   └── profiles/
│       ├── low_latency.toml
│       ├── energy_savings.toml
│       └── high_bandwidth.toml
└── bindings/
    └── python/                    # pybind11, pip-installable
        ├── labios/__init__.py
        ├── setup.py
        └── tests/
```

### 4.3 Tech Stack (Final Decisions)

| Component | Choice | Why |
|---|---|---|
| Language | C++20 | Coroutines, jthread, concepts. Modules are optional stretch goal. |
| Build | CMake 3.25+ with presets | dev, release, sanitizer, benchmark presets |
| Serialization | FlatBuffers | Zero-copy, cross-language (Python, Rust, Go), schema evolution |
| Label queue | NATS 2.10+ with JetStream | Already in the codebase. JetStream adds exactly-once, persistence, KV store |
| Warehouse + Metadata | Redis 7 (or DragonflyDB) | Replaces Memcached. Richer data structures, pub/sub, Lua scripting |
| Async I/O | io_uring (Linux) with POSIX fallback | Replaces blocking read/write and busy-wait loops |
| Hashing | xxHash3 | Replaces cityhash. Faster, maintained, SIMD |
| Python bindings | pybind11 | Mature, well-documented, works with C++20 |
| Testing | Catch2 + pytest | Catch2 for C++, pytest for Python SDK |
| Config | TOML | Typed, unambiguous. Replaces YAML |
| Containers | Docker + Docker Compose | Development, testing, CI, and deployment |
| CI | GitHub Actions | ASan, TSan, UBSan, Catch2, pytest on every PR |
| MPI | Optional dependency | Keep for HPC benchmarks. Not required for agent workloads. |

**Replaced dependencies:**

| 2018 | 2026 |
|---|---|
| Memcached 1.0.18 | Redis 7 |
| NATS C client 3.3.0 | NATS 2.10+ JetStream |
| cereal | FlatBuffers |
| cityhash | xxHash3 |
| yaml-cpp | toml++ |
| popen("du -s") | std::filesystem::space() |
| std::system("rm -rf") | std::filesystem::remove_all() |
| raw new/delete | std::unique_ptr, object pools |

### 4.4 Async I/O: Coroutines Replace Spin-Polling

The 2018 code uses busy-wait loops for synchronization:
```cpp
// OLD: burns CPU waiting for completion
while (!data_m->exists(DATASPACE_DB, task.destination.filename, ...)) { }
```

LABIOS 2.0 uses C++20 coroutines:
```cpp
// NEW: cooperative async
auto data = co_await warehouse.get(label.destination);
```

For kernel-level async I/O, workers use `io_uring` instead of blocking POSIX read/write.

### 4.5 Testing Strategy

The 2018 code has zero tests. LABIOS 2.0 requires:

- **Unit tests** (Catch2): Label construction, shuffler logic, each solver in isolation, warehouse operations, catalog CRUD
- **Integration tests**: Full pipeline from client publish through dispatcher to worker completion, with real NATS/Redis
- **Benchmark suite**: Preserve the CM1, HACC, Montage, K-means benchmarks from the paper. Add agent-specific benchmarks (LLM checkpoint storm, KV cache thrashing, multi-agent file contention)
- **Fuzz testing**: Label serialization/deserialization, malformed label handling
- **CI**: GitHub Actions with sanitizers (ASan, TSan, UBSan) on every PR

---

## 5. Architecture: Component Map

```
┌──────────────────────────────────────────────────────────────────────┐
│                        Applications / Agents                         │
│  MPI Apps │ Spark Jobs │ LLM Inference │ Agent Frameworks │ REST API │
└─────────────────────────────┬────────────────────────────────────────┘
                              │
┌─────────────────────────────▼────────────────────────────────────────┐
│                         LABIOS Client                                │
│  ┌──────────────┐  ┌────────────────┐  ┌─────────────────────────┐  │
│  │ Label Manager │  │Content Manager │  │   Catalog Manager       │  │
│  │ - build       │  │- warehouse     │  │ - file/label metadata   │  │
│  │ - split/agg   │  │- rooms         │  │ - label status tracking │  │
│  │ - serialize   │  │- small-IO cache│  │ - location mappings     │  │
│  └──────┬───────┘  └───────┬────────┘  └────────────┬────────────┘  │
│         │ labels           │ data                    │ metadata      │
└─────────┼──────────────────┼─────────────────────────┼───────────────┘
          ▼                  ▼                         ▼
┌─────────────────┐  ┌──────────────┐          ┌──────────────┐
│  Label Queue    │  │  Warehouse   │          │  Inventory   │
│  (NATS/JetStr.) │  │  (Redis/DFL) │          │  (Redis/DFL) │
└────────┬────────┘  └──────────────┘          └──────────────┘
         │
┌────────▼─────────────────────────────────────────────────────────────┐
│                       Label Dispatcher                               │
│  ┌──────────────────┐  ┌─────────────────────────────────────────┐  │
│  │ Shuffler          │  │ Scheduler                               │  │
│  │ - aggregate       │  │ - round_robin / random / constraint /   │  │
│  │ - detect deps     │  │   minmax (real DP, not degenerate)      │  │
│  │ - build supertask │  │ - answers: how many, which, assignment  │  │
│  │ - route reads     │  │                                         │  │
│  └──────────────────┘  └─────────────────────────────────────────┘  │
└────────┬─────────────────────────────────────────────────────────────┘
         │ {worker_id → vec<label>}
┌────────▼─────────────────────────────────────────────────────────────┐
│                       Worker Manager                                 │
│  - bucket-sorted worker scores                                       │
│  - commission / decommission (IPMI, SSH, WOL)                        │
│  - per-worker queues                                                 │
│  - load balancing                                                    │
└────────┬─────────────────────────────────────────────────────────────┘
         │
┌────────▼─────────────────────────────────────────────────────────────┐
│                          Workers                                     │
│  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐                   │
│  │Worker 1 │ │Worker 2 │ │Worker 3 │ │Worker N │  (elastic pool)   │
│  │ io_uring│ │ io_uring│ │ io_uring│ │ io_uring│                   │
│  │ SDS exec│ │ SDS exec│ │ SDS exec│ │ SDS exec│                   │
│  └────┬────┘ └────┬────┘ └────┬────┘ └────┬────┘                   │
│       │           │           │           │                          │
│  ┌────▼───────────▼───────────▼───────────▼────┐                    │
│  │         Storage Backends                     │                    │
│  │  POSIX │ io_uring │ (future: S3, HDFS)      │                    │
│  └─────────────────────────────────────────────┘                    │
└──────────────────────────────────────────────────────────────────────┘
```

### 5.5 Development Environment

Every developer must be able to clone the repo and have a fully working LABIOS system in under 5 minutes via Docker Compose. No manual dependency installation. No "works on my machine." The `dependencies.sh` script in the current repo is a liability. Docker replaces it.

**Local development workflow:**
```bash
git clone https://github.com/grc-iit/labios && cd labios
docker compose up -d              # Full LABIOS system on localhost
docker compose exec test bash     # Shell into test container
./run_tests.sh                    # Unit + integration tests against live system
docker compose down               # Teardown
```

**Docker Compose services:**
```yaml
services:
  nats:
    image: nats:2.10-alpine
    command: ["--jetstream"]
  redis:
    image: redis:7-alpine
  dispatcher:
    build: {context: ., target: dispatcher}
    depends_on: [nats, redis]
  worker-1:
    build: {context: ., target: worker}
    depends_on: [nats, redis]
    environment:
      LABIOS_WORKER_ID: 1
      LABIOS_WORKER_SPEED: 5        # Fast NVMe tier
      LABIOS_WORKER_CAPACITY: 10GB
  worker-2:
    build: {context: ., target: worker}
    depends_on: [nats, redis]
    environment:
      LABIOS_WORKER_ID: 2
      LABIOS_WORKER_SPEED: 3        # SSD tier
      LABIOS_WORKER_CAPACITY: 50GB
  worker-3:
    build: {context: ., target: worker}
    depends_on: [nats, redis]
    environment:
      LABIOS_WORKER_ID: 3
      LABIOS_WORKER_SPEED: 1        # HDD tier
      LABIOS_WORKER_CAPACITY: 200GB
  manager:
    build: {context: ., target: manager}
    depends_on: [nats, redis]
  test:
    build: {context: ., target: test}
    depends_on: [dispatcher, worker-1, worker-2, worker-3, manager]
```

**Multi-stage Dockerfile:** One builder stage compiles everything, then thin runtime images for each service. The test image includes Python + pytest + the pybind11 SDK.

**Real hardware benchmarks (Chameleon / IIT SCS Lab):** Same Docker images deployed via Kubernetes or Slurm + Singularity. The container abstraction means we test the exact same binaries locally and on the cluster.

---

## 6. Implementation Milestones

Each milestone ends with a working demo runnable on a laptop. No milestone exceeds 3 weeks. If it does, split it.

### Milestone 0: Skeleton + Docker + CI (~2 weeks)

- [ ] CMake project compiles on C++20
- [ ] Multi-stage Dockerfile for each service: labios-dispatcher, labios-worker, labios-manager, labios-client
- [ ] `docker-compose.yml` starts the full system: NATS JetStream (label queue), Redis 7 (warehouse + metadata), 3 workers, 1 dispatcher, 1 manager
- [ ] GitHub Actions CI: build, sanitizers (ASan, TSan, UBSan), smoke test
- [ ] Stub implementations for all services (connect to NATS/Redis, log readiness)

**Demo:** `docker compose up` starts everything. A test client publishes one label, one worker receives it, logs confirm delivery.

### Milestone 1: Labels Flow End-to-End (~3 weeks)

- [ ] Implement the full Label struct from Section 2.1 (every field, no shortcuts)
- [ ] FlatBuffers schema and generated code for Label serialization
- [ ] Client creates labels from POSIX intercept (`LD_PRELOAD`): fwrite, fread, fopen, fclose
- [ ] Labels publish to NATS JetStream. Workers subscribe per-worker queues
- [ ] Workers execute WRITE and READ primitives: data to Redis warehouse, metadata to Redis inventory
- [ ] Completion notification flows back to client (Redis pub/sub or NATS reply)
- [ ] Catalog Manager tracks label lifecycle (queued, scheduled, executing, complete)

**Demo:** `LD_PRELOAD=liblabios_intercept.so dd if=/dev/zero of=/labios/test.dat bs=1M count=100` writes 100MB through LABIOS. Read it back. Data matches. Print throughput.

### Milestone 2: The Shuffler (~3 weeks)

The brain that the original code never shipped. Implements the Label Dispatcher's preprocessing pipeline from Section 2.3.

- [ ] **Aggregation**: merge labels targeting consecutive offsets in the same file
- [ ] **Dependency detection**: RAW, WAW, WAR hazards per-file
- [ ] **Supertask creation**: dependent labels packaged for single-worker ordered execution
- [ ] **Read-locality routing**: reads go directly to the worker holding the data, bypassing the solver
- [ ] Configurable label min/max sizes from Section 2.2 (1-to-N splitting, N-to-1 aggregation)
- [ ] Small-I/O cache in the client: requests below min threshold accumulate and flush as aggregated labels

**Demo:** Write 50 small files (10KB each) and show aggregation in dispatcher logs. Write file A then read file A and show the supertask in logs. Read a file and show it routed to the worker that holds it.

### Milestone 3: Smart Scheduling (~3 weeks)

- [ ] Restore the Constraint-based solver (completely absent from current code)
- [ ] Fix the MinMax (DP) solver: `calculate_values()` must use real per-worker speed and energy from config, not compile-time constants
- [ ] Worker scoring with all 5 variables measured or configured per-worker
- [ ] Configurable weight profiles (`low_latency.toml`, `energy_savings.toml`, `high_bandwidth.toml`) from Table 2 of the paper
- [ ] Bucket-sorted worker list in the Worker Manager (approximate bin sorting from the paper)
- [ ] Round Robin and Random solvers carried forward (clean reimplementation)

**Demo:** 3 Docker workers with different speed/capacity configs. Submit 100 labels. Show that Constraint-based routes to the best-scored workers. Switch weight profile, rerun, show different routing.

### Milestone 4: Elastic Workers (~2 weeks)

- [ ] Dynamic commission/decommission from Section 2.8
- [ ] Commission trigger: queue depth exceeds configurable threshold
- [ ] Decommission trigger: worker idle beyond timeout + queue drained
- [ ] In Docker, "commission" means spinning up a new worker container via Docker API. "Decommission" means graceful shutdown.
- [ ] Leader election for dispatcher and worker manager (NATS built-in or Raft-lite over NATS)
- [ ] Worker auto-suspend on idle, auto-resume on label arrival

**Demo:** Start with 1 worker. Flood the queue with 10,000 labels. Watch Docker scale to 3 workers. Let the queue drain. Watch 2 workers shut down.

### Milestone 5: Software-Defined Storage (~3 weeks)

- [ ] Restore SDS from Section 2.6: labels carry function references from a shared program repository
- [ ] Workers load user functions via `dlopen` (shared libraries registered in the program repo)
- [ ] Builtin transforms: `compress_lz4`, `filter`, `deduplicate`, `sum`, `median`, `sort`
- [ ] Safety: seccomp sandbox for user-provided functions (no fork, no exec, no network)
- [ ] SDS labels carry the function name + serialized arguments; the worker resolves and executes

**Demo:** Submit a label that reads 1GB of integers from the warehouse and computes the median at the worker. The client never touches the raw data. Print the result.

### Milestone 6: Warehouse Intelligence (~2 weeks)

- [ ] Ephemeral rooms from Section 2.7: temporary shared regions for inter-process data sharing
- [ ] Room lifecycle: create, publish, subscribe, drain, destroy (auto-destroy when all participants disconnect)
- [ ] Small-I/O cache integrated into Content Manager (if not completed in M2)
- [ ] Configurable warehouse placement: per-node, per-application, per-group

**Demo:** Two processes. Process A writes 1000 small records to room "stage1". Process B subscribes, reads them all, writes transformed results to room "stage2". Rooms auto-destroy when both processes finish.

### Milestone 7: Python SDK + Agent API (~3 weeks)

- [ ] pybind11 bindings: `connect`, `create_label`, `publish`, `wait`, `create_room`, `subscribe`
- [ ] Intent tags from Section 3.3: Checkpoint, Cache, ToolOutput, FinalResult, Intermediate, SharedState
- [ ] Agent Isolation: per-agent namespace partitioning (Agent A cannot read Agent B's labels unless they share a room)
- [ ] Priority Lanes: control-path I/O gets a fast lane, background I/O gets bulk lane
- [ ] Observability Labels: read-only queries for queue depth, worker scores, data locations
- [ ] `pip install labios` from the repo (setup.py / pyproject.toml)
- [ ] C API header (`include/labios/labios.h`) for FFI to Rust/Go agent runtimes

**Demo:** A Python script acting as two agents. Agent 1 writes tool results with `intent=ToolOutput`. Agent 2 reads them from a shared room. Show namespace isolation (Agent 2 cannot see Agent 1's private labels). Show priority lanes (fast-lane label completes before bulk-lane label submitted earlier).

### Milestone 8: Four Deployment Models (~2 weeks)

- [ ] From Section 2.9: Accelerator, Forwarder, Buffering, Remote
- [ ] Docker Compose profiles: `docker compose --profile accelerator up` vs `--profile remote up`
- [ ] Same workload, different model, different performance characteristics
- [ ] Per-profile TOML configs with appropriate component placement

**Demo:** Run the same 10GB write workload on accelerator mode vs. remote mode. Show the throughput difference. Explain in logs why.

### Milestone 9: Benchmarks + Paper Numbers (~3 weeks)

- [ ] Reproduce or exceed: CM1 (16x I/O speedup), HACC (6x), Montage (65% execution time reduction, 17x I/O boost)
- [ ] New benchmark: Agent pipeline. Three Python agents in a chain, passing 10GB of structured data through LABIOS rooms vs. through filesystem + Redis directly.
- [ ] All benchmarks run in Docker Compose (reproducibility) and on real hardware (IIT SCS Lab cluster or Chameleon) for publication
- [ ] Results table matching or improving the HPDC'19 paper numbers

**Demo:** Run all benchmarks, produce a results table. Run the agent benchmark, show LABIOS beats the filesystem+Redis baseline.

---

## 7. Non-Negotiable Principles

1. **Every feature in the HPDC'19 paper ships in 2.0.** The paper is the specification. If it's described there, it gets implemented. No more "oversimplified for migration" regressions.

2. **Labels are the universal abstraction.** Everything is a label. Human I/O calls become labels. Agent I/O calls become labels. Internal data movement becomes labels. Observability queries become labels. The label is the atom of the system.

3. **Fully decoupled, always.** No component assumes the presence or location of another. Clients never talk to workers. The dispatcher is the only bridge. This is the architectural invariant that enables all four deployment models.

4. **Async by default, sync by choice.** The 2019 paper showed 16x I/O speedup and 40% execution time reduction from async mode on CM1. The 2026 system must make async the default, with sync as an explicit opt-in for legacy compatibility.

5. **The scheduler is the brain.** The shuffler, dependency resolver, and solver collectively determine the system's intelligence. They must be modular (concept-constrained interfaces), testable in isolation, and swappable at runtime via configuration.

6. **No hardcoded constants for dynamic state.** Worker speed, energy, capacity, and load are always measured or configured per-worker from real system telemetry, never compile-time constants shared across all workers.

7. **Zero raw memory management.** No `new`, no `delete`, no `malloc`, no `free`, no `popen`, no `std::system`. The 2018 code uses `std::system("rm -rf ...")` for directory cleanup and `popen("du -s | awk")` for capacity checks. This is unacceptable in production-grade C++.

8. **Tests prove correctness.** No component is considered implemented until it has unit tests covering its contract. Integration tests prove end-to-end label flow. Benchmarks prove performance claims match or exceed the paper's published numbers.

9. **Every milestone ends with a demo.** If you can't demo it, it's not done.

10. **Docker Compose is the development environment.** If it doesn't work in `docker compose up`, it doesn't work.

11. **Python SDK ships at Milestone 7, not "later."** Without Python, agents can't use LABIOS. The SDK is not a nice-to-have.

12. **Benchmarks are the final milestone, not an afterthought.** We need to reproduce the paper's numbers or explain why 2.0 is different.

---

## 8. Out of Scope (Future Extensions)

The following are valuable but not part of LABIOS 2.0 core. They layer on top after the core ships.

- S3, HDFS, and other storage bridging backends (built on the stable backend interface)
- HDF5, PnetCDF, MPI-IO adapters (built on the stable POSIX intercept)
- ML-based scheduling policies (built on the stable solver interface)
- Fault tolerance beyond what NATS JetStream provides (dead letter queues, label retry, checkpoint recovery)
- TLS/mTLS for all transport (NATS 2.x supports this natively; enable when deploying outside localhost)
- Distributed tracing (OpenTelemetry integration)
- Configuration hot-reload without service restart
- Packaging for Spack, Helm, Singularity
- Rust bindings for Rust-based agent runtimes

---

## 9. Reference

- **Original Paper:** `.planning/reference/original-paper/labios.md` (HPDC'19, Kougkas et al.)
- **Patent:** US 11,630,834 B2, "Label-Based Data Representation I/O Process and System"
- **NSF Award:** #2331480
- **Prior Systems by Same Team:** Hermes (HPDC'18), IRIS (ICS'18), Harmonia (Cluster'18)
