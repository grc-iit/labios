# LABIOS 2.0 Architecture

LABIOS is the first agent I/O runtime. It converts all I/O into self-describing
labels that flow through a distributed pipeline of shufflers, schedulers, and
workers. Each component enriches the label as it passes through. The label is the
information highway, the state machine, and the audit trail. US Patent
11,630,834 B2. NSF Award #2331480. HPDC'19 Best Paper Nominee.

This document describes the system as built. Authoritative source:
[LABIOS-SPEC.md](../LABIOS-SPEC.md).

---

## System Topology

```
Agent Frameworks (LangChain, CrewAI, custom)  |  HPC Apps (MPI, POSIX)
         |                                              |
         | SDK / labios.h (C FFI)     LD_PRELOAD        |
         v                            intercept         v
┌─────────────────────────────────────────────────────────────┐
│                    LABIOS Client Library                     │
│                                                             │
│  Label Manager          Content Manager      Catalog Manager│
│  split / aggregate      warehouse staging    label status   │
│  snowflake IDs          small-I/O cache      file→worker map│
│                                                             │
│  Channels               Workspaces           Observability  │
│  streaming pub/sub      persistent ACL       OBSERVE labels │
└──────────────────────────┬──────────────────────────────────┘
                           |
         NATS JetStream (labels)  +  DragonflyDB (data + metadata)
                           |
┌──────────────────────────v──────────────────────────────────┐
│                    Label Dispatcher                          │
│                                                             │
│  Batch collector (size=100, timeout=50ms)                   │
│      ↓                                                      │
│  OBSERVE handler (inline, no shuffle)                       │
│      ↓                                                      │
│  Shuffler → aggregation, RAW/WAW/WAR, supertasks, locality  │
│      ↓                                                      │
│  Scheduler → RoundRobin | Random | Constraint | MinMax DP   │
│      ↓                                                      │
│  Continuation processor (Notify | Chain | Conditional)      │
│  Telemetry publisher (throughput, latency, utilization)      │
└──────────────────────────┬──────────────────────────────────┘
                           |
┌──────────────────────────v──────────────────────────────────┐
│              Worker Manager (leader-elected)                  │
│                                                             │
│  Bucket-sorted registry (5 buckets by composite score)      │
│  Per-tier queries: Databot | Pipeline | Agentic             │
│  Elastic orchestrator: commission / decommission / resume    │
│  Docker Engine API for container lifecycle                   │
└──────────────────────────┬──────────────────────────────────┘
                           |
┌──────────────────────────v──────────────────────────────────┐
│              Workers (elastic pool)                           │
│                                                             │
│  Tier 0  Databot     single I/O ops, stateless              │
│  Tier 1  Pipeline    SDS DAG execution, program repository   │
│  Tier 2  Agentic     reasoning, tools, inference             │
│                                                             │
│  BackendRegistry → URI scheme resolution                     │
│  file:// | s3:// | vector:// | graph:// | kv:// | pfs://    │
└──────────────────────────────────────────────────────────────┘
```

Clients never talk to workers. The dispatcher is the only bridge. This
invariant holds across all deployment configurations.

---

## The Label

Every I/O operation in LABIOS is a label. A label is a mutable, self-describing
record that accumulates metadata as it flows through the system. The design
follows a residual stream model: each runtime component reads what it needs and
writes what it knows, then passes the label forward.

### Anatomy

