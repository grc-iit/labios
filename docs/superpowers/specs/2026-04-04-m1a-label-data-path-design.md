# M1a Design: Label Data Path

**Date:** 2026-04-04
**Milestone:** 1a (Label Data Path)
**Constitutional ref:** LABIOS-2.0.md Section 2.1, 2.7, 3.2, 6 (Milestone 1)
**Prerequisite:** M0 complete (skeleton, Docker, CI)
**Followed by:** M1b (POSIX intercept)

## Scope

This sub-milestone delivers the core data path: labels flow from client through dispatcher to workers, data stages through Redis warehouse, workers execute READ and WRITE against local storage, completion notifications return to the client via NATS request-reply, and the catalog tracks every label's lifecycle.

What is NOT in scope: POSIX intercept (M1b), label shuffling/aggregation/dependency detection (M2), smart scheduling (M3), elastic workers (M4), SDS function execution (M5).

## Decisions

| Decision | Choice | Rationale |
|---|---|---|
| Label schema scope | All fields from paper + 2.0 extensions | Forward-compatible, no schema migration later |
| Pointer model | FlatBuffers union (MemoryPtr, FilePath, NetworkEndpoint) | Type-safe, self-documenting, zero ambiguity |
| Client API | Section 3.2 agent API (create_label / publish / wait) | Constitutional requirement, expressive |
| Worker assignment | RoundRobinSolver behind Solver concept | Establishes correct architecture for M3 |
| Completion notification | NATS request-reply | Native NATS, zero extra infrastructure, supports sync and async |
| Data staging | Client writes to Redis, label carries key | Fully decoupled client/worker per architectural invariant |
| Catalog | CatalogManager class wrapping Redis hash per label | Typed interface, defined key schema, shared by all components |

## 1. FlatBuffers Schema

File: `schemas/label.fbs`

```fbs
namespace labios.schema;

enum LabelType : byte { Read, Write, Delete, Flush, Composite }
enum CompletionStatus : byte { Complete, Error }
enum Intent : byte { None, Checkpoint, Cache, ToolOutput, FinalResult, Intermediate, SharedState }
enum Isolation : byte { None, Application, Agent }

// Flexible pointer: can reference memory, a file path, or a network endpoint
union PointerVariant { MemoryPtr, FilePath, NetworkEndpoint }

table MemoryPtr {
  address: uint64;
  size:    uint64;
}

table FilePath {
  path:   string;
  offset: uint64;
  length: uint64;
}

table NetworkEndpoint {
  host: string;
  port: uint16;
}

table Pointer {
  ptr: PointerVariant;
}

table Label {
  id:           uint64;       // unique: nanosecond timestamp + app_id hash
  type:         LabelType;
  source:       Pointer;
  destination:  Pointer;
  operation:    string;       // SDS function name from program repo (M5)
  flags:        uint32;       // bitfield: see LabelFlags below
  priority:     uint8;        // 0 = default, 255 = highest
  app_id:       uint32;
  dependencies: [uint64];     // supertask chains (M2)
  data_size:    uint64;       // total bytes of data
  intent:       Intent;       // 2.0: semantic scheduling hint
  ttl_seconds:  uint32;       // 2.0: auto-expire after N seconds (0 = permanent)
  isolation:    Isolation;    // 2.0: namespace partitioning
  reply_to:     string;       // NATS reply inbox (set by dispatcher when forwarding)
}

table Completion {
  label_id: uint64;
  status:   CompletionStatus;
  error:    string;
  data_key: string;           // Redis key where read result data is staged
}

root_type Label;
```

**LabelFlags bitfield (C++ side):**
```
Queued      = 1 << 0
Scheduled   = 1 << 1
Pending     = 1 << 2
Cached      = 1 << 3
Invalidated = 1 << 4
Async       = 1 << 5
```

CMake runs `flatc --cpp` on `schemas/label.fbs` at configure time. The generated header (`label_generated.h`) goes into the build directory and is included via a build-dir include path.

FlatBuffers dependency is added to `cmake/LabiosDependencies.cmake` via FetchContent (URL archive, same pattern as cnats/hiredis). The `flatc` compiler is built from source as part of the FetchContent.

## 2. Label C++ Types

File: `include/labios/label.h`, `src/labios/label.cpp`

Thin C++ wrapper around the FlatBuffers-generated types. Provides:

- `LabelType` enum (mirrors FlatBuffers but usable without including generated header)
- `LabelFlags` bitfield constants
- `generate_label_id(uint32_t app_id) -> uint64_t`: combines nanosecond timestamp with app_id bits. Uses `std::chrono::high_resolution_clock` for the timestamp portion.
- `serialize_label(...) -> std::vector<std::byte>`: builds a FlatBuffers Label from C++ parameters and returns the serialized buffer.
- `deserialize_label(std::span<const std::byte>) -> DeserializedLabel`: zero-copy FlatBuffers access wrapper.
- `serialize_completion(...) -> std::vector<std::byte>`: builds a Completion message.
- `deserialize_completion(std::span<const std::byte>) -> DeserializedCompletion`: accessor wrapper.

The `DeserializedLabel` and `DeserializedCompletion` types hold a reference to the buffer and provide typed accessors. They do not copy data (FlatBuffers zero-copy).

## 3. Client Library

File: `include/labios/client.h`, `src/labios/client.cpp`

### API (matching Section 3.2)

```cpp
namespace labios {

// Helper constructors for Pointer variants
Pointer memory_ptr(const void* addr, uint64_t size);
Pointer path(std::string_view filepath);
Pointer path(std::string_view filepath, uint64_t offset, uint64_t length);
Pointer endpoint(std::string_view host, uint16_t port);

struct LabelParams {
    LabelType type;
    Pointer source;
    Pointer destination;
    std::string operation;                    // default: empty (no SDS)
    uint32_t flags = 0;
    uint8_t priority = 0;
    Intent intent = Intent::None;
    uint32_t ttl_seconds = 0;
    Isolation isolation = Isolation::None;
};

class Status {
public:
    void wait();                              // block until complete
    bool ready() const;                       // non-blocking poll
    CompletionStatus result() const;          // Complete or Error
    std::string error() const;                // error message if Error
    std::string data_key() const;             // Redis key for read data
    uint64_t label_id() const;
};

class Label {
public:
    uint64_t id() const;
    LabelType type() const;
    // ... accessors for all fields
private:
    friend class Client;
    std::vector<std::byte> serialized_;
    uint64_t id_;
};

class Client {
public:
    explicit Client(const Config& cfg);
    ~Client();

    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    // Two-step API matching Section 3.2
    Label create_label(const LabelParams& params);
    Status publish(const Label& label,
                   std::span<const std::byte> data = {});

    // Convenience: synchronous write
    void write(std::string_view filepath,
               std::span<const std::byte> data,
               uint64_t offset = 0);

    // Convenience: synchronous read
    std::vector<std::byte> read(std::string_view filepath,
                                uint64_t offset,
                                uint64_t size);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

Client connect(const Config& cfg);  // factory function per Section 3.2 API

} // namespace labios
```

### Internal flow

**`publish()` for WRITE:**
1. Generate label ID via `generate_label_id(app_id)`
2. Stage data in Redis: `redis.set("labios:data:{label_id}", data)`
3. Record in catalog: `catalog.create(label_id, app_id, Write)` with status Queued
4. Serialize label with FlatBuffers (data_size set to actual buffer size)
5. Send via NATS request to `labios.labels` with a unique reply inbox
6. Return `Status` wrapping the pending NATS reply

**`publish()` for READ:**
1. Generate label ID
2. Record in catalog as Queued
3. Serialize label (source = file path to read, data_size = requested size)
4. Send via NATS request to `labios.labels`
5. Return `Status`

**`wait()`:**
1. Block on the NATS reply (with timeout)
2. Deserialize the Completion message
3. Update catalog to Complete or Error
4. For READ: the `data_key` in the completion tells the client where to find the data in Redis

**`write()` convenience:**
1. `publish({.type=Write, .source=memory_ptr(data), .destination=path(filepath)}, data)`
2. `status.wait()`

**`read()` convenience:**
1. `auto status = publish({.type=Read, .source=path(filepath, offset, size), .destination=memory_ptr(nullptr, size)})`
2. `status.wait()`
3. `auto data = redis.get(status.data_key())`
4. `redis.del(status.data_key())`
5. Return data

### app_id

For M1a, `app_id` is derived from the process PID: `static_cast<uint32_t>(getpid())`. M7 (Python SDK + Agent API) will introduce proper agent registration.

## 4. Warehouse

File: `include/labios/warehouse.h`, `src/labios/warehouse.cpp`

Thin wrapper around Redis for data staging:

```cpp
namespace labios {

class Warehouse {
public:
    explicit Warehouse(transport::RedisConnection& redis);

    void stage(uint64_t label_id, std::span<const std::byte> data);
    std::vector<std::byte> retrieve(uint64_t label_id);
    void remove(uint64_t label_id);
    bool exists(uint64_t label_id) const;

private:
    transport::RedisConnection& redis_;
};

} // namespace labios
```

Key format: `labios:data:{label_id}`. Data is stored as raw bytes. Redis `SET`/`GET` with binary-safe `%b` format (already implemented in the M0 RedisConnection wrapper).

