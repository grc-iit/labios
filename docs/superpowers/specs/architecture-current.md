# LABIOS 2.0 Architecture — Current Implementation

## System Overview

```
┌─────────────────────────────────────────────────────────────┐
│                    Application Layer                         │
│                                                             │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────────┐  │
│  │ HPC App      │  │ Python Agent │  │ Any App          │  │
│  │ (MPI/POSIX)  │  │ (pybind11)   │  │ (C API / FFI)   │  │
│  └──────┬───────┘  └──────┬───────┘  └────────┬─────────┘  │
│         │ LD_PRELOAD       │ import labios      │ labios.h   │
└─────────┼──────────────────┼────────────────────┼───────────┘
          ▼                  ▼                    ▼
┌─────────────────────────────────────────────────────────────┐
│                    LABIOS Client Library                     │
│                    (liblabios.a / liblabios_intercept.so)    │
│                                                             │
│  ┌────────────────┐  ┌──────────────────┐  ┌────────────┐  │
│  │ Label Manager  │  │ Content Manager  │  │  Catalog   │  │
│  │                │  │                  │  │  Manager   │  │
│  │ • split large  │  │ • stage data in  │  │            │  │
│  │   writes into  │  │   warehouse      │  │ • track    │  │
│  │   N labels     │  │ • small-I/O      │  │   label    │  │
│  │ • aggregate    │  │   cache + flush  │  │   status   │  │
│  │   small writes │  │ • retrieve read  │  │ • file →   │  │
│  │ • assign IDs   │  │   results        │  │   worker   │  │
│  │   (snowflake)  │  │                  │  │   mapping  │  │
│  └───────┬────────┘  └────────┬─────────┘  └─────┬──────┘  │
│          │ labels             │ data              │ metadata │
└──────────┼────────────────────┼───────────────────┼─────────┘
           ▼                    ▼                   ▼
   ┌───────────────┐    ┌──────────────┐    ┌──────────────┐
   │  NATS 2.10    │    │ DragonflyDB  │    │ DragonflyDB  │
   │  JetStream    │    │ (warehouse)  │    │ (inventory)  │
   │               │    │              │    │              │
   │ label queue   │    │ data staging │    │ metadata     │
   │ worker queues │    │ key-value    │    │ sorted sets  │
   │ score updates │    │              │    │              │
   └───────┬───────┘    └──────────────┘    └──────────────┘
           │
           ▼
┌─────────────────────────────────────────────────────────────┐
│                    Label Dispatcher                          │
│                    (labios-dispatcher process)               │
│                                                             │
│  Subscribe to "labios.labels"                               │
│       ↓                                                     │
│  Batch collection (size=100 OR timeout=50ms)  ← BOTTLENECK │
│       ↓                                                     │
│  ┌──────────────┐    ┌─────────────────────────────────┐    │
│  │   Shuffler   │    │        Scheduler                │    │
│  │              │    │                                 │    │
│  │ • aggregate  │    │ • Round Robin                   │    │
│  │   consecutive│    │ • Random                        │    │
│  │   writes     │    │ • Constraint-based (weighted)   │    │
│  │ • detect     │    │ • MinMax DP (knapsack)          │    │
│  │   RAW/WAW/WAR│    │                                 │    │
│  │ • create     │    │ Configured via TOML or env var  │    │
│  │   supertasks │    │ Weight profiles in conf/profiles│    │
│  │ • route reads│    │                                 │    │
│  │   to data    │    │                                 │    │
│  │   holder     │    │                                 │    │
│  └──────────────┘    └─────────────────────────────────┘    │
│       ↓                                                     │
│  Publish to "labios.worker.<id>"                            │
└─────────────────────────────────────────────────────────────┘
           │
           ▼
┌─────────────────────────────────────────────────────────────┐
│                    Worker Manager                            │
│                    (labios-manager process)                  │
│                                                             │
│  • Bucket-sorted worker registry (5 buckets by score)       │
│  • NATS registration/deregistration protocol                │
│  • Score tracking (availability, capacity, load, speed,     │
│    energy) with weight profiles from Table 2                │
│  • Elastic orchestrator (M4):                               │
│    - Queue depth monitoring from dispatcher                 │
│    - Commission: Docker API → create worker container       │
│    - Decommission: Docker API → stop idle container         │
│    - Resume: NATS command to self-suspended worker          │
└─────────────────────────────────────────────────────────────┘
           │
           ▼
┌─────────────────────────────────────────────────────────────┐
│                    Workers (labios-worker processes)         │
│                                                             │
│  ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌─────────────┐   │
│  │Worker 1 │  │Worker 2 │  │Worker 3 │  │Worker N     │   │
│  │speed=5  │  │speed=3  │  │speed=1  │  │(elastic)    │   │
│  │NVMe tier│  │SSD tier │  │HDD tier │  │commissioned │   │
│  └────┬────┘  └────┬────┘  └────┬────┘  └──────┬──────┘   │
│       │            │            │               │           │
│  Each worker:                                               │
│  • Subscribes to "labios.worker.<id>"                       │
│  • Retrieves data from DragonflyDB warehouse                │
│  • Writes to local filesystem (/labios/data/)               │
│  • Publishes completion via NATS reply                      │
│  • Reports score every 2s                                   │
│  • Self-suspends after idle timeout                         │
└─────────────────────────────────────────────────────────────┘
```