```
┌─────────────── Origin (sealed at creation) ──────────────────────────┐
│ id            uint64     Snowflake: 41-bit ms + 10-bit node + 12-seq │
│ app_id        uint32     Agent or application identity               │
│ type          enum       Read | Write | Delete | Flush | Composite   │
│                          | Observe                                   │
│ operation     string     Semantic operation name                     │
│ source        variant    MemoryPtr | FilePath | NetworkEndpoint      │
│ destination   variant    MemoryPtr | FilePath | NetworkEndpoint      │
│ source_uri    string     URI-addressed source (file://, s3://, ...)  │
│ dest_uri      string     URI-addressed destination                   │
│ priority      uint8      0-255, higher is more urgent                │
│ intent        enum       None | Checkpoint | Cache | ToolOutput      │
│                          | FinalResult | Intermediate | SharedState  │
│                          | Embedding | ModelWeight | KVCache         │
│                          | ReasoningTrace                            │
│ isolation     enum       None | Agent | Workspace | Global           │
│ durability    enum       Ephemeral | Durable                         │
│ version       uint64     Monotonic conflict detection                │
│ ttl_seconds   uint32     Auto-expire for ephemeral data              │
│ pipeline      Pipeline   SDS DAG of transformation stages            │
│ continuation  struct     on_complete: Notify | Chain | Conditional   │
│ data_size     uint64     Payload byte count                         │
│ reply_to      string     NATS reply inbox for completion delivery   │
│ flags         bitmask    Queued | Scheduled | Pending | Cached       │
│                          | Invalidated | Async | HighPrio            │
└──────────────────────────────────────────────────────────────────────┘

┌─────────────── Accumulation (written by runtime) ────────────────────┐
│ dependencies  vector     RAW / WAW / WAR hazard records              │
│ routing       struct     worker_id + scheduling policy               │
│ children      vector     Supertask member label IDs                  │
│ hops          vector     Component name + timestamp audit trail      │
│ file_key      string     Normalized path for shuffler grouping       │
└──────────────────────────────────────────────────────────────────────┘

┌─────────────── State (updated during execution) ─────────────────────┐
│ status        enum       Created → Queued → Shuffled → Scheduled     │
│                          → Executing → Complete | Failed             │
│ created_us    uint64     Creation timestamp (microseconds)           │
│ completed_us  uint64     Completion timestamp (microseconds)         │
└──────────────────────────────────────────────────────────────────────┘
```

### Serialization

Labels are serialized with FlatBuffers (`schemas/label.fbs`). The serializer
uses a thread-local `FlatBufferBuilder` and conditional field writing to avoid
allocations on hot paths.

| Operation           | Latency |
|---------------------|---------|
| serialize_label     | ~155 ns |
| deserialize_label   | ~76 ns  |
| Pipeline setup      | ~278 ns |

### ID Generation

Snowflake algorithm. 41 bits for millisecond timestamp (~69 years), 10 bits for
node identity (app_id XOR random), 12 bits for per-millisecond sequence counter
(4096 IDs per ms per node). No coordination required.

---

## Client API

The client presents a layered API surface. Each layer builds on the one below.

### Layer 1: Synchronous Convenience

```cpp
auto client = labios::connect(labios::load_config("conf/labios.toml"));

client.write("/data/output.dat", buffer);
auto data = client.read("/data/output.dat", 0, size);
```

### Layer 2: Asynchronous I/O

```cpp
auto s1 = client.async_write("/data/step1.dat", buf1);
auto s2 = client.async_write("/data/step2.dat", buf2);
// ... application continues computing ...
client.wait(s1);
client.wait(s2);
```

### Layer 3: Label-Level Control

```cpp
auto label = client.create_label({
    .type = labios::LabelType::Write,
    .source = labios::memory_ptr(buffer, size),
    .destination = labios::file_path("/data/output.dat"),
    .flags = labios::LabelFlags::Async,
    .priority = 5,
    .intent = labios::Intent::Checkpoint,
});
auto status = client.publish(label, data_span);
client.wait(status);
```

### Layer 4: URI-Based I/O

```cpp
client.write_to("file:///data/output.dat", data);
auto result = client.read_from("s3://bucket/key", size);
```

### Layer 5: Intent-Driven and Pipelines

```cpp
client.write_with_intent("/data/model.bin", weights, Intent::ModelWeight, 10);

client.execute_pipeline(
    "file:///raw/input.dat",
    "file:///processed/output.dat",
    pipeline,
    Intent::Intermediate
);
```

### Layer 6: Channels (Streaming Coordination)

```cpp
client.create_channel("embeddings", /*ttl=*/300);
client.publish_to_channel("embeddings", embedding_data);
client.subscribe_to_channel("embeddings", [](const labios::ChannelMessage& msg) {
    // msg.sequence, msg.label_id, msg.data
});
```

### Layer 7: Workspaces (Persistent Shared State)

```cpp
client.create_workspace("shared-context", /*ttl=*/3600);
client.workspace_put("shared-context", "config", config_bytes);
auto val = client.workspace_get("shared-context", "config");
client.workspace_grant("shared-context", other_agent_id);
```