For M1a, all data goes through the warehouse. M2's small-I/O cache and M6's ephemeral rooms layer on top.

## 5. Catalog Manager

File: `include/labios/catalog.h`, `src/labios/catalog.cpp`

```cpp
namespace labios {

enum class LabelStatus : uint8_t {
    Queued, Scheduled, Executing, Complete, Error
};

class CatalogManager {
public:
    explicit CatalogManager(transport::RedisConnection& redis);

    void create(uint64_t label_id, uint32_t app_id, LabelType type);
    void set_status(uint64_t label_id, LabelStatus status);
    LabelStatus get_status(uint64_t label_id) const;
    void set_worker(uint64_t label_id, int worker_id);
    std::optional<int> get_worker(uint64_t label_id) const;

private:
    transport::RedisConnection& redis_;
};

} // namespace labios
```

Redis key: `labios:catalog:{label_id}` as a Redis HASH with fields:
- `status` (string: "queued", "scheduled", "executing", "complete", "error")
- `app_id` (string of uint32)
- `type` (string: "read", "write", etc.)
- `worker_id` (string of int, set by dispatcher)
- `created_at` (epoch milliseconds)
- `updated_at` (epoch milliseconds)

This requires adding `hset` and `hget` methods to `RedisConnection`. The M0 wrapper only has `set`/`get` for strings.

## 6. Solver Interface + Round Robin

File: `include/labios/solver/solver.h`

```cpp
namespace labios {

struct WorkerInfo {
    int id;
    bool available;
};

// Map of worker_id -> vector of serialized label buffers
using AssignmentMap = std::unordered_map<int, std::vector<std::vector<std::byte>>>;

template<typename T>
concept Solver = requires(T s,
    std::vector<std::vector<std::byte>> labels,
    std::vector<WorkerInfo> workers) {
    { s.assign(labels, workers) } -> std::same_as<AssignmentMap>;
};

} // namespace labios
```

File: `include/labios/solver/round_robin.h`, `src/labios/solver/round_robin.cpp`

```cpp
namespace labios {

class RoundRobinSolver {
public:
    AssignmentMap assign(std::vector<std::vector<std::byte>> labels,
                         std::vector<WorkerInfo> workers);
private:
    size_t next_ = 0;
};

} // namespace labios
```

Cycles through available workers. Skips workers with `available == false`. Resets to 0 when it wraps.

## 7. Dispatcher Changes

File: `src/services/labios-dispatcher.cpp` (modify existing)

The dispatcher evolves from an M0 stub to a real component:

1. Maintains a list of registered workers (from config or Redis)
2. Subscribes to `labios.labels`
3. On each message:
   a. Deserialize the label
   b. Extract the NATS reply-to address from the message
   c. Inject `reply_to` into the label (re-serialize or forward as metadata)
   d. Update catalog: status = Scheduled
   e. Run `solver.assign({label}, workers)` to get assignment
   f. Publish the label to `labios.worker.{assigned_id}` with the reply-to preserved
4. Cooperative shutdown via jthread

The dispatcher forwards the reply-to address alongside the label so the worker can publish completion directly to the client's inbox.

**Implementation detail:** NATS messages carry `reply_to` as message metadata. When the dispatcher re-publishes to a worker subject, it includes the original reply-to in the label's `reply_to` field. The worker reads this field and publishes the completion to it.

## 8. Worker Changes

File: `src/services/labios-worker.cpp` (modify existing)

The worker evolves from an M0 stub:

1. Subscribe to `labios.worker.{id}`
2. On each message:
   a. Deserialize the label
   b. Update catalog: status = Executing
   c. Execute based on label type:
      - **WRITE:** retrieve data from warehouse (`labios:data:{label_id}`), write to local file at `destination.path` + offset using POSIX I/O, remove the warehouse key
      - **READ:** read from local file at `source.path` + offset for `data_size` bytes, stage result in warehouse (`labios:data:{label_id}`)
   d. Update catalog: status = Complete (or Error)
   e. Serialize Completion message
   f. Publish Completion to `label.reply_to` NATS subject
3. Cooperative shutdown via jthread

**Local storage:** Workers write to a configurable directory (`/labios/data/` in Docker, `./labios_data/` locally). The directory is created on startup if it doesn't exist. File paths from labels are resolved relative to this root.

**Mutex:** Redis access is protected by the existing mutex (from M0 fix). The NATS callback runs on a cnats thread; all Redis and file operations happen under the lock.

## 9. RedisConnection Extensions

The M0 `RedisConnection` only has `set(key, value)` and `get(key)`. M1a needs:

```cpp
// New methods on RedisConnection:
void hset(std::string_view key, std::string_view field, std::string_view value);
std::optional<std::string> hget(std::string_view key, std::string_view field) const;
void del(std::string_view key);
void set_binary(std::string_view key, std::span<const std::byte> data);
std::vector<std::byte> get_binary(std::string_view key);
```

The existing `set`/`get` operate on strings. `set_binary`/`get_binary` operate on raw byte buffers (same Redis SET/GET but with `std::byte` instead of `std::string`). `hset`/`hget` use Redis HASH operations for the catalog. `del` removes a key.

## 10. NatsConnection Extensions

The M0 `NatsConnection` needs request-reply support:

```cpp
// New methods on NatsConnection:
struct Reply {
    std::vector<std::byte> data;
};

Reply request(std::string_view subject, std::span<const std::byte> data,
              std::chrono::milliseconds timeout);

// The subscribe callback now also receives the reply_to subject:
using MessageCallback = std::function<void(std::string_view subject,
                                           std::span<const std::byte> data,
                                           std::string_view reply_to)>;

void publish_to(std::string_view subject, std::span<const std::byte> data);
```

`request()` uses `natsConnection_Request()` which sends with a reply inbox and waits for the reply. The callback signature gains `reply_to` so the worker can respond to the client's inbox.

## 11. Testing

### Unit tests (no live services)

**label_test.cpp:**
- Serialize a WRITE label with FilePath source/destination, deserialize, verify all fields match
- Serialize with MemoryPtr source, deserialize, verify union type resolves correctly
- Label ID generation produces unique IDs across calls
- Completion serialization roundtrip

**solver_test.cpp:**
- RoundRobin with 3 workers, 6 labels: expect 2 labels per worker
- RoundRobin with 1 unavailable worker: skips it
- RoundRobin with 0 available workers: returns empty map

**catalog_test.cpp:** (requires live Redis or a mock)
- Create entry, verify status is Queued
- Transition Queued -> Scheduled -> Executing -> Complete
- get_worker returns empty before set, returns id after set

### Integration tests (Docker, live services)

**data_path_test.cpp:**
- Write 1MB through the full pipeline, read it back, byte-compare
- Write 10 x 1MB labels, verify all 10 complete, verify catalog shows all Complete
- Write to worker-1, read back (round-robin assigns reads too), verify data matches

### Demo script

```bash
# Inside the test container or via docker compose run
labios-demo-write-read --size 100M --label-size 1M
# Writes 100MB as 100 labels, reads them back, prints:
#   Written: 100MB (100 labels) in X.XXs (XXX MB/s)
#   Read:    100MB (100 labels) in X.XXs (XXX MB/s)
#   Verify:  OK (all bytes match)
```

This is a simple binary in `src/services/` or `tests/benchmark/` that uses the Client API.

## 12. New and Modified Files

**New files (16):**
```
schemas/label.fbs
include/labios/label.h
include/labios/client.h
include/labios/catalog.h
include/labios/warehouse.h
include/labios/solver/solver.h
include/labios/solver/round_robin.h
src/labios/label.cpp
src/labios/client.cpp
src/labios/catalog.cpp
src/labios/warehouse.cpp
src/labios/solver/round_robin.cpp
tests/unit/label_test.cpp
tests/unit/solver_test.cpp
tests/integration/data_path_test.cpp
src/services/labios-demo.cpp
```

**Modified files (7):**
```
cmake/LabiosDependencies.cmake           (add FlatBuffers)
src/labios/CMakeLists.txt                (add new sources)
src/services/CMakeLists.txt              (add demo binary)
include/labios/transport/nats.h          (add request, reply_to in callback)
src/labios/transport/nats.cpp            (implement request)
include/labios/transport/redis.h         (add hset, hget, del, binary ops)
src/labios/transport/redis.cpp           (implement new methods)
tests/CMakeLists.txt                     (add new test targets)
src/services/labios-dispatcher.cpp       (real dispatching)
src/services/labios-worker.cpp           (real execution)
```

## 13. Demo

```bash
docker compose up -d
docker compose run --rm test          # unit + integration tests
docker compose exec test labios-demo  # 100MB write/read throughput demo
docker compose down -v
```

Expected output:
```
dispatcher  | [*] dispatcher ready (solver: round-robin, workers: 3)
worker-1    | [*] worker-1 ready (speed=5, capacity=10GB)
worker-2    | [*] worker-2 ready (speed=3, capacity=50GB)
worker-3    | [*] worker-3 ready (speed=1, capacity=200GB)

# Demo output:
Written: 100MB (100 labels) in 2.3s (43.5 MB/s)
Read:    100MB (100 labels) in 1.8s (55.6 MB/s)
Verify:  OK (all bytes match)
```
