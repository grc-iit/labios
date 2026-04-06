# LABIOS Backend Guide

Backends are thin adapters that execute label operations against external storage
systems. LABIOS routes labels to backends by URI scheme. All intelligence
(shuffling, scheduling, caching, aggregation) happens upstream in the runtime.
The backend's job is narrow: last-mile execution.

## Architecture

```
Label arrives at Worker
        │
        ▼
BackendRegistry::resolve(uri.scheme)
        │
        ▼
BackendStore::put / get / del / query
        │
        ▼
External storage system (user's infrastructure)
```

The BackendRegistry maps URI schemes to BackendStore implementations. Workers
receive the full label (not just the data) so backends can use metadata like
intent, isolation level, and priority if the external system supports it.

## The BackendStore Concept

Every backend must satisfy the `BackendStore` C++ concept:

```cpp
template <typename T>
concept BackendStore = requires(T t, const labios::Label& label,
                                std::span<const std::byte> data,
                                const std::string& key) {
    { t.put(label, data) }    -> std::same_as<BackendResult>;
    { t.get(label) }          -> std::same_as<BackendDataResult>;
    { t.del(label) }          -> std::same_as<BackendResult>;
    { t.query(label) }        -> std::same_as<BackendQueryResult>;
    { t.scheme() }            -> std::convertible_to<std::string_view>;
};
```

### Return Types

```cpp
struct BackendResult {
    bool ok;
    std::string error;  // empty on success
};

struct BackendDataResult {
    bool ok;
    std::string error;
    std::vector<std::byte> data;
};

struct BackendQueryResult {
    bool ok;
    std::string error;
    std::string json;  // query results as JSON
};
```

## Built-in Backends

### PosixBackend (`file://`)

Reads and writes files on the local filesystem. The URI path maps directly to
the worker's data directory.

```
file:///data/output.bin → /labios/data/data/output.bin
```

Features:
- Offset-based writes and reads
- Path sanitization to prevent directory traversal
- Automatic parent directory creation
- File deletion via `del()`
- Query returns "not supported" (filesystem has no query language)

### KvBackend (`kv://`)

Connects to a user's Redis or Redis-compatible KV store. Not to be confused
with LABIOS's internal DragonflyDB warehouse.

```
kv://session/model-weights → Redis key "session/model-weights"
```

Configuration: set `LABIOS_KV_HOST` and `LABIOS_KV_PORT` (defaults to
`redis-kv:6380` in Docker Compose).

Features:
- Binary data storage with metadata hash (intent, isolation, priority)
- Optional TTL from label metadata
- Key listing via `query()` with SCAN
- App-namespaced keys prevent collisions between agents

### SqliteBackend (`sqlite://`)

Connects to a SQLite database for structured storage. Creates a `labios_store`
table automatically.

```
sqlite:///data/results.db?key=run-42
```

Schema:
```sql
CREATE TABLE IF NOT EXISTS labios_store (
    key TEXT PRIMARY KEY,
    data BLOB,
    intent TEXT,
    isolation TEXT,
    priority INTEGER,
    ttl INTEGER,
    app_id TEXT,
    created_at INTEGER,
    version INTEGER DEFAULT 1
);
```

Features:
- Prepared statements for put/get/del (no SQL injection)
- Versioned inserts (INSERT OR REPLACE increments version)
- Query filtering by intent, isolation, priority
- JSON output from query results

## Writing a New Backend

### Step 1: Create the Header

```cpp
// include/labios/backend/my_backend.h
#pragma once
#include <labios/backend/backend.h>
#include <labios/label.h>
#include <string_view>

namespace labios {

class MyBackend {
public:
    explicit MyBackend(const std::string& connection_string);
    ~MyBackend();

    BackendResult     put(const Label& label, std::span<const std::byte> data);
    BackendDataResult get(const Label& label);
    BackendResult     del(const Label& label);
    BackendQueryResult query(const Label& label);

    static constexpr std::string_view scheme() { return "myscheme"; }

private:
    // Connection handle to external system
};

} // namespace labios
```

### Step 2: Implement the Backend

```cpp
// src/labios/backend/my_backend.cpp
#include <labios/backend/my_backend.h>

namespace labios {

MyBackend::MyBackend(const std::string& connection_string) {
    // Connect to external system
}

BackendResult MyBackend::put(const Label& label,
                             std::span<const std::byte> data) {
    // Extract routing info from label
    auto key = label.dest_uri.empty() ? label.id : label.dest_uri;

    // Use label metadata if the external system supports it
    // label.intent, label.isolation, label.priority

    // Execute the write
    // ...

    return {.ok = true, .error = {}};
}

BackendDataResult MyBackend::get(const Label& label) {
    auto key = label.source_uri.empty() ? label.id : label.source_uri;
    // Execute the read
    std::vector<std::byte> data;
    // ...
    return {.ok = true, .error = {}, .data = std::move(data)};
}

BackendResult MyBackend::del(const Label& label) {
    // Execute the delete
    return {.ok = true, .error = {}};
}

BackendQueryResult MyBackend::query(const Label& label) {
    // Execute the query, return JSON
    return {.ok = true, .error = {}, .json = "[]"};
}

} // namespace labios
```

### Step 3: Register with BackendRegistry

In `src/services/labios-worker.cpp`, register the backend:

```cpp
#include <labios/backend/my_backend.h>

// In worker initialization:
registry.register_backend("myscheme",
    std::make_unique<MyBackend>(connection_string));
```

### Step 4: Add to CMakeLists.txt

```cmake
target_sources(labios PRIVATE
    src/labios/backend/my_backend.cpp
)
```

### Step 5: Add to Docker Compose (if needed)

If the backend connects to an external service, add a container to
`docker-compose.yml` for development:

```yaml
my-service:
  image: my-service:latest
  ports:
    - "9999:9999"
  healthcheck:
    test: ["CMD", "my-health-check"]
    interval: 2s
    start_period: 5s
```

## URI Scheme Reference

| Scheme | Backend | Status | External System |
|--------|---------|--------|-----------------|
| `file://` | PosixBackend | Implemented | Local/network filesystem |
| `kv://` | KvBackend | Implemented | Redis, DragonflyDB, KeyDB |
| `sqlite://` | SqliteBackend | Implemented | SQLite database |
| `s3://` | S3Backend | Planned | S3, MinIO, R2 |
| `vector://` | VectorBackend | Planned | ChromaDB, Pinecone, Weaviate |
| `graph://` | GraphBackend | Planned | Neo4j, DGraph |
| `pfs://` | PfsBackend | Planned | Lustre, GPFS, BeeGFS |
| `observe://` | (internal) | Implemented | Handled inline by dispatcher |

## Important: Internal Plumbing vs External Backends

LABIOS uses DragonflyDB and NATS internally as plumbing. These are completely
separate from backends.

| Concern | Component | Purpose |
|---------|-----------|---------|
| Internal plumbing | DragonflyDB (:6379) | Warehouse staging, catalog metadata, label status |
| Internal plumbing | NATS (:4222) | Label routing between components |
| External backend | User's Redis (:6380) | `kv://` label execution target |
| External backend | User's filesystem | `file://` label execution target |
| External backend | User's SQLite | `sqlite://` label execution target |

A `kv://` label connects to the user's Redis instance, not to LABIOS's internal
DragonflyDB. This separation is a core architectural invariant.