### Layer 8: Observability

```cpp
std::string json = client.observe("observe://workers/scores");
std::string health = client.observe("observe://system/health");
```

Seven query endpoints: `queue/depth`, `workers/scores`, `workers/count`,
`system/health`, `channels/list`, `workspaces/list`, `config/current`.

### Transparent POSIX Intercept

```bash
LD_PRELOAD=liblabios_intercept.so \
  LABIOS_NATS_URL=nats://localhost:4222 \
  LABIOS_REDIS_HOST=localhost \
  ./my_simulation
```

Intercepts 26 POSIX entry points: `open`, `open64`, `close`, `read`, `write`,
`pread`, `pwrite`, `lseek`, `lseek64`, `fsync`, `fdatasync`, `ftruncate`,
`ftruncate64`, `mkdir`, `rmdir`, `rename`, `unlink`, `access`, `stat`, `fstat`,
`lstat`, `dup`, `dup2`, and legacy glibc wrappers `__xstat`, `__fxstat`,
`__lxstat`. Falls back to the real filesystem if LABIOS initialization fails.

### C API (FFI)

```c
#include <labios/labios.h>

labios_client_t client;
labios_connect("nats://localhost:4222", "localhost", 6379, &client);
labios_async_write(client, "/data/out.dat", buf, size, 0, &status);
labios_wait(status);
labios_disconnect(client);
```

---

## Write Path

```
client.write("/file.dat", 1MB_buffer)
     │
     ├─1─► DragonflyDB: SET labios:data:<label_id> = 1MB      stage
     ├─2─► NATS: publish "labios.labels" (FlatBuffers, ~200B)  enqueue
     │
     ▼
Dispatcher: batch accumulation (up to 100 labels or 50ms)
     │
     ├─3─► Shuffler: aggregation + dependency check
     ├─4─► Scheduler: pick target worker
     ├─5─► NATS: publish "labios.worker.<id>"                  route
     │
     ▼
Worker:
     │
     ├─6─► DragonflyDB: GET labios:data:<label_id>             retrieve
     ├─7─► Backend: write to storage (file://, s3://, etc.)    execute
     ├─8─► DragonflyDB: DEL labios:data:<label_id>             cleanup
     ├─9─► NATS: publish completion to reply inbox              complete
     │
     ▼
Client: receives completion, write() returns
```

Total hops: 9 (4 NATS, 3 DragonflyDB, 1 backend, 1 shuffler).

## Read Path

```
client.read("/file.dat", 0, 1MB)
     │
     ├─1─► NATS: publish "labios.labels" (read label)          enqueue
     │
     ▼
Dispatcher:
     │
     ├─2─► Catalog: lookup which worker holds /file.dat
     ├─3─► Route to holding worker (read-locality)              direct
     ├─4─► NATS: publish "labios.worker.<id>"
     │
     ▼
Worker:
     │
     ├─5─► Backend: read from storage                           execute
     ├─6─► DragonflyDB: SET labios:data:<label_id> = 1MB       stage
     ├─7─► NATS: publish completion                             complete
     │
     ▼
Client:
     │
     ├─8─► DragonflyDB: GET labios:data:<label_id>             retrieve
     └───► return data to application
```

---

## Dispatcher

The dispatcher is the orchestration center. It receives all labels from all
clients, enriches them through the shuffler, schedules them to workers, and
handles continuations after completion.

### Batch Processing

Labels arrive on the `labios.labels` NATS subject. A collector thread
accumulates them into batches of up to `batch_size` (default 100) or until
`batch_timeout_ms` (default 50ms) elapses. Each batch is processed as a unit.

### OBSERVE Labels

Labels with `type == Observe` skip the shuffler entirely. The dispatcher handles
them inline by querying the requested system state and publishing the JSON
result back to the client's reply inbox. If the OBSERVE label carries a
continuation, it is processed after the query completes.

### Shuffler

The shuffler enriches each batch through three stages:

1. **Aggregation**: consecutive writes to the same file are merged into a single
   label. A reply fanout map preserves completion delivery to all original
   clients.