## Data Flow — Write Path (Current)

```
client.write("/file.dat", 1MB_buffer)
     │
     ├─1─► DragonflyDB: SET labios:data:<label_id> = 1MB    ← DATA COPY #1
     │
     ├─2─► NATS: publish "labios.labels" (serialized label, ~200 bytes)
     │
     ▼
Dispatcher: waits up to 50ms for batch to fill               ← LATENCY #1
     │
     ├─3─► Shuffler: check aggregation + dependencies
     ├─4─► Solver: pick target worker(s)
     │
     ├─5─► NATS: publish "labios.worker.2" (serialized label)
     │
     ▼
Worker 2:
     │
     ├─6─► DragonflyDB: GET labios:data:<label_id> → 1MB    ← DATA COPY #2
     ├─7─► Filesystem: write 1MB to /labios/data/file.dat
     ├─8─► DragonflyDB: DEL labios:data:<label_id>
     │
     ├─9─► NATS: publish completion to reply inbox
     │
     ▼
Client: receives completion, write() returns

Total hops: 9 (4 NATS, 3 DragonflyDB, 1 filesystem, 1 shuffler)
```

## Data Flow — Read Path (Current)

```
client.read("/file.dat", offset=0, size=1MB)
     │
     ├─1─► NATS: publish "labios.labels" (read label)
     │
     ▼
Dispatcher: waits up to 50ms                                 ← LATENCY #1
     │
     ├─2─► Catalog: lookup which worker holds /file.dat
     ├─3─► Direct route to holding worker (read-locality)
     │
     ├─4─► NATS: publish "labios.worker.2"
     │
     ▼
Worker 2:
     │
     ├─5─► Filesystem: read 1MB from /labios/data/file.dat
     ├─6─► DragonflyDB: SET labios:data:<label_id> = 1MB    ← STAGING
     │
     ├─7─► NATS: publish completion
     │
     ▼
Client:
     │
     ├─8─► DragonflyDB: GET labios:data:<label_id> → 1MB    ← RETRIEVE
     └───► return data to application
```

## API Usage Examples

### Transparent POSIX Intercept (no code changes)
```bash
# Any existing application works unchanged
LD_PRELOAD=liblabios_intercept.so \
  LABIOS_NATS_URL=nats://localhost:4222 \
  LABIOS_REDIS_HOST=localhost \
  ./my_simulation
```

### Native C++ API — Synchronous
```cpp
#include <labios/client.h>

auto client = labios::connect(labios::load_config("conf/labios.toml"));

// Write 10MB (split into 10 x 1MB labels automatically)
client.write("/data/output.dat", buffer);

// Read back
auto data = client.read("/data/output.dat", 0, 10 * 1024 * 1024);
```

### Native C++ API — Asynchronous (paper Figure 4)
```cpp
#include <labios/client.h>

auto client = labios::connect(cfg);

// Publish writes without blocking
auto s1 = client.async_write("/data/step1.dat", buf1);
auto s2 = client.async_write("/data/step2.dat", buf2);

// ... application continues computing ...

// Barrier: wait for both to complete
client.wait(s1);
client.wait(s2);
```

### Label-Level API (advanced)
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

### C API (for Python/Rust/Go agents)
```c
#include <labios/labios.h>

labios_client_t client;
labios_connect("nats://localhost:4222", "localhost", 6379, &client);

labios_status_t status;
labios_async_write(client, "/data/out.dat", buf, size, 0, &status);
// ... agent continues reasoning ...
labios_wait(status);
labios_status_free(status);

labios_disconnect(client);
```

## Configuration (conf/labios.toml)

```toml
[nats]
url = "nats://localhost:4222"

[redis]
host = "localhost"
port = 6379

[label]
min_size = "64KB"    # Below this → small-I/O cache
max_size = "1MB"     # Above this → split into N labels

[dispatcher]
batch_size = 100          # Max labels per batch
batch_timeout_ms = 50     # Max wait before dispatching  ← TUNE THIS
aggregation_enabled = true
dep_granularity = "per-file"   # or "per-app", "per-dataset"

[scheduler]
policy = "round-robin"    # random | constraint | minmax
profile_path = ""         # Path to weight profile TOML

[elastic]
enabled = false
min_workers = 1
max_workers = 10
pressure_threshold = 5
worker_idle_timeout_ms = 30000
```

## Known Performance Bottlenecks

1. **Dispatcher batch timeout (50ms)** — dominates sync latency
2. **Double data copy through DragonflyDB** — writes stage in warehouse
   then worker retrieves; reads do the reverse
3. **Per-label NATS round-trip** — 9 hops per write label
4. **No zero-copy path** — data serialized and deserialized at every hop
