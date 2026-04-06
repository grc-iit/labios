# LABIOS SDK Guide

Complete API reference for the LABIOS client libraries. All three language
bindings (C++, Python, C) wrap the same runtime. Every operation creates a label,
publishes it through NATS to the dispatcher, and waits for the worker's reply.

## API Layers

The client exposes eight layers, ordered from highest to lowest abstraction:

| Layer | Purpose | C++ | Python | C |
|-------|---------|-----|--------|---|
| Synchronous I/O | Read/write with blocking wait | `write()` / `read()` | `write()` / `read()` | `labios_write()` / `labios_read()` |
| Asynchronous I/O | Non-blocking with explicit wait | `async_write()` / `async_read()` / `wait()` | `async_write()` / `async_read()` / `wait()` | `labios_async_write()` / `labios_async_read()` / `labios_wait()` |
| Label-level | Build and publish raw labels | `create_label()` / `publish()` | `create_label()` / `publish()` | -- |
| URI-based | Route by URI scheme | `write_to()` / `read_from()` | `write_to()` / `read_from()` | -- |
| Intent-driven | Semantic intent on labels | `write_with_intent()` / `execute_pipeline()` | `write_with_intent()` / `execute_pipeline()` | -- |
| Channels | Streaming pub/sub | `create_channel()` / `publish_to_channel()` / `subscribe_to_channel()` | same | -- |
| Workspaces | Persistent shared state with ACL | `create_workspace()` / `workspace_put()` / `workspace_get()` | same | -- |
| Observability | Query runtime state | `observe()` | `observe()` | -- |

## Connecting

### C++

```cpp
#include <labios/client.h>
#include <labios/config.h>

// From TOML config file
auto cfg = labios::load_config("conf/labios.toml");
auto client = labios::connect(cfg);

// Or set connection details directly
labios::Config cfg;
cfg.nats_url = "nats://localhost:4222";
cfg.redis_host = "localhost";
cfg.redis_port = 6379;
auto client = labios::connect(cfg);
```

### Python

```python
import labios

# From config file
client = labios.connect("conf/labios.toml")

# Direct connection
client = labios.connect_to("nats://localhost:4222", "localhost", 6379)
```

### C

```c
#include <labios/labios.h>

labios_client_t client;
int rc = labios_connect("nats://localhost:4222", "localhost", 6379, &client);
if (rc != LABIOS_OK) { /* handle error */ }

// Or from config file
rc = labios_connect_config("conf/labios.toml", &client);

// Always disconnect when done
labios_disconnect(client);
```

## Synchronous I/O

Blocking write and read. The call returns only after the worker confirms
execution.

### C++

```cpp
// Write 1KB at offset 0
std::vector<std::byte> data(1024, std::byte{0x42});
client.write("/data/output.bin", data, 0);

// Read 1KB from offset 0
auto result = client.read("/data/output.bin", 0, 1024);
// result is std::vector<std::byte>
```

### Python

```python
client.write("/data/output.bin", b"\x42" * 1024, 0)
data = client.read("/data/output.bin", 0, 1024)
# data is bytes
```

### C

```c
char data[1024];
memset(data, 0x42, sizeof(data));
labios_write(client, "/data/output.bin", data, sizeof(data), 0);

char buf[1024];
size_t bytes_read;
labios_read(client, "/data/output.bin", 0, 1024, buf, sizeof(buf), &bytes_read);
```

## Asynchronous I/O

Non-blocking publish. The client returns immediately with a handle. Call `wait()`
to collect the result.

### C++

```cpp
auto pending = client.async_write("/data/big.bin", data, 0);
// ... do other work ...
client.wait(pending);

auto rpending = client.async_read("/data/big.bin", 0, size);
auto result = client.wait_read(rpending);
```

### Python

```python
pending = client.async_write("/data/big.bin", data, 0)
# ... do other work ...
client.wait(pending)

rpending = client.async_read("/data/big.bin", 0, size)
result = client.wait_read(rpending)
```

## Label-level API

For maximum control, build labels directly. This exposes the full label
structure from LABIOS-SPEC.md Section 2.

```cpp
auto label = client.create_label(labios::LabelType::Write,
                                 "/data/custom.bin", data, 0);
// Modify label fields before publishing
label.intent = labios::Intent::Checkpoint;
label.priority = 10;
label.isolation = labios::Isolation::Relaxed;

client.publish(label);
```

```python
label = client.create_label(labios.LabelType.Write,
                            "/data/custom.bin", data, 0)
label.intent = labios.Intent.Checkpoint
client.publish(label)
```

## URI-based I/O

Route labels to specific backends by URI scheme. The URI determines which
`BackendStore` handles the operation.

```cpp
// Write to the local filesystem
client.write_to("file:///data/output.bin", data, 0);

// Write to a KV store
client.write_to("kv://session/model-weights", data, 0);

// Write to SQLite
client.write_to("sqlite:///data/results.db?key=run-42", data, 0);

// Async variant
auto pending = client.async_write_to("kv://cache/embeddings", data, 0);
client.wait(pending);
```

```python
client.write_to("file:///data/output.bin", data, 0)
client.write_to("kv://session/model-weights", data, 0)
result = client.read_from("kv://session/model-weights", 0, 0)
```

Supported URI schemes:

| Scheme | Backend | Target |
|--------|---------|--------|
| `file://` | PosixBackend | Local or networked filesystem |
| `kv://` | KvBackend | User's Redis or compatible KV store |
| `sqlite://` | SqliteBackend | User's SQLite database |
| `s3://` | (planned) | S3-compatible object store |
| `vector://` | (planned) | Vector database (ChromaDB, Pinecone) |
| `graph://` | (planned) | Graph database (Neo4j, DGraph) |
| `pfs://` | (planned) | Parallel filesystem (Lustre, GPFS) |