2. **Dependency detection**: the shuffler identifies RAW (Read-After-Write),
   WAW (Write-After-Write), and WAR (Write-After-Read) hazards by consulting
   the catalog for file-to-worker location mappings. Granularity is configurable
   (`per-file` or `per-region`).

3. **Supertask formation**: dependent labels are grouped under a composite parent
   label. The children are packed into a Redis key
   (`labios:supertask:<parent_id>`) and the parent is routed to a single worker
   that executes the group atomically.

The shuffler output is a `ShuffleResult` containing three sets:

| Set             | Routing Strategy                        |
|-----------------|-----------------------------------------|
| `direct_route`  | Read-locality, sent to the data holder  |
| `supertasks`    | Composite, sent to solver for placement |
| `independent`   | No dependencies, sent to solver         |

### Scheduling Policies

Four solver implementations satisfy the `Solver` concept:

| Policy         | Class              | Complexity          | Use Case                     |
|----------------|--------------------|---------------------|------------------------------|
| Round-Robin    | `RoundRobinSolver` | O(1)                | Fair distribution, default   |
| Random         | `RandomSolver`     | O(1)                | Uneven workloads             |
| Constraint     | `ConstraintSolver` | O(labels x workers) | Weighted scoring by profile  |
| MinMax DP      | `MinMaxSolver`     | O(labels x workers) | Balance I/O vs energy        |

The constraint solver uses a `WeightProfile` that assigns weights to six
scoring variables: availability, capacity, load, speed, energy, and tier.
Profiles are defined in TOML files under `conf/profiles/`.

### Continuation Processor

After a label completes, the dispatcher checks its `continuation` field:

| Kind          | Behavior                                       |
|---------------|------------------------------------------------|
| `None`        | No post-processing                             |
| `Notify`      | Publish completion data to a named channel     |
| `Chain`       | Decode template from `chain_params`, create a new label, re-publish to `labios.labels` |
| `Conditional` | Evaluate expression against completion fields; if true, create chained label |

Conditional expressions support `==`, `!=`, `>`, `<` operators on fields:
`status`, `data_size`, `error`, `label_id`. Compound conditions use `&&`.

### Telemetry

The `TelemetryPublisher` runs in a background `jthread` and publishes metrics
every 2 seconds to the `labios.telemetry` NATS subject: labels dispatched,
labels completed, average latency, and a snapshot of all worker scores.

---

## Worker Manager

The worker manager maintains a real-time registry of all workers and their
scores. It runs as a separate process (`labios-manager`) and communicates over
NATS.

### Worker Scoring

Each worker reports five dynamic variables plus a static tier assignment:

| Variable     | Range    | Description                  |
|--------------|----------|------------------------------|
| availability | 0 or 1   | Currently accepting labels   |
| capacity     | [0, 1]   | Remaining / total storage    |
| load         | [0, 1]   | Queue size / max queue       |
| speed        | [1, 5]   | Bandwidth class (normalized) |
| energy       | [1, 5]   | Power draw (normalized)      |
| tier         | [0, 2]   | Databot=0, Pipeline=1, Agentic=2 |

Composite score:

```
Score = Σ(weight_j * variable_j)  for j in {availability, capacity, 1-load, speed/5, energy/5, tier/2}
```

Weights come from a `WeightProfile`. Four profiles ship with LABIOS:
`low_latency`, `high_bandwidth`, `energy_savings`, and `agentic`.

### Bucket-Sorted Registry

Workers are organized into 5 buckets by score range ([0.0-0.2], [0.2-0.4], ...,
[0.8-1.0]). Score updates move workers between buckets. Top-N queries iterate
from the highest bucket downward. The registry also supports per-tier queries
for elastic scaling decisions.

### Three Worker Tiers

| Tier | Name     | Capabilities                        | SDS Support |
|------|----------|-------------------------------------|-------------|
| 0    | Databot  | Single I/O operations, stateless    | Rejected    |
| 1    | Pipeline | SDS DAG execution, program repo     | Full        |
| 2    | Agentic  | Reasoning, tool use, inference      | Full        |

Workers declare their tier at registration. The scheduler considers tier
compatibility when routing labels that carry SDS pipelines.

---

## Elastic Scaling

