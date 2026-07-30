# Getting Started with LABIOS

LABIOS is a Label I/O runtime. The default Docker Compose topology is a
**single-host reference deployment** for development and evaluation; it is not a
production or multi-node deployment.

## Prerequisites

- Git
- Docker 24 or newer
- Docker Compose v2.20 or newer (`docker compose`, not legacy `docker-compose`)

CMake, a compiler, and Python are **not** required for this Docker path.

## Clone and start

```bash
git clone https://github.com/akougkas/labios.git
cd labios
docker compose up -d --build --wait
```

`--wait` returns only after every daemon in the default topology reports
healthy. The first start builds the C++ service images; later starts reuse them.

> **Development-only security:** ports 4222, 8222, 6379, and 6380 are bound to
> localhost without application credentials or TLS. Do not expose this topology
> to another host or use its defaults in production.

## Perform verified real I/O

Run the demo from the dispatcher image, which contains `labios-demo` alongside the daemon binary:

```bash
docker compose exec dispatcher labios-demo
```

A successful run ends with:

```text
LABIOS demo: verified 24 writes and read-backs across shared file and SQLite backends
```

Those operations traverse the client, JetStream, dispatcher, round-robin
scheduler, and workers. The demo writes and reads both `file://` and `sqlite://`
resources and compares every byte. A wait timeout is reported as a timeout; it
does not cancel the underlying label.

## Inspect health

```bash
docker compose ps
curl -fsS http://127.0.0.1:8222/healthz
docker compose exec redis redis-cli ping
```

The running daemon rows should show `healthy`, NATS should return `ok`, and
DragonflyDB should return `PONG`. The `test` service is intentionally absent:
it is a non-daemon profile used by `docker compose run`.

Useful logs:

```bash
docker compose logs dispatcher
docker compose logs worker-1 worker-2 worker-3
```

## Stop or reset

Keep persistent state for the next start:

```bash
docker compose down
```

Remove the JetStream, DragonflyDB, external Redis fixture, and shared worker
volumes as well:

```bash
docker compose --profile test down -v --remove-orphans
```

## Run tests

The test image contains the compiled C++ test programs; it does not contain or
claim to contain the Python SDK.

```bash
docker compose run --rm --build test
docker compose run --rm --build test labios-data-path-test "[deployment]"
```

For a native build:

```bash
cmake --preset dev
cmake --build build/dev -j"$(nproc)"
ctest --test-dir build/dev --output-on-failure -L unit
```

Native integration tests additionally require reachable NATS and DragonflyDB.

## Next steps

- [Deployment](deployment.md) — topology, persistence, storage, and security limits
- [Configuration](configuration.md) — implemented TOML and environment settings
- [SDK guide](sdk-guide.md) — client APIs
- [Backends](backends.md) — external backend adapters
- [MCP integration](mcp-integration.md) — current MCP prototype and limitations
