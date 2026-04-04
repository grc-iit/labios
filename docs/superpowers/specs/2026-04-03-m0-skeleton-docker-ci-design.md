# M0 Design: Skeleton + Docker + CI

**Date:** 2026-04-03
**Milestone:** 0 (Skeleton + Docker + CI)
**Constitutional ref:** LABIOS-2.0.md Section 6, Milestone 0

## Decisions

| Decision | Choice | Rationale |
|---|---|---|
| C++ client libs | cnats + hiredis, minimal RAII wrappers | Defer real transport abstraction to M1. M0 proves connectivity only. |
| Docker runtime base | debian:bookworm-slim | glibc-native, debuggable, zero libc compat risk for C++20/io_uring in later milestones |
| Smoke test scope | NATS message roundtrip + Redis confirmation key | Proves both infrastructure paths end-to-end through actual service code |
| Sanitizers | Skipped for M0 | Essence over theatrics. Add when there is real logic to sanitize. |
| Header layout | Classic include/src split | Public headers in include/labios/, implementations in src/labios/ |
| Service readiness | Docker healthchecks + depends_on condition | Declarative, no custom scripts |
| CI | Two jobs: native CMake build + Docker compose smoke test | Fast feedback on compile errors + full integration validation |
| Skeleton scope | Thin stubs, real plumbing | Only files that compile and do something. No empty scaffolding. |

## 1. CMake Structure

### Root CMakeLists.txt

- `cmake_minimum_required(VERSION 3.25)`
- `project(labios VERSION 2.0.0 LANGUAGES CXX)`
- `set(CMAKE_CXX_STANDARD 20)`, `CMAKE_CXX_STANDARD_REQUIRED ON`
- Includes `cmake/LabiosDependencies.cmake`
- Adds subdirectories: `src/labios`, `src/services`, `tests`

### CMakePresets.json

Three presets:

| Preset | CMAKE_BUILD_TYPE | Notes |
|---|---|---|
| `dev` | Debug | Warnings: -Wall -Wextra -Wpedantic -Werror |
| `release` | Release | LTO enabled |
| `ci` | RelWithDebInfo | For native CI build job |

All presets set `binaryDir` to `build/${presetName}`.

### Dependencies (cmake/LabiosDependencies.cmake)

All fetched via `FetchContent`. No system-installed deps.

| Dependency | Version | Purpose |
|---|---|---|
| cnats | v3.9.1+ (pin tag) | NATS C client |
| hiredis | v1.2.0+ (pin tag) | Redis C client |
| toml++ | v3.4.0 (pin tag) | TOML config parser (header-only) |
| Catch2 | v3.7.1 (pin tag) | Testing framework (test targets only) |

### Build Targets

| Target | Type | Sources | Links |
|---|---|---|---|
| `labios` | STATIC library | transport/nats.cpp, transport/redis.cpp, config.cpp | cnats, hiredis, toml++ |
| `labios-dispatcher` | Executable | services/labios-dispatcher.cpp | labios |
| `labios-worker` | Executable | services/labios-worker.cpp | labios |
| `labios-manager` | Executable | services/labios-manager.cpp | labios |
| `labios-smoke-test` | Executable (CTest) | tests/integration/smoke_test.cpp | labios, Catch2::Catch2WithMain |

## 2. Source Files

### include/labios/transport/nats.h

RAII wrapper around cnats.

```cpp
namespace labios::transport {

class NatsConnection {
public:
    explicit NatsConnection(std::string_view url);
    ~NatsConnection();

    NatsConnection(const NatsConnection&) = delete;
    NatsConnection& operator=(const NatsConnection&) = delete;
    NatsConnection(NatsConnection&&) noexcept;
    NatsConnection& operator=(NatsConnection&&) noexcept;

    void publish(std::string_view subject, std::span<const std::byte> data);
    void subscribe(std::string_view subject,
                   std::function<void(std::span<const std::byte>)> callback);

    [[nodiscard]] bool connected() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace labios::transport
```

Uses pimpl to hide cnats types from the public header.

### include/labios/transport/redis.h

RAII wrapper around hiredis.