The elastic subsystem commissions and decommissions workers based on queue
pressure. It is tier-aware: under SDS pipeline pressure, it commissions
Pipeline workers; under bulk I/O pressure, it commissions Databots.

### Decision Engine

A pure function `evaluate_tiered()` takes a `TieredSnapshot` and returns a
`ScaleDecision`:

```
Input:
  queue breakdown  (total, with_pipeline, observe_count)
  pressure count   (consecutive batches exceeding threshold)
  per-tier state   (active, min, max, idle IDs, suspended IDs)
  cooldown state   (last commission time, cooldown duration)

Output:
  action           None | Commission | Decommission | Resume
  target_tier      Databot | Pipeline | Agentic
  target_worker_id worker ID (for decommission/resume)
  reason           human-readable justification
```

### Orchestrator

The `Orchestrator<Runtime>` template accepts any type satisfying the
`ContainerRuntime` concept:

```cpp
template<typename T>
concept ContainerRuntime = requires(T rt, const ContainerSpec& spec, const std::string& id) {
    { rt.create_and_start(spec) } -> std::same_as<std::string>;
    { rt.stop_and_remove(id) };
};
```

The `DockerClient` implementation communicates with the Docker Engine API over a
Unix socket. Commissioned containers receive environment variables specifying
NATS URL, Redis host, worker ID, speed, energy, capacity, and tier.

Workers self-suspend after an idle timeout and can be resumed via a NATS command
from the orchestrator.

---

## SDS Programmable Pipelines

SDS (Software-Defined Storage) enables data transformations to execute inside
the I/O path. Labels carry a `Pipeline` of stages. Only Tier 1 (Pipeline) and
Tier 2 (Agentic) workers execute pipelines; Tier 0 (Databot) workers reject
them.

### Pipeline Definition

```cpp
struct PipelineStage {
    std::string operation;     // "builtin://compress_rle" or "repo://custom"
    std::string args;          // stage-specific arguments
    int input_stage = -1;      // -1 = label source, >=0 = output of stage[N]
    int output_stage = -1;     // -1 = write to dest, >=0 = input to stage[N]
};
```

### Builtins

| Function                    | Purpose                          |
|-----------------------------|----------------------------------|
| `builtin://identity`        | Pass-through (testing)           |
| `builtin://sort_uint64`     | Sort uint64 arrays               |
| `builtin://sum_uint64`      | Sum uint64 arrays                |
| `builtin://truncate`        | Slice to N bytes                 |
| `builtin://sample`          | Random sample                    |
| `builtin://filter_bytes`    | Filter bytes by predicate        |
| `builtin://compress_rle`    | Run-length encoding compression  |
| `builtin://decompress_rle`  | Run-length encoding decompression|

The `ProgramRepository` holds all registered functions (builtins loaded at
construction, user-defined via `register_function()`). The executor chains
stages: each stage's output becomes the next stage's input.

---

## Channels and Workspaces

Two coordination primitives serve multi-agent workflows.

### Channels (Streaming)

Named pub/sub streams backed by DragonflyDB (data) and NATS (notifications).
Each message gets a monotonically increasing sequence number. Channels support
TTL-based retention, drain (stop + wait), and auto-destroy on last disconnect.

Redis key pattern: `labios:channel:{name}:{seq}`
NATS subject: `labios.channel.{name}`

### Workspaces (Persistent Shared State)

Named key-value regions with per-key versioning, access control lists, and TTL.
Agents call `workspace_put()` to write and `workspace_get()` to read. Every
write increments the version counter. Access is granted per-agent via
`workspace_grant()`. Prefix-filtered listing enables namespace conventions.

Redis key patterns:
- Data: `labios:ws:{name}:{key}`
- Version: `labios:ws:{name}:{key}:v{N}`
- Metadata: `labios:ws:{name}:_meta:{key}`
- Index: `labios:ws:{name}:_index`

---

## URI Routing and Backends

Labels carry `source_uri` and `dest_uri` fields that address storage through URI
schemes. The `BackendRegistry` maps schemes to implementations via a C++20
`BackendStore` concept:

```cpp
template<typename B>
concept BackendStore = requires(B b, std::string_view path,
    uint64_t offset, uint64_t length, std::span<const std::byte> data) {
    { b.put(path, offset, data) } -> std::same_as<BackendResult>;
    { b.get(path, offset, length) } -> std::same_as<BackendDataResult>;
    { b.del(path) } -> std::same_as<BackendResult>;
    { b.scheme() } -> std::same_as<std::string_view>;
};
```

Backends register with the registry. Workers resolve URIs at execution time and
dispatch to the appropriate backend. The `PosixBackend` handles `file://`
schemes. Additional backends (S3, vector, graph) plug in through the same
concept.

URI structure: `scheme://authority/path?query`

Supported schemes (current and planned):

| Scheme     | Backend        | Status       |
|------------|----------------|--------------|
| `file://`  | PosixBackend   | Implemented  |
| `s3://`    | S3Backend      | Planned      |
| `vector://`| VectorBackend  | Planned      |
| `graph://` | GraphBackend   | Planned      |
| `kv://`    | KVBackend      | Planned      |
| `observe://`| ObserveHandler| Implemented  |

---

## Transport Layer

### NATS JetStream

All label routing flows through NATS. Key subjects:

| Subject                     | Direction      | Payload               |
|-----------------------------|----------------|-----------------------|
| `labios.labels`             | Client → Disp  | Serialized label      |
| `labios.worker.{id}`        | Disp → Worker  | Serialized label      |
| `{reply_inbox}`             | Worker → Client| Serialized completion |
| `labios.manager.workers`    | Disp → Manager | Worker list request   |
| `labios.queue.depth`        | Disp → Manager | Queue depth telemetry |
| `labios.telemetry`          | Disp → Any     | Metrics snapshot      |
| `labios.channel.{name}`     | Client ↔ Client| Channel notifications |

### DragonflyDB (Redis Wire-Compatible)

DragonflyDB serves as both the data warehouse and metadata store. It provides
~20x throughput over Redis for the concurrent access patterns LABIOS generates.

Key patterns:

| Pattern                            | Purpose                     | Type        |
|------------------------------------|-----------------------------|-------------|
| `labios:data:{label_id}`           | Staged label data           | Binary blob |
| `labios:catalog:{label_id}`        | Label metadata              | Hash        |
| `labios:location:{filepath}`       | Whole-file worker mapping   | String      |
| `labios:olocation:{filepath}`      | Per-offset worker mapping   | Sorted set  |
| `labios:filemeta:{filepath}`       | File size, mtime, exists    | Hash        |
| `labios:supertask:{id}`            | Packed child labels         | Binary blob |
| `labios:channel:{name}:{seq}`      | Channel message data        | Binary blob |
| `labios:ws:{name}:{key}`           | Workspace data              | Binary blob |
| `labios:ready:{service}`           | Service readiness flag      | String      |
| `labios:queue:depth`               | Queue depth snapshot        | String      |
| `labios:observe:{id}`              | Observability query result  | String      |

---

## Content Manager

The content manager handles data staging and small-I/O optimization.

### Warehouse

Data flows through DragonflyDB as a staging area. On writes, the client stages
data under `labios:data:{label_id}` before publishing the label. The worker
retrieves the data, executes the I/O, and deletes the staging key. On reads, the
flow reverses.

### Small-I/O Cache

Writes below `label_min_size` (default 64KB) accumulate in a per-file-descriptor
cache. The cache flushes when the total exceeds the threshold or when a timer
fires (default 500ms). Flushed regions are promoted to full labels and published
through the normal pipeline.

---

## Catalog Manager

The catalog tracks label lifecycle and file location mappings.

### Label Lifecycle

Each label passes through a state machine stored in Redis:

```
Created → Queued → Shuffled → Scheduled → Executing → Complete | Failed
```

The catalog stores flags, error details, and worker assignments for each label.
Batch scheduling updates are pipelined for efficiency.

### Location Tracking

Two granularities:

1. **Whole-file**: `set_location(path, worker_id)` / `get_location(path)`.
   Used by the shuffler for read-locality routing.

2. **Per-offset**: `set_location(path, offset, length, worker_id)` /
   `get_location(path, offset, length)`. Stored as a sorted set with offset as
   the score. Enables fine-grained chunk-to-worker mapping for large files.

