# Getting Started with LABIOS

LABIOS is the first agent I/O runtime. This guide takes you from clone to
running labels in under 5 minutes.

## Prerequisites

- Docker 24+ and Docker Compose v2
- cmake 3.25+ (native builds only)
- A C++20 compiler: GCC 12+ or Clang 15+ (native builds only)

## Start the Runtime

```bash
git clone https://github.com/akougkas/labios.git
cd labios
docker compose up -d
```

The first build takes a few minutes (multi-stage Dockerfile compiles all C++
binaries). Subsequent starts are instant.

## Verify

```bash
docker compose ps                       # all 10 services healthy
curl -s http://localhost:8222/healthz    # NATS JetStream ok
docker compose exec redis redis-cli ping  # DragonflyDB ok
```

All health checks must pass before clients can connect. The dispatcher and
workers block until NATS and DragonflyDB are reachable.

## Your First Label (Python SDK)

```python
import _labios as labios

client = labios.connect_to("nats://localhost:4222", "localhost", 6379)

# Write data through the runtime
client.write("/data/hello.dat", b"Hello from LABIOS!", 0)

# Read it back
data = client.read("/data/hello.dat", 0, 18)
print(data)  # b'Hello from LABIOS!'

# Check system health
health = client.observe("observe://system/health")
print(health)
```

The Python SDK uses pybind11 bindings to the C++ client. Build the module with
`cmake --preset dev && cmake --build build/dev -j$(nproc)` or use the Docker
test container where it is pre-built.

## Your First Label (C++)

```cpp
#include <labios/client.h>
#include <labios/config.h>
#include <cstring>
#include <iostream>

int main() {
    auto cfg = labios::load_config("conf/labios.toml");
    auto client = labios::connect(cfg);

    const char* msg = "Hello from LABIOS!";
    auto data = std::as_bytes(std::span(msg, std::strlen(msg)));

    client.write("/data/hello.dat", data, 0);

    auto result = client.read("/data/hello.dat", 0, 18);
    std::cout.write(reinterpret_cast<const char*>(result.data()), result.size());
    std::cout << '\n';
}
```

Link against `labios` and its dependencies (nats, hiredis, flatbuffers). The
CMake build handles this automatically via `target_link_libraries`.

## Your First Label (C API)

```c
#include <labios/labios.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    labios_client_t client;
    labios_connect("nats://localhost:4222", "localhost", 6379, &client);

    const char* msg = "Hello from LABIOS!";
    labios_write(client, "/data/hello.dat", msg, strlen(msg), 0);

    char buf[64];
    size_t bytes_read;
    labios_read(client, "/data/hello.dat", 0, 18, buf, sizeof(buf), &bytes_read);
    printf("%.*s\n", (int)bytes_read, buf);

    labios_disconnect(client);
}
```

The C API (`labios.h`) is designed for FFI consumers: Rust, Go, or any language
with a C calling convention.

## Connect Claude Code (MCP)

Add this to your Claude Code MCP settings (`~/.claude/settings.json` or
project-level `.mcp.json`):

```json
{
  "mcpServers": {
    "labios": {
      "command": "/path/to/labios/mcp/connect.sh"
    }
  }
}
```

The `connect.sh` script attaches to the MCP container via Docker stdio. The
runtime must be running (`docker compose up -d`) before Claude Code connects.

Once connected, Claude Code gains five MCP tools:

| Tool | Purpose |
|------|---------|
| `labios_observe` | Query runtime state (queue depth, worker scores, health) |
| `labios_store` | Store bulk data in workspace memory (persists across sessions) |
| `labios_retrieve` | Retrieve stored data, searching across memory tiers |
| `labios_process` | Process files through pipelines at the storage layer |
| `labios_knowledge` | Query stored knowledge across all memory tiers |

## What's Running

`docker compose up -d` starts 10 services:

| Service | Image | Port | Role |
|---------|-------|------|------|
| **nats** | nats:2.10-alpine | 4222, 8222 | Message broker with JetStream. All labels route through NATS subjects. |
| **redis** | DragonflyDB | 6379 | Internal warehouse and metadata store. Stages data in transit, tracks label status and file locations. NOT a backend for user data. |
| **redis-kv** | Redis 7 Alpine | 6380 | External KV backend for `kv://` URIs. Simulates user infrastructure. |
| **dispatcher** | labios-dispatcher | internal | Receives all labels, runs shuffler (aggregation, dependency detection), scheduler (4 policies), continuation processor, and telemetry publisher. |
| **worker-1** | labios-worker | internal | Fast worker: speed=5, energy=1, 10GB capacity. Handles latency-sensitive labels. |
| **worker-2** | labios-worker | internal | Balanced worker: speed=3, energy=3, 50GB capacity. General-purpose. |
| **worker-3** | labios-worker | internal | Capacity worker: speed=1, energy=5, 200GB capacity. Handles bulk storage. |
| **manager** | labios-manager | internal | Worker Manager: maintains bucket-sorted registry, handles elastic scaling decisions. |
| **test** | labios-test | internal | Runs smoke and integration tests on startup. |
| **mcp** | labios-mcp | internal | MCP server for coding agent integration. Connects Claude Code to the runtime. |

Workers have isolated data volumes (`/labios/data`). The dispatcher, manager,
and MCP server are stateless processes that rely on NATS and DragonflyDB.

## Native Build (Without Docker)

```bash
cmake --preset dev
cmake --build build/dev -j$(nproc)
ctest --test-dir build/dev          # run all 353 tests
```

You need NATS and DragonflyDB running locally. Set `LABIOS_NATS_URL` and
`LABIOS_REDIS_HOST` environment variables or edit `conf/labios.toml`.

## Run Tests

```bash
# Inside Docker (recommended)
docker compose exec test bash
./run_tests.sh

# Native
ctest --test-dir build/dev              # all 353 tests
ctest --test-dir build/dev -L unit      # 224 unit tests
ctest --test-dir build/dev -L smoke     # 68 smoke tests (needs live cluster)
ctest --test-dir build/dev -L kernel    # 15 science application replays
ctest --test-dir build/dev -L bench     # 25 agent I/O benchmarks
```

## Next Steps

- [docs/sdk-guide.md](sdk-guide.md) for the full API reference across all 8 layers
- [docs/deployment.md](deployment.md) for production deployment and scaling
- [docs/configuration.md](configuration.md) for tuning every runtime parameter
- [docs/backends.md](backends.md) for adding new storage backends
- [docs/mcp-integration.md](mcp-integration.md) for connecting coding agents via MCP
- [docs/architecture.md](architecture.md) for the complete implementation reference