## Intent-driven API

Attach semantic intent to labels. The scheduler uses intent to adjust worker
scoring weights (spec S7.4).

```cpp
client.write_with_intent("/data/checkpoint.bin", data, 0,
                         labios::Intent::Checkpoint);

// Execute a pipeline at the storage layer
client.execute_pipeline("file:///data/logs/",
                        {"grep:ERROR", "count", "head:100"});
```

```python
client.write_with_intent("/data/checkpoint.bin", data, 0,
                         labios.Intent.Checkpoint)

client.execute_pipeline("file:///data/logs/",
                        ["grep:ERROR", "count", "head:100"])
```

Available intents: `None`, `Checkpoint`, `Cache`, `ToolOutput`, `FinalResult`,
`Intermediate`, `SharedState`.

## Channels

Streaming coordination for multi-agent workflows. Channels provide ordered,
append-only message streams with backpressure, TTL, and subscriber callbacks.

```cpp
// Create a channel with TTL and max pending messages
client.create_channel("progress", /*ttl_seconds=*/300, /*max_pending=*/1000);

// Publish a message (returns monotonic sequence number)
uint64_t seq = client.publish_to_channel("progress", data);

// Subscribe with a callback
client.subscribe_to_channel("progress",
    [](const labios::ChannelMessage& msg) {
        std::cout << "seq=" << msg.sequence
                  << " size=" << msg.data.size() << "\n";
    });

// Destroy when done (auto-destroys if TTL expires)
client.destroy_channel("progress");
```

```python
client.create_channel("progress", ttl_seconds=300, max_pending=1000)
seq = client.publish_to_channel("progress", data)
client.subscribe_to_channel("progress", lambda msg: print(msg))
```

## Workspaces

Persistent shared state for multi-agent collaboration. Workspaces provide
per-key versioning, access control lists, and prefix-based listing.

```cpp
// Create a workspace with creator identity
client.create_workspace("project-data", "agent-1");

// Grant access to another agent
client.workspace_grant("project-data", "agent-2");

// Put/get/delete data
client.workspace_put("project-data", "model/v1", model_bytes);
auto data = client.workspace_get("project-data", "model/v1");
auto v2 = client.workspace_get("project-data", "model/v1", /*version=*/2);

// List keys by prefix
auto keys = client.workspace_list("project-data", "model/");

// Delete a key
client.workspace_del("project-data", "model/v1");
```

```python
client.create_workspace("project-data", "agent-1")
client.workspace_grant("project-data", "agent-2")
client.workspace_put("project-data", "model/v1", model_bytes)
data = client.workspace_get("project-data", "model/v1")
```

## Observability

Query the runtime state without side effects. The `observe()` method creates
an OBSERVE-type label that the dispatcher handles inline (no shuffling or
scheduling).

```cpp
// System health
auto health = client.observe("observe://system/health");

// Queue depth
auto depth = client.observe("observe://system/queue_depth");

// Worker scores
auto scores = client.observe("observe://system/worker_scores");

// Active channels
auto channels = client.observe("observe://system/channels");

// Active workspaces
auto workspaces = client.observe("observe://system/workspaces");

// Current configuration
auto config = client.observe("observe://system/config");

// Data location for a file
auto loc = client.observe("observe://data/location?path=/data/output.bin");

// Telemetry snapshot
auto metrics = client.observe("observe://system/metrics");
```

```python
health = client.observe("observe://system/health")
scores = client.observe("observe://system/worker_scores")
```

Returns a JSON string with the query results.

## Runtime Configuration

Modify runtime parameters without restarting. Changes take effect on the next
label batch.

```cpp
client.set_config("batch_size", "200");
client.set_config("scheduler_policy", "constraint");
client.set_config("aggregation_enabled", "false");
client.set_config("reply_timeout_ms", "10000");
client.set_config("cache_flush_interval_ms", "2000");
client.set_config("cache_read_policy", "always");

auto cfg = client.get_config();
```

```python
client.set_config("batch_size", "200")
client.set_config("scheduler_policy", "constraint")
```

## Error Handling

### C++ Exceptions

The C++ client throws `std::runtime_error` on connection failures and I/O
timeouts. Wrap calls in try/catch at the application boundary.

### C Error Codes

| Code | Meaning |
|------|---------|
| `LABIOS_OK` | Success |
| `LABIOS_ERR_CONNECT` | Failed to connect to NATS or DragonflyDB |
| `LABIOS_ERR_TIMEOUT` | Reply did not arrive within the configured timeout |
| `LABIOS_ERR_IO` | Worker reported an I/O error |
| `LABIOS_ERR_INVALID` | Invalid arguments (null pointer, zero size) |

### Python

Python raises `RuntimeError` for connection failures and `TimeoutError` for
reply timeouts. The native module prints a helpful message if the shared library
is not built.

## Enums

### LabelType

`Read`, `Write`, `Delete`, `Flush`, `Observe`, `Composite`

### Intent

`None`, `Checkpoint`, `Cache`, `ToolOutput`, `FinalResult`, `Intermediate`,
`SharedState`

### Isolation

`Strict`, `Session`, `Relaxed`

### Durability

`None`, `WriteThrough`, `Replicate`

## Building the Python Module

The Python SDK builds automatically with the CMake project:

```bash
cmake --preset dev
cmake --build build/dev -j$(nproc)
```

The shared module is placed at `build/dev/python/_labios.cpython-*.so`. Add
the directory to `PYTHONPATH` or install it:

```bash
export PYTHONPATH=/path/to/labios/build/dev/python:$PYTHONPATH
python -c "import labios; print(labios.connect_to.__doc__)"
```

Inside the Docker test container, the module is pre-installed.
