# LABIOS 2.0 Specification

**The Agent I/O Runtime**

US Patent 11,630,834 B2 | NSF Award #2331480 | HPDC'19 Best Paper Nominee
Author: Anthony Kougkas, Illinois Institute of Technology

---

## 1. The Agent I/O Runtime

LABIOS is the first agent I/O runtime. Not a filesystem. Not middleware. Not an object store. Not an agent framework. It is the execution environment for I/O operations expressed as labels.

### 1.1 The 2019 Thesis

Domain scientists running MPI simulations and Hadoop jobs need a unified I/O platform that handles conflicting workload requirements through the label abstraction. A label is a self-describing unit of I/O work. The infrastructure routes, shuffles, schedules, transforms, and delivers labels without moving raw data. LABIOS proved this architecture with CM1 (16x I/O speedup), HACC (6x), and Montage (17x I/O boost with 65% execution time reduction).

### 1.2 The 2026 Thesis

AI agents have replaced domain scientists as the primary producers and consumers of I/O at every scale. A coding agent writing files on a laptop. A retrieval agent querying vector databases. A science agent orchestrating terabyte-level distributed I/O across a cluster. A multi-agent workflow coordinating data sharing between dozens of agents with different isolation and priority requirements.

Every I/O operation an agent performs is expressible as a label: a self-describing unit of work that carries type, source, destination, operation, priority, intent, dependencies, and continuation hints. Labels are to I/O what packets are to networking.

### 1.3 The Category

The agent I/O runtime is the layer between agent frameworks (above) and storage backends (below). Agent frameworks produce labels explicitly via SDK or implicitly via intercept. LABIOS routes, shuffles, schedules, transforms, and delivers labels to workers that execute them against any storage backend. The agent never touches raw data unless it chooses to.

LABIOS is always running. It is a service, not a library. Agents connect to it. It exposes a hyper-configurable surface that agents can navigate to express exactly what they need: latency requirements, energy constraints, isolation boundaries, data locality preferences, pipeline operations, and continuation triggers. In 2019, configuration complexity was a burden because humans get overwhelmed. In 2026, configuration complexity is power because agents navigate complex parameter spaces better than humans. More LABIOS knobs means more expressive power for the agent to articulate its I/O needs.

---

## 2. The Label

### 2.1 The Universal Information Carrier

A label is a self-describing unit of I/O work that flows through the LABIOS runtime like a package through a logistics chain. Each station reads the label, adds its contribution, and sends it forward. The label is the information highway of the system.

The mental model is the residual stream inside a transformer: the hidden state vector flows through every layer, each layer reading from it and writing to it. The label flows through every LABIOS component the same way. The client writes I/O intent. The shuffler annotates aggregation and dependency information. The scheduler writes routing decisions. The worker writes execution results. The continuation engine reads the result and creates the next label.

Components are lean processors. They operate ON the label rather than maintaining heavy external state. The catalog manager only persists metadata for durable labels. For ephemeral labels (the majority in agent workloads), the label itself carries all state.

### 2.2 Label Anatomy

```
Label {
    // === Origin (set by creator, sealed after creation) ===
    id            : uint64         // Snowflake: 41-bit ms + 10-bit node + 12-bit seq
    app_id        : uint32         // Agent or application identity
    type          : enum           // READ, WRITE, DELETE, FLUSH, COMPOSITE, OBSERVE
    source        : URI            // Where data comes from
    destination   : URI            // Where data goes (file://, s3://, vector://, graph://)
    priority      : uint8          // 0-255, maps to priority lanes
    intent        : enum           // Checkpoint, Cache, ToolOutput, FinalResult,
                                   // Intermediate, SharedState, Embedding,
                                   // ModelWeight, KVCache, ReasoningTrace
    version       : uint64         // Monotonic counter for conflict detection
                                   // (checkpoint-resume, concurrent WAW resolution)
    isolation     : enum           // Agent (private), Workspace (shared), Global
    ttl           : duration       // Auto-expire for ephemeral data
    pipeline      : vec<Operation> // SDS pipeline DAG
    continuation  : Continuation   // on_complete: notify channel, create label,
                                   // or trigger condition
    durability    : enum           // Ephemeral (label-carried state) or
                                   // Durable (catalog-persisted)

    // === Accumulation (written by components as label flows) ===
    dependencies  : vec<id>        // Added by shuffler (RAW, WAW, WAR detection)
    aggregation   : AggregationInfo // Added by shuffler (merged offset ranges)
    supertask_id  : optional<id>   // Added by shuffler if label joins a supertask
    routing       : RoutingDecision // Added by scheduler (target worker, policy used)
    score_snapshot: WorkerScores   // Added by scheduler (scores at decision time)

    // === State (updated as label progresses) ===
    status        : enum           // Queued, Shuffled, Scheduled, Executing,
                                   // Complete, Failed
    timestamps    : Timestamps     // Created, queued, dispatched, started, completed
    result        : optional<Result> // Execution outcome (data location, error,
                                   // SDS output)
    hops          : vec<HopRecord> // Trace of every component that touched this label
}
```