```cpp
namespace labios::transport {

class RedisConnection {
public:
    RedisConnection(std::string_view host, int port);
    ~RedisConnection();

    RedisConnection(const RedisConnection&) = delete;
    RedisConnection& operator=(const RedisConnection&) = delete;
    RedisConnection(RedisConnection&&) noexcept;
    RedisConnection& operator=(RedisConnection&&) noexcept;

    void set(std::string_view key, std::string_view value);
    [[nodiscard]] std::optional<std::string> get(std::string_view key);

    [[nodiscard]] bool connected() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace labios::transport
```

### include/labios/config.h

```cpp
namespace labios {

struct Config {
    std::string nats_url = "nats://localhost:4222";
    std::string redis_host = "localhost";
    int redis_port = 6379;
    int worker_id = 0;
    int worker_speed = 1;
    std::string worker_capacity = "1GB";
    std::string service_name;
};

Config load_config(const std::filesystem::path& path);

} // namespace labios
```

Environment variables override TOML values. Precedence: env > TOML > defaults. Env var naming: `LABIOS_NATS_URL`, `LABIOS_REDIS_HOST`, `LABIOS_REDIS_PORT`, `LABIOS_WORKER_ID`, `LABIOS_WORKER_SPEED`, `LABIOS_WORKER_CAPACITY`.

### src/labios/transport/nats.cpp

Implements NatsConnection. Constructor calls `natsConnection_ConnectTo()`. Destructor calls `natsConnection_Destroy()`. `publish()` calls `natsConnection_PublishString()`. `subscribe()` creates a subscription with a C callback that forwards to the stored `std::function`.

### src/labios/transport/redis.cpp

Implements RedisConnection. Constructor calls `redisConnect()`. Destructor calls `redisFree()`. `set()`/`get()` use `redisCommand()` with RAII reply cleanup.

### src/labios/config.cpp

Parses TOML with toml++. Reads each field, then checks corresponding `LABIOS_*` environment variable. Env wins if set.

### src/services/labios-dispatcher.cpp

```
main():
  config = load_config("conf/labios.toml")  // or from LABIOS_CONFIG_PATH env
  nats = NatsConnection(config.nats_url)
  redis = RedisConnection(config.redis_host, config.redis_port)
  subscribe to "labios.labels" subject
  on message: log "received label" (placeholder for M1 dispatching)
  redis.set("labios:ready:dispatcher", "1")
  touch /tmp/labios-ready
  log "dispatcher ready" (to stdout, plain text with timestamp)
  block via std::jthread + stop_token
  on SIGINT/SIGTERM: request stop, destructors clean up connections
```

### src/services/labios-worker.cpp

```
main():
  config = load_config(...)
  nats = NatsConnection(config.nats_url)
  redis = RedisConnection(config.redis_host, config.redis_port)
  subscribe to "labios.worker.{id}" subject
  on message:
    log "worker {id} received message"
    redis.set("labios:confirmation:{msg_id}", "received_by_worker_{id}")
  redis.set("labios:ready:worker-{id}", "1")
  touch /tmp/labios-ready
  log "worker {id} ready (speed={speed}, capacity={capacity})"
  block via jthread + stop_token
  cooperative shutdown on signal
```

The worker writes a Redis confirmation key on every received message. This is what the smoke test asserts against.

### src/services/labios-manager.cpp

```
main():
  config = load_config(...)
  nats = NatsConnection(config.nats_url)
  redis = RedisConnection(config.redis_host, config.redis_port)
  redis.set("labios:ready:manager", "1")
  touch /tmp/labios-ready
  log "manager ready"
  block via jthread + stop_token
  cooperative shutdown on signal
```

Simplest service. Connects, logs, waits. Real logic arrives in M3 (worker scoring) and M4 (elastic workers).

### tests/integration/smoke_test.cpp

Catch2 test file with two test cases:

**Test 1: "NATS message flows from client to worker"**
1. Connect to NATS and Redis (URLs from env or defaults)
2. Generate a unique message ID (timestamp-based)
3. Publish a message to `labios.worker.1` containing the message ID
4. Poll Redis for key `labios:confirmation:{msg_id}` with timeout (2s, 100ms intervals)
5. Assert the key exists and value contains "received_by_worker_1"