### File Metadata

`track_open`, `track_write`, `track_unlink`, `track_truncate` maintain file
existence, size, and modification time. This metadata enables the shuffler to
detect dependencies without touching the filesystem.

---

## Deployment

### Docker Compose (Default)

```
docker compose up -d       # Full system on localhost
docker compose down -v     # Teardown with volume cleanup
```

Eight services, all health-checked:

| Service      | Image                                          | Ports          | Role                      |
|--------------|------------------------------------------------|----------------|---------------------------|
| nats         | nats:2.10-alpine                               | 4222, 8222     | Message broker, JetStream |
| redis        | docker.dragonflydb.io/dragonflydb/dragonfly    | 6379           | Warehouse + metadata      |
| dispatcher   | labios-dispatcher (built)                      | internal       | Label routing             |
| worker-1     | labios-worker (built)                          | internal       | speed=5, energy=1, 10GB   |
| worker-2     | labios-worker (built)                          | internal       | speed=3, energy=3, 50GB   |
| worker-3     | labios-worker (built)                          | internal       | speed=1, energy=5, 200GB  |
| manager      | labios-manager (built)                         | internal       | Worker registry           |
| test         | labios-test (built)                            | internal       | Smoke + integration tests |

Each worker has an isolated data volume (`/labios/data`). The multi-stage
Dockerfile builds all binaries in a single builder stage, then copies each
service binary into a minimal Debian runtime image.

### Native Build

```bash
cmake --preset dev
cmake --build build/dev -j$(nproc)
ctest --test-dir build/dev
```

### Configuration

All settings live in `conf/labios.toml` with environment variable overrides:

| Category    | Key Settings                                                  |
|-------------|---------------------------------------------------------------|
| Transport   | `nats.url`, `redis.host`, `redis.port`                        |
| Label       | `label.min_size` (64KB), `label.max_size` (1MB)              |
| Dispatcher  | `batch_size` (100), `batch_timeout_ms` (50), `aggregation_enabled`, `dep_granularity` |
| Scheduler   | `policy` (round-robin), `profile_path`                        |
| Elastic     | `enabled`, `min_workers`, `max_workers`, `pressure_threshold`, `eval_interval_ms`, per-tier min/max |
| Cache       | `flush_interval_ms` (500), `default_read_policy` (read-through) |
| Worker      | `id`, `speed`, `energy`, `capacity`, `tier`                   |
| Intercept   | `prefixes` (["/labios"])                                      |

---

## Source Layout

```
src/labios/                          Core library
  label.h/cpp                        Label struct, FlatBuffers serialization, snowflake IDs
  client.h/cpp                       Client API (all 8 layers)
  session.h/cpp                      Session lifecycle (NATS + Redis + managers)
  label_manager.h/cpp                Label creation, splitting, publishing
  shuffler.h/cpp                     Aggregation, dependency detection, supertasks
  channel.h/cpp                      Streaming pub/sub channels
  workspace.h/cpp                    Persistent shared state with ACL and versioning
  continuation.h/cpp                 Reactive I/O chaining (Notify/Chain/Conditional)
  observability.h/cpp                OBSERVE label handler, 7 query endpoints
  telemetry.h/cpp                    Continuous metrics publisher
  uri.h/cpp                          URI parser for label routing
  config.h/cpp                       TOML + environment variable configuration
  worker_manager.h/cpp               Bucket-sorted registry, tier tracking
  content_manager.h/cpp              Warehouse staging, small-I/O cache
  catalog_manager.h/cpp              Metadata, label status, file locations
  solver/                            Scheduling policies
    solver.h                         Solver concept + WeightProfile + WorkerTier
    round_robin.h/cpp                Stateful round-robin
    random.h/cpp                     Randomized selection
    constraint.h/cpp                 Weighted scoring
    minmax.h/cpp                     Knapsack approximation (Bertsimas & Demir 2002)
  backend/                           Storage abstraction
    backend.h                        BackendStore concept, AnyBackend type erasure
    posix_backend.h/cpp              POSIX filesystem (file:// scheme)
    registry.h/cpp                   Scheme-to-backend registry
  sds/                               Programmable data pipelines
    types.h/cpp                      Pipeline, PipelineStage, StageResult
    program_repo.h/cpp               Function registry with builtins
    executor.h/cpp                   DAG executor
    builtins.cpp                     8 built-in transformations
  elastic/                           Auto-scaling
    decision_engine.h/cpp            Pure decision function (evaluate_tiered)
    docker_client.h/cpp              Docker Engine API over Unix socket
    orchestrator.h/cpp               Template<ContainerRuntime>, commission/decommission
  transport/                         Infrastructure
    nats.h/cpp                       NATS JetStream client wrapper
    redis.h/cpp                      DragonflyDB client (pipeline, sorted sets, scans)
  adapter/                           POSIX intercept support
    adapter.h                        IOAdapter concept
    posix_adapter.h/cpp              Syscall → label conversion
    fd_table.h/cpp                   File descriptor tracking

src/services/                        Standalone processes
  labios-dispatcher.cpp              Batch processor, label routing
  labios-worker.cpp                  Label executor, backend dispatch
  labios-manager.cpp                 Worker registry, elastic orchestrator
  labios-demo.cpp                    Demo application

src/drivers/                         Interposition
  posix_intercept.cpp                LD_PRELOAD library (26 entry points)

schemas/
  label.fbs                          FlatBuffers schema (27 label fields)

include/labios/
  labios.h                           C API header for FFI consumers

conf/
  labios.toml                        Default configuration
  profiles/                          Weight profiles
    low_latency.toml
    high_bandwidth.toml
    energy_savings.toml
    agentic.toml
```

