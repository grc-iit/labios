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

The test image contains the compiled C++ test programs and the executable
Python SDK package (including FlatBuffers 24.3.25 runtime/bindings).

```bash
docker compose run --rm --build test
docker compose run --rm --build test labios-data-path-test "[deployment]"
docker compose run --rm --build -e LABIOS_PYTHON_LIVE=1 test \
  python3 -m pytest -q /opt/labios/tests/python -m live
docker compose run --rm --build test \
  python3 /opt/labios/examples/python/golden.py
```

The Python golden example uses only public Label I/O APIs and verifies exact
pipeline output bytes. The daemon images remain minimal service images and do
not promise an embedded SDK.

For a native build:

```bash
cmake --preset dev
cmake --build build/dev -j"$(nproc)"
ctest --test-dir build/dev --output-on-failure -L unit
```

Native integration tests additionally require reachable NATS and DragonflyDB.
Building the native Python module requires Python development headers; importing
the package requires `flatbuffers==24.3.25`. The built import root is
`build/dev/python`.

## Next steps

- [Deployment](deployment.md) — topology, persistence, storage, and security limits
- [Configuration](configuration.md) — implemented TOML and environment settings
- [SDK guide](sdk-guide.md) — client APIs
- [Backends](backends.md) — external backend adapters
- [MCP integration](mcp-integration.md) — public Label I/O tools, stable errors, golden path, and compatibility migration