### 2.3 The Factory Model

LABIOS is a label factory. Labels enter as raw I/O intent from agents. They flow through processing stations (shuffler, scheduler, worker) where each station enriches the label with its decisions. A completed label carries the full history of how the I/O was orchestrated and executed.

### 2.4 Durability as Configuration

Ephemeral labels carry all their state in-flight and are garbage collected after completion. Durable labels have their state persisted to the catalog for tracking, auditing, or recovery. Most agent I/O is ephemeral. Science-critical I/O is durable. The agent chooses.

### 2.5 Label Properties

Labels are mutable information carriers that accumulate context as they flow through the runtime. Origin fields are sealed after creation (the creator's intent is preserved). Accumulation and state fields are written by the runtime.

The `OBSERVE` type is new for 2026: a read-only label that queries system state (queue depth, worker scores, data locations) without side effects. Agents use observability labels to make informed I/O decisions.

### 2.6 What Labels Are Not

Labels are not compute tasks. They express I/O intent, not arbitrary computation. Labels are not RDDs or data objects. They are instructions, not data containers. Labels are not API calls. They are queued, shuffled, scheduled, and executed asynchronously by the runtime.

### 2.7 Continuation Model

Labels carry optional continuation hints that enable reactive I/O chaining without agent polling:

- **Notify:** On completion, publish a notification to a named channel. Subscribing agents receive the result.
- **Chain:** On completion, create a new label with specified parameters. Enables multi-step I/O workflows.
- **Conditional:** On completion, evaluate a condition against the result. If true, create the continuation label. If false, stop.
- **None:** Default. Label completes and the agent retrieves the result when ready.

Continuations keep LABIOS as an I/O runtime (not an agent framework) while giving it the hooks to participate efficiently in agent reasoning loops. The agent framework handles reasoning; LABIOS handles I/O orchestration with reactive triggers.

### 2.8 Label Lifecycle

Create → Queue → Shuffle → Schedule → Assign → Execute → Complete → Continuation

---

## 3. Triple Decoupling

The architectural insight that makes everything else possible.

### 3.1 Instruction from Data

The label (instruction) travels separately from the data. The dispatcher shuffles, reorders, aggregates, and routes labels while the data sits untouched in the warehouse. This is what makes label intelligence possible: sophisticated optimization over instructions without paying the cost of moving bytes.

For agents: an agent publishes a pipeline label (read → filter → embed → index) and the entire operation executes at the storage layer. The agent never touches raw data. The warehouse is the bridge between intent and execution, and data only moves when a worker needs it.

### 3.2 Production from Execution

The agent (producer) and the worker (executor) never communicate directly. The dispatcher is the only bridge. The agent publishes labels and continues reasoning. Workers execute labels independently, at their own pace, on their own storage.

For agents: this is why async-by-default works. An agent's reasoning loop does not stall waiting for I/O unless it explicitly chooses to wait. Labels with continuation hints chain operations reactively. The agent produces I/O intent; the runtime handles execution.

### 3.3 Scheduling from Storage

The dispatcher decides WHERE and WHEN labels execute. Workers decide HOW, against whatever backend the label's destination URI specifies. The scheduler knows nothing about POSIX vs S3 vs vector databases. Workers know nothing about scheduling policies.

For agents: this is what enables universal label routing. A label destined for `vector://embeddings/collection` routes through the same dispatcher and scheduling infrastructure as `file:///data/checkpoint.pt`. The intelligence (shuffling, dependency resolution, scoring) applies uniformly regardless of backend.

### 3.4 The Invariant

No component assumes the presence or location of another. Clients never talk to workers. The dispatcher is the only bridge. This invariant enables the scale-adaptive deployment model: same label abstraction, same runtime, whether LABIOS runs in-process on a laptop or across 100 nodes in a cluster.

---

## 4. Universal Label Routing

### 4.1 Every Storage System Is a URI

The label's `source` and `destination` fields are URIs. The URI scheme tells the worker which backend to use. The worker maintains a backend registry that maps URI schemes to implementations.

### 4.2 URI Scheme Registry

| Scheme | Backend Category | Examples |
|--------|-----------------|----------|
| `file://` | Local/distributed filesystem | POSIX, Lustre, GPFS, BeeGFS |
| `s3://` | Object storage | S3, MinIO, GCS, Azure Blob |
| `vector://` | Vector database | Pinecone, Milvus, pgvector, Weaviate |
| `graph://` | Graph database | Neo4j, Memgraph, Amazon Neptune |
| `kv://` | Key-value store | Redis, DragonflyDB, RocksDB |
| `stream://` | Message stream | NATS JetStream, Kafka |
| `memory://` | In-memory warehouse | LABIOS warehouse (DragonflyDB) |
| `pfs://` | Parallel filesystem | OrangeFS, DAOS, GPFS |

### 4.3 The Routing Protocol

1. Agent creates a label with `destination = "vector://embeddings/my_collection"`
2. Label flows through the runtime (shuffled, scheduled, assigned to a worker)
3. Worker receives the label, extracts the URI scheme (`vector://`)
4. Worker looks up the `vector` scheme in its backend registry
5. Backend implementation translates the label's operation into backend-specific calls
6. Result flows back onto the label

### 4.4 Backend Contract

Every backend implements a concept-constrained interface:

```cpp
template<typename B>
concept BackendStore = requires(B b, Label& label, std::span<const std::byte> data) {
    { b.put(label, data) } -> std::same_as<Result>;
    { b.get(label) }       -> std::same_as<DataResult>;
    { b.del(label) }       -> std::same_as<Result>;
    { b.query(label) }     -> std::same_as<QueryResult>;
};
```

An agent targeting a vector database uses the same LABIOS API as one targeting a filesystem. The URI is the only difference. LABIOS provides the routing, scheduling, and pipeline infrastructure uniformly. New backends are added by implementing the concept and registering a URI scheme. No changes to the agent, the client, or the dispatcher.

### 4.5 Cross-Backend Operations

Storage bridging is native. A single SDS pipeline label can read from `file:///data/raw.csv`, filter it, embed it, and write to `vector://embeddings/collection`. Cross-backend operations are first-class because the pipeline DAG specifies source and destination per operation.

---

## 5. Programmable Data Pipelines

### 5.1 SDS Evolved

In 2019, Software-Defined Storage meant labels carry function pointers. A worker reads data, executes the function, returns the result. The application never touches raw data.

In 2026, SDS becomes a programmable pipeline engine. Labels carry DAGs of operations that workers execute end-to-end. A single label can express: read a CSV from the filesystem, filter rows by a predicate, embed the text column using a local model, and index the embeddings into a vector database.

### 5.2 Pipeline Definition

```
Pipeline = vec<Stage>

Stage {
    operation : URI            // "builtin://filter" or "repo://my_org/my_func"
    args      : bytes          // Serialized arguments for the operation
    input     : StageRef       // "source" (label source) or index of previous stage
    output    : StageRef       // "destination" (label destination) or index of next stage
}
```

### 5.3 Program Repository

A shared registry of functions accessible to all workers.

**Builtins:** `compress_lz4`, `decompress_lz4`, `filter`, `deduplicate`, `sort`, `sum`, `median`, `sample`, `format_convert`

**Agent builtins (2026):** `embed`, `chunk_text`, `extract_entities`, `similarity_search`, `classify`

**User functions:** Agents register custom functions as shared libraries. Workers load them via `dlopen`. Functions execute in a seccomp sandbox (no fork, no exec, no network).

### 5.4 Execution Model

1. Worker receives a label with a pipeline
2. Worker resolves each stage's operation from the program repository
3. Worker executes stages in DAG order, passing intermediate results between stages
4. Final output goes to the label's destination (could be a different backend than the source)
5. Result (metadata, output location, computed value) is written back onto the label

### 5.5 Intelligence Travels to Data

A science agent that needs to preprocess 10TB of simulation output does not download 10TB. It publishes a pipeline label. Workers execute the pipeline in parallel across the cluster. The agent receives the processed result.

### 5.6 The Agentic Evolution of SDS

The trajectory of SDS follows a natural progression: builtins → user functions → agent skills → reasoning-capable workers. Workers evolve from dumb I/O executors to lightweight agentic processors. The program repository becomes a skill registry. The `dlopen` mechanism that loads `compress_lz4` today loads a fine-tuned embedding model tomorrow. The line between I/O and intelligent data processing dissolves when workers carry reasoning capabilities.

---

## 6. Label Intelligence

### 6.1 Granularity Control (Client-Side)

The Label Manager transforms raw I/O requests into appropriately sized labels.

**1-to-N splitting:** A 10MB write with 1MB max label size produces 10 labels. Configurable min/max sizes driven by hardware characteristics (page size, cache size, network buffers, storage blocks).

**N-to-1 aggregation:** Ten 100KB writes below the 1MB min threshold accumulate in the small-I/O cache. When the cache fills or a timer expires, they flush as a single aggregated label (memtable/SSTable pattern).

**1-to-1 passthrough:** For synchronous reads or labels that already fit within bounds.

### 6.2 The Shuffler (Dispatcher-Side)

The shuffler takes a batch of labels and enriches each one. Every decision is written onto the label.

**Data Aggregation:** Labels targeting consecutive offsets in the same file are merged into a single larger label. Preserves locality. The shuffler writes aggregation metadata onto each affected label. Configurable on/off.

**Dependency Detection:** RAW (read-after-write), WAW (write-after-write), WAR (write-after-read) hazards detected per configurable granularity (per-agent, per-file, per-dataset). Dependencies are written onto the label.

**Supertask Creation:** Dependent labels are packaged into a supertask that executes its children in strictly increasing order on a single worker. The supertask ID is written onto each child label.

**Read-Locality Routing:** Read labels are routed directly to the worker holding the data, bypassing the solver entirely. The routing decision is written onto the label.

### 6.3 Batch Processing

The dispatcher collects labels by count (e.g., 100 per batch) or by timeout (e.g., 50ms). This is the fundamental latency/throughput trade-off knob. Agents that need low latency can set tight timeouts. Agents with bulk workloads prefer large batches for better shuffling decisions.

### 6.4 Agentic Dispatching

Every LABIOS component has a deterministic algorithmic baseline and an agentic evolution path. The shuffling rules, aggregation heuristics, and routing decisions are LABIOS primitives. A dispatcher agent can reason about which primitives to apply, using them as tools. The rules are the deterministic baseline; reasoning is the 2026 evolution.

---

## 7. Scheduling and Worker Scoring

### 7.1 Worker Tiers

LABIOS defines three worker tiers representing a spectrum from lightweight I/O executors to reasoning-capable agents.

| Tier | Name | Capabilities | Cost |
|------|------|-------------|------|
| 0 | Databot | Single I/O operations (read, write, delete). Stateless. No SDS. | Lowest. Cheap to commission and decommission. |
| 1 | Pipeline | Executes SDS pipeline DAGs. Loads functions from program repository via dlopen. | Medium. More resources than a databot. |
| 2 | Agentic | Reasoning-capable. Loads agent skills. Can write code, use tools, make decisions about complex data management. Powered by lightweight fine-tuned models. | Highest. Most capable. |

Workers self-register with the Worker Manager on startup, advertising their tier and capabilities. The Worker Manager maintains a bucket-sorted registry extended to account for tier heterogeneity.

### 7.2 The Worker Manager as Orchestrator

The Worker Manager is leader-elected to the most powerful node in the pool. It is distributed and scalable. Beyond the paper's responsibilities (registry, score tracking, commission/decommission), the 2026 Worker Manager can:

- Decompose complex labels into sub-labels distributed across workers
- Coordinate multi-worker execution for labels that span tiers
- Make elastic scaling decisions per tier (commission more databots vs. promote a pipeline worker)

### 7.3 Extensible Scoring Mechanism

```
Score(worker_i) = Σ(j=1..N) Weight_j × Variable_j
```

The spec defines the scoring mechanism, not a fixed variable list. Variables form a scored vector that grows as the system evolves.

**Baseline variables (from the paper):**

| Variable | Range | Dynamic? | Source |
|----------|-------|----------|--------|
| Availability | {0, 1} | Yes | Worker state (active/suspended) |
| Capacity | [0, 1] | Yes | Remaining / total storage |
| Load | [0, 1] | Yes | Queue size / max queue |
| Speed | {1..5} | Init | Storage medium bandwidth class |
| Energy | {1..5} | Init | Power wattage class |

**2026 example extensions:**

| Variable | Range | What It Captures |
|----------|-------|-----------------|
| Tier | {0, 1, 2} | Worker capability class |
| Skills | [0, 1] | Fraction of requested capabilities available |
| Compute | [0, 1] | Available CPU/memory resources |
| Reasoning | {0..5} | Model capability class (0 = none) |

Weight profiles are TOML configs that assign weights to all registered variables. The paper's three profiles (low_latency, energy_savings, high_bandwidth) are the starting set.

### 7.4 Intent-Aware Scheduling

The dispatcher uses intent tags to modify scheduling behavior:

- `Checkpoint` intent → weight capacity and speed high, tier irrelevant
- `Pipeline` intent → weight tier and skills high (need Pipeline or Agentic worker)
- `Complex` intent → weight reasoning high (need Agentic worker)
- `Cache` intent → weight speed and load high (fast, available worker)
- `Intermediate` intent → weight load low (nearest available worker)

### 7.5 Four Scheduling Policies

| Policy | Cost | How It Works |
|--------|------|-------------|
| Round Robin | Low | Distribute labels cyclically to available workers. Fair but unintelligent. |
| Random | Low | Uniform random to all workers including suspended. No optimization. |
| Constraint-based | Medium | Request top-N scored workers from manager. Distribute evenly under the current weight profile. |
| MinMax DP | High | Multidimensional knapsack: maximize performance, minimize energy, subject to capacity/load. Approximate dynamic programming. Near-optimal. |

These are the deterministic baseline. A scheduler agent can reason about which policy to apply per batch based on current system state, workload patterns, and agent intent. The four policies become tools in the scheduler agent's toolkit.

---

## 8. Multi-Agent Coordination

### 8.1 The Problem

When LABIOS served human users running MPI programs, inter-process data sharing meant one process writes temporary files and another reads them. The paper's ephemeral rooms eliminated the filesystem round-trip and produced Montage's 17x I/O speedup.

In 2026, coordination is between agents that have different trust levels, different data lifetimes, different priority needs, and potentially different organizations.

### 8.2 Channels (Evolved from Ephemeral Rooms)

Named, typed data streams between agents in a workflow.

- Agent A publishes to channel `"workflow/stage-1-output"`
- Agent B subscribes and processes data as it arrives
- Data flows through the warehouse without filesystem round-trips
- Properties: ordering guarantees, backpressure, TTL, configurable durability
- Lifecycle: create → publish/subscribe → drain → destroy (auto-destroy on last disconnect)

Channels are the coordination primitive for pipeline-style multi-agent workflows.

### 8.3 Workspaces (Persistent Shared State)

Named, access-controlled regions of the warehouse.

- Multiple agents read and write to the same workspace over time
- Properties: versioning, access control lists, TTL, configurable durability
- Use case: a team of science agents sharing a knowledge graph, intermediate datasets, or experimental results that persist across sessions

### 8.4 Isolation Model

| Level | Visibility | Use Case |
|-------|-----------|----------|
| Agent | Private to the owning agent | Default. Local scratchpad, reasoning intermediates. |
| Workspace | Shared within a defined group | Collaborative work. Agents explicitly granted access. |
| Channel | Pub/sub within connected agents | Pipeline data flow. Temporary. |
| Global | All agents on the LABIOS instance | System-wide resources, shared models, common datasets. |

### 8.5 Isolation as a Security Boundary

Agent A in `Agent` isolation cannot see Agent B's labels or data. Cross-isolation data sharing requires explicit channel or workspace creation. This is critical for multi-tenant deployments where agents from different users, different organizations, or different trust levels share the same LABIOS instance.

---

## 9. Elastic Scaling

### 9.1 Per-Tier Elasticity

The paper proved elastic storage works: commissioning workers during I/O bursts and suspending them during compute phases reduced energy consumption while maintaining performance. The 2026 extension applies elasticity per worker tier.

### 9.2 Scaling Triggers

| Trigger | Action | Scope |
|---------|--------|-------|
| Queue pressure exceeds threshold | Commission workers | Per tier: databots for bulk, pipeline workers for SDS labels |
| Worker idle beyond timeout | Suspend worker | Individual: self-suspend, notify manager |
| Tier capacity exhausted | Promote (commission higher-tier) | Cross-tier escalation |
| All queues drained | Decommission excess workers | Per tier: scale down to minimums |
| Energy budget exceeded | Decommission lowest-priority | Global: energy-aware |

### 9.3 Commission/Decommission by Environment

- **Docker:** Spin up/stop containers via Docker Engine API
- **Kubernetes:** Scale deployments per tier
- **Bare metal:** IPMI power on/off, SSH, Wake-on-LAN
- **Cloud:** Auto-scaling groups per tier

### 9.4 Worker Self-Suspend

A worker with no labels in its queue for a configurable timeout self-suspends and notifies the manager. The manager can resume it via NATS command when new labels arrive for its tier.

### 9.5 Agentic Evolution

The elastic decision engine (currently rule-based) can be driven by a manager agent that reasons about workload patterns, energy budgets, cost constraints, and upcoming demand. The commission/decommission primitives become tools.

---

## 10. Observability and Telemetry

### 10.1 Two Observation Patterns

LABIOS exposes its internal state to agents through two complementary mechanisms.

### 10.2 Observability Labels (Point-in-Time Queries)

An agent publishes a label with `type = OBSERVE` and a query specification:

- `observe://queue/depth` → current queue depth per priority lane
- `observe://workers/scores` → current worker scores and availability
- `observe://data/location?file=/data/checkpoint.pt` → which worker holds this data
- `observe://system/health` → backend health, warehouse capacity, dispatcher throughput

The observability label flows through the dispatcher like any other label. The result is written onto the label and returned to the agent. Read-only, no side effects.

### 10.3 Telemetry Stream (Continuous Monitoring)

LABIOS publishes a continuous telemetry stream via NATS:

- Label throughput (labels/sec per priority lane)
- Latency percentiles (p50, p95, p99 per operation type)
- Worker utilization per tier
- Backend response times
- Cache hit rates
- Queue depth over time
- Elastic scaling events

Agents subscribe to the stream and adapt their I/O patterns in real-time. A science agent might observe increasing queue pressure and reduce its I/O rate. A coding agent might see high cache hit rates and lean into cached reads.

### 10.4 Agent-Driven Optimization

Observability labels answer "what is the state right now?" Telemetry streams answer "how is the state changing?" Together, they give agents the information needed to make intelligent I/O decisions. LABIOS is hyper-configurable because agents can observe the system and tune their behavior based on what they see.

---

## 11. Scale-Adaptive Deployment

### 11.1 One Model, Hyper-Configurable

The paper defined four deployment models: accelerator, forwarder, buffering, and remote distributed. Each placed LABIOS components differently across hardware tiers. In 2026, LABIOS is a single adaptive deployment that configures itself based on available resources.

LABIOS is a service, always running. Agents connect to it. It adapts.

### 11.2 Adaptation Rules

| Resources Detected | Configuration |
|-------------------|---------------|
| Single node, no external storage | Embedded: all components in-process. Warehouse in local memory. Workers write to local filesystem. |
| Single node with external backends | Gateway: local client + dispatcher. Workers route to external backends via URI. |
| Multi-node cluster | Distributed: dispatcher and warehouse on dedicated nodes. Workers spread across storage nodes. Per-tier scaling. |
| Cloud/Kubernetes | Orchestrated: components as pods/services. Auto-scaling per tier. Managed NATS and DragonflyDB. |
| Hybrid (local + remote) | Mixed: local workers for fast I/O, remote workers for durable/distributed storage. Dispatcher routes by intent. |

### 11.3 Configuration Surface

TOML configuration with every tunable knob exposed: batch size, timeout, aggregation on/off, dependency granularity, weight profiles, backend URIs, worker tier limits, elastic thresholds, channel TTLs, workspace persistence, telemetry intervals.

Agents navigate this configuration space. More knobs means more expressive power for agents. This is a deliberate design choice: LABIOS trades human-friendliness for agent-expressiveness.

### 11.4 The Invariant Holds

Labels flow through the same lifecycle across all configurations. The dispatcher is the only bridge. Production is decoupled from execution. What changes is component placement and resource allocation.

---

## 12. Integration Surfaces

### 12.1 Two Tiers: Transparent for the Unaware, Rich for the Informed

**Tier 1: Transparent Intercept (no code changes)**

Legacy applications and unaware agents use standard I/O calls. LABIOS intercepts at the boundary.

- **POSIX intercept** (`LD_PRELOAD`): `fwrite()`, `fread()`, `fopen()`, `fclose()` become labels automatically.
- **MPI-IO wrapper:** MPI collective I/O calls become batched labels with aggregation hints.
- **Filesystem mount (future):** FUSE mount where all reads/writes are label operations.

**Tier 2: Native SDK (full expressiveness)**

LABIOS-aware agents and applications use the SDK directly.

- **C++ native API:** `client.write()`, `client.read()`, `client.async_write()`, `client.wait()`
- **Label-level API:** `client.create_label()`, `client.publish()` with full control over intent, pipeline, isolation, continuation, priority
- **C API** (`labios.h`): FFI for Rust, Go, and other agent runtimes
- **Python SDK** (`pip install labios`): pybind11 bindings. The primary agent API. Full access to labels, channels, workspaces, observability.

### 12.2 Configuration API

Agents can observe and tune LABIOS at runtime:

- `observe://config/current` → read current configuration
- `labios.config.set(key, value)` → adjust configuration parameters
- Agents tune LABIOS based on observed telemetry without human intervention

### 12.3 Agent Memory Adapters

Agent memory systems (LangChain Memory, LlamaIndex Storage, custom vector DBs) all implement the same pattern: store context, retrieve context, share context, expire context. These map directly to labels:

- `put(key, value, metadata)` → Write label with intent from metadata
- `get(key)` → Read label
- `share(key, workspace)` → Write label with Workspace isolation
- `delete(key)` → Delete label
- `list(prefix)` → Observability label

A thin adapter layer (~100 lines per library) translates each memory library's API into labels. LABIOS becomes the universal backend for agent memory. The heavy lifting (routing, scheduling, caching, lifecycle, ordering) is handled by the runtime.

### 12.4 The Gradient

Unaware agents get LABIOS benefits for free (async I/O, aggregation, intelligent routing). SDK agents get full expressiveness (intent tags, pipelines, channels, workspaces, observability, configuration). The same runtime serves both.

---

## 13. Agent I/O Benchmarks

### 13.1 New Benchmarks for a New Category

LABIOS 2.0 defines its own performance bar. No reproduction of 2019 workloads. The benchmarks measure LABIOS against the alternative: agents doing I/O directly against backends without a runtime.

### 13.2 Benchmark Suite

| Benchmark | Agent Pattern | What It Proves |
|-----------|--------------|----------------|
| **Coding Agent I/O** | Single agent, local files, small file I/O (1KB-1MB) | LABIOS adds minimal overhead for small-scale local I/O. Embedded mode works. |
| **RAG Pipeline** | Agent reads documents, embeds chunks, queries vector store | Cross-backend pipelines (file:// → embed → vector://) work end-to-end via SDS. |
| **Science Data Pipeline** | Distributed agents processing TB-scale output across a cluster | Tier 1/2 workers handle large-scale SDS pipelines. Channels coordinate stages. |
| **Checkpoint Storm** | 100+ agents simultaneously checkpointing models/state | Elastic scaling absorbs bursty I/O. Databots commission under pressure. |
| **Multi-Agent Collaboration** | Team of agents sharing a workspace, concurrent reads/writes | Workspaces handle concurrent multi-agent access with isolation guarantees. |
| **Cross-Backend ETL** | Pipeline spanning file:// → filter → graph:// → vector:// | Universal label routing handles multi-backend pipelines in a single label. |
| **Agentic Worker** | Complex data task requiring Tier 2 reasoning | Agentic workers outperform hardcoded logic on ambiguous data operations. |
| **Scale Adaptation** | Same workload on 1 node → 4 nodes → 16 nodes | Single deployment model adapts without reconfiguration. |

### 13.3 Performance Targets

| Path | Target |
|------|--------|
| Label serialization (FlatBuffers, per label) | < 0.5 µs |
| Label deserialization (per label) | < 0.2 µs |
| Shuffler (100-label mixed batch) | < 100 µs |
| Full path, fast lane (client → dispatcher → worker, control-path I/O) | < 1 ms |
| Full path, bulk lane (background I/O, checkpoints, logs) | < 10 ms |
| Context share (channel write + subscriber notify) | < 2 ms |
| Catalog batch (100 labels, pipelined) | < 200 µs |

The label processing pipeline must add minimal overhead to the agent's critical path. An agent's reasoning loop should not stall measurably on I/O when using async mode with the fast priority lane.

### 13.4 Comparison Baseline

Every benchmark compares LABIOS against agents performing the same I/O directly against storage backends. The thesis: LABIOS' intelligence (shuffling, scheduling, pipelines, coordination, elasticity) makes agent I/O faster, more efficient, and more scalable than direct access.

---

## 14. Positioning

### 14.1 Why Existing Categories Fail

| Category | What It Does | Why Insufficient for Agents |
|----------|-------------|----------------------------|
| Parallel filesystems (Lustre, GPFS) | POSIX-compliant distributed file storage | No intent, no priority, no isolation, no SDS, no agent awareness. Optimized for large sequential I/O. Agents produce unpredictable, mixed-granularity, intent-rich I/O. |
| Object stores (S3, MinIO) | HTTP-based blob storage | No scheduling intelligence, no dependency resolution, no pipelines, no real-time coordination. |
| Vector databases (Pinecone, Milvus) | Similarity search on embeddings | Single-backend. No routing to other storage types. No I/O orchestration. |
| Agent frameworks (LangChain, CrewAI) | Agent reasoning and tool orchestration | I/O is an afterthought. Synchronous, blocking, unoptimized. I/O plugins are thin wrappers around individual backends. |
| Distributed middleware (Alluxio, IRIS) | Data access abstraction | Lack the label abstraction, SDS pipelines, agent isolation, and scheduling intelligence. |

### 14.2 Where LABIOS Sits

```
Agent Frameworks (LangChain, CrewAI, AutoGen, custom)
         |
         |  produce labels (via SDK) or I/O calls (via intercept)
         v
    +-----------------------------+
    |  LABIOS: Agent I/O Runtime  |
    |                             |
    |  Labels -> Shuffle ->       |
    |  Schedule -> Execute ->     |
    |  Pipeline -> Coordinate     |
    +-------------+---------------+
                  |  universal label routing
                  v
    Storage Backends (file://, s3://, vector://, graph://, kv://, pfs://)
```

LABIOS is not a better Lustre. It is not a better LangChain. It is the layer between them that makes I/O intelligent, scheduled, coordinated, elastic, and observable. It is the runtime that turns raw I/O calls into orchestrated data operations that serve agents at every scale.

---

## 15. Cross-Cutting Theme: The Agentic Evolution

Every LABIOS component has a deterministic algorithmic baseline and an agentic evolution path.

| Component | Algorithmic Baseline | Agentic Evolution |
|-----------|---------------------|-------------------|
| Shuffler | Rule-based aggregation, dependency detection | Dispatcher agent reasons about shuffling strategy |
| Scheduler | Four fixed policies with weight profiles | Scheduler agent selects policy per batch dynamically |
| Worker Manager | Threshold-based elastic scaling | Manager agent reasons about workload patterns and cost |
| Workers (Tier 0-1) | Execute I/O operations and SDS pipelines | Tier 2 workers reason, write code, use tools |
| Configuration | Static TOML files | Agents observe telemetry and tune configuration in real-time |

The primitives exist either way. What changes is whether deterministic rules or reasoning agents orchestrate them. This duality ensures LABIOS works today with algorithmic policies and evolves toward fully agentic operation as lightweight reasoning models mature.

---

## References

- **Patent:** US 11,630,834 B2, "Label-Based Data Representation I/O Process and System"
- **Paper:** Kougkas, A., Devarajan, H., Lofstead, J., Sun, X-H. "LABIOS: A Distributed Label-Based I/O System." HPDC'19, Best Paper Nominee.
- **NSF Award:** #2331480
- **Constitutional Document:** `LABIOS-2.0.md`
- **Original Paper:** `.planning/reference/original-paper/labios.md`