**Test 2: "All services report ready"**
1. Connect to Redis
2. Check that dispatcher, worker-1/2/3, and manager have logged readiness
3. Implementation: each service sets a Redis key `labios:ready:{service_name}` on startup
4. Assert all five keys exist

### conf/labios.toml

```toml
[nats]
url = "nats://localhost:4222"

[redis]
host = "localhost"
port = 6379

[worker]
id = 0
speed = 1
capacity = "1GB"
```

## 3. Dockerfile

Multi-stage build. Single Dockerfile with named targets.

```dockerfile
# Stage 1: Builder
FROM debian:bookworm-slim AS builder
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake git ca-certificates pkg-config
COPY . /src
WORKDIR /src
RUN cmake --preset release && cmake --build build/release -j$(nproc)

# Stage 2: Dispatcher
FROM debian:bookworm-slim AS dispatcher
COPY --from=builder /src/build/release/src/services/labios-dispatcher /usr/local/bin/
COPY conf/ /etc/labios/
ENV LABIOS_CONFIG_PATH=/etc/labios/labios.toml
ENTRYPOINT ["labios-dispatcher"]

# Stage 3: Worker
FROM debian:bookworm-slim AS worker
COPY --from=builder /src/build/release/src/services/labios-worker /usr/local/bin/
COPY conf/ /etc/labios/
ENV LABIOS_CONFIG_PATH=/etc/labios/labios.toml
ENTRYPOINT ["labios-worker"]

# Stage 4: Manager
FROM debian:bookworm-slim AS manager
COPY --from=builder /src/build/release/src/services/labios-manager /usr/local/bin/
COPY conf/ /etc/labios/
ENV LABIOS_CONFIG_PATH=/etc/labios/labios.toml
ENTRYPOINT ["labios-manager"]

# Stage 5: Test
FROM debian:bookworm-slim AS test
COPY --from=builder /src/build/release/tests/labios-smoke-test /usr/local/bin/
ENV LABIOS_NATS_URL=nats://nats:4222
ENV LABIOS_REDIS_HOST=redis
ENTRYPOINT ["labios-smoke-test"]
```

## 4. docker-compose.yml

```yaml
services:
  nats:
    image: nats:2.10-alpine
    command: ["--jetstream", "--store_dir=/data", "--http_port=8222"]
    ports: ["4222:4222", "8222:8222"]
    healthcheck:
      test: ["CMD", "sh", "-c", "echo > /dev/tcp/localhost/4222"]
      interval: 2s
      timeout: 2s
      retries: 5
      start_period: 2s

  redis:
    image: redis:7-alpine
    ports: ["6379:6379"]
    healthcheck:
      test: ["CMD", "redis-cli", "ping"]
      interval: 2s
      timeout: 2s
      retries: 5

  dispatcher:
    build: { context: ., target: dispatcher }
    depends_on:
      nats: { condition: service_healthy }
      redis: { condition: service_healthy }
    environment:
      LABIOS_NATS_URL: "nats://nats:4222"
      LABIOS_REDIS_HOST: "redis"
    healthcheck:
      test: ["CMD-SHELL", "test -f /tmp/labios-ready"]
      interval: 2s
      retries: 10

  worker-1:
    build: { context: ., target: worker }
    depends_on:
      nats: { condition: service_healthy }
      redis: { condition: service_healthy }
    environment:
      LABIOS_NATS_URL: "nats://nats:4222"
      LABIOS_REDIS_HOST: "redis"
      LABIOS_WORKER_ID: "1"
      LABIOS_WORKER_SPEED: "5"
      LABIOS_WORKER_CAPACITY: "10GB"
    healthcheck:
      test: ["CMD-SHELL", "test -f /tmp/labios-ready"]
      interval: 2s
      retries: 10

  worker-2:
    build: { context: ., target: worker }
    depends_on:
      nats: { condition: service_healthy }
      redis: { condition: service_healthy }
    environment:
      LABIOS_NATS_URL: "nats://nats:4222"
      LABIOS_REDIS_HOST: "redis"
      LABIOS_WORKER_ID: "2"
      LABIOS_WORKER_SPEED: "3"
      LABIOS_WORKER_CAPACITY: "50GB"
    healthcheck:
      test: ["CMD-SHELL", "test -f /tmp/labios-ready"]
      interval: 2s
      retries: 10

  worker-3:
    build: { context: ., target: worker }
    depends_on:
      nats: { condition: service_healthy }
      redis: { condition: service_healthy }
    environment:
      LABIOS_NATS_URL: "nats://nats:4222"
      LABIOS_REDIS_HOST: "redis"
      LABIOS_WORKER_ID: "3"
      LABIOS_WORKER_SPEED: "1"
      LABIOS_WORKER_CAPACITY: "200GB"
    healthcheck:
      test: ["CMD-SHELL", "test -f /tmp/labios-ready"]
      interval: 2s
      retries: 10

  manager:
    build: { context: ., target: manager }
    depends_on:
      nats: { condition: service_healthy }
      redis: { condition: service_healthy }
    environment:
      LABIOS_NATS_URL: "nats://nats:4222"
      LABIOS_REDIS_HOST: "redis"
    healthcheck:
      test: ["CMD-SHELL", "test -f /tmp/labios-ready"]
      interval: 2s
      retries: 10

  test:
    build: { context: ., target: test }
    depends_on:
      dispatcher: { condition: service_healthy }
      worker-1: { condition: service_healthy }
      worker-2: { condition: service_healthy }
      worker-3: { condition: service_healthy }
      manager: { condition: service_healthy }
    environment:
      LABIOS_NATS_URL: "nats://nats:4222"
      LABIOS_REDIS_HOST: "redis"
```