---

## Test Suite

295 tests across four categories. All pass.

| Category    | Tests | Scope                                          |
|-------------|-------|------------------------------------------------|
| unit        | 192   | Every component in isolation                   |
| smoke       | 62    | Live cluster integration (NATS, Redis, workers)|
| kernel      | 15    | Science application replays (CM1, HACC, Montage, K-means) |
| bench       | 25    | Agent I/O benchmarks (coding agent, RAG, checkpoint storm, multi-agent collab, cross-backend ETL, agentic worker, scale adaptation) |
| integration | 1     | Elastic scaling under sustained load           |

```bash
ctest --test-dir build/dev              # All 295
ctest --test-dir build/dev -L unit      # Unit only
ctest --test-dir build/dev -L kernel    # Science kernels
ctest --test-dir build/dev -L bench     # Agent benchmarks
```

---

## Design Principles

**Labels are the information highway.** Every component reads what it needs and
writes what it knows. The label accumulates context as it flows. No separate
control channel exists.

**Triple decoupling.** Instruction is decoupled from data (labels describe
intent; data flows separately through the warehouse). Production is decoupled
from execution (agents create labels; workers execute them asynchronously).
Scheduling is decoupled from storage (the scheduler routes to workers
independent of backend type).

**Clients never talk to workers.** The dispatcher is the only bridge. This
invariant simplifies security, scaling, and debugging.

**Concepts over inheritance.** `BackendStore`, `Solver`, `ContainerRuntime`, and
`WorkerManager` are C++20 concepts. New implementations satisfy the concept and
plug in without modifying existing code.

**Cooperative shutdown.** All background threads use `std::jthread` with
`std::stop_token`. No kill booleans, no signal handlers, no forced termination.

**Scale-adaptive.** The same codebase runs on a laptop (1 worker, no elastic) and
a cluster (100+ workers, per-tier elastic scaling). Configuration controls the
deployment model; the architecture does not change.

---

## Tech Stack

| Component       | Choice                              |
|-----------------|-------------------------------------|
| Language        | C++20 (coroutines, jthread, concepts) |
| Build           | CMake 3.25+ with presets            |
| Serialization   | FlatBuffers                         |
| Label queue     | NATS 2.10+ with JetStream           |
| Warehouse       | DragonflyDB (Redis 7 wire-compatible)|
| Async I/O       | io_uring with POSIX fallback        |
| Hashing         | xxHash3                             |
| Python bindings | pybind11 (planned)                  |
| Testing         | Catch2 (C++) + pytest (Python)      |
| Config          | TOML                                |
| Containers      | Docker + Docker Compose             |
| CI              | GitHub Actions (ASan, TSan, UBSan)  |