The NATS healthcheck uses the monitoring endpoint (port 8222, enabled by default). Each LABIOS service touches `/tmp/labios-ready` after successful NATS+Redis connection, enabling the file-based healthcheck.

Three workers with distinct speed/capacity per the constitutional doc (Section 6, M0). Worker 1 is NVMe-tier (speed 5, 10GB), worker 2 is SSD-tier (speed 3, 50GB), worker 3 is HDD-tier (speed 1, 200GB).

## 5. GitHub Actions CI

File: `.github/workflows/ci.yml`

```yaml
name: CI
on: [push, pull_request]

jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Install deps
        run: sudo apt-get update && sudo apt-get install -y build-essential cmake git
      - name: Configure
        run: cmake --preset dev
      - name: Build
        run: cmake --build build/dev -j$(nproc)

  smoke:
    runs-on: ubuntu-latest
    needs: build
    steps:
      - uses: actions/checkout@v4
      - name: Build images
        run: docker compose build
      - name: Start system
        run: docker compose up -d
      - name: Run smoke test
        run: docker compose run --rm test
      - name: Teardown
        if: always()
        run: docker compose down -v
```

The `smoke` job depends on `build` succeeding. No point running Docker integration if the code does not compile.

## 6. File Inventory

Every file this milestone creates:

```
CMakeLists.txt
CMakePresets.json
cmake/LabiosDependencies.cmake
Dockerfile
docker-compose.yml
.github/workflows/ci.yml
conf/labios.toml
include/labios/config.h
include/labios/transport/nats.h
include/labios/transport/redis.h
src/labios/CMakeLists.txt
src/labios/config.cpp
src/labios/transport/nats.cpp
src/labios/transport/redis.cpp
src/services/CMakeLists.txt
src/services/labios-dispatcher.cpp
src/services/labios-worker.cpp
src/services/labios-manager.cpp
tests/CMakeLists.txt
tests/integration/smoke_test.cpp
```

20 files total. Every one compiles and serves a purpose.

## 7. Demo

```bash
docker compose up -d
# All services start, connect to NATS/Redis, log readiness
# docker compose logs shows:
#   dispatcher  | [2026-04-03T...] dispatcher ready
#   worker-1    | [2026-04-03T...] worker 1 ready (speed=5, capacity=10GB)
#   worker-2    | [2026-04-03T...] worker 2 ready (speed=3, capacity=50GB)
#   worker-3    | [2026-04-03T...] worker 3 ready (speed=1, capacity=200GB)
#   manager     | [2026-04-03T...] manager ready

docker compose run --rm test
# smoke_test: NATS message flows from client to worker ... PASSED
# smoke_test: All services report ready ..................... PASSED

docker compose down -v
```
