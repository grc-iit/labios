# M0: Skeleton + Docker + CI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a compiling C++20 project with NATS/Redis connectivity, Docker Compose orchestration, and CI proving end-to-end message delivery.

**Architecture:** Static library (liblabios) with transport wrappers and config loading. Three service executables link against it. A Catch2 smoke test verifies NATS message roundtrip and Redis confirmation through live services orchestrated by Docker Compose.

**Tech Stack:** C++20, CMake 3.25+, cnats, hiredis, toml++, Catch2, Docker, GitHub Actions

**Spec:** `docs/superpowers/specs/2026-04-03-m0-skeleton-docker-ci-design.md`

---

## File Map

| File | Responsibility |
|---|---|
| `CMakeLists.txt` | Root project definition, C++20 standard, subdirectory wiring |
| `CMakePresets.json` | dev/release/ci build presets |
| `cmake/LabiosDependencies.cmake` | FetchContent for cnats, hiredis, toml++, Catch2 |
| `conf/labios.toml` | Default config with NATS/Redis URLs and worker defaults |
| `include/labios/config.h` | Config struct + load_config declaration |
| `src/labios/config.cpp` | TOML parsing with env var override |
| `src/labios/CMakeLists.txt` | liblabios static library target |
| `include/labios/transport/nats.h` | NatsConnection RAII class (pimpl) |
| `src/labios/transport/nats.cpp` | cnats implementation |
| `include/labios/transport/redis.h` | RedisConnection RAII class (pimpl) |
| `src/labios/transport/redis.cpp` | hiredis implementation |
| `src/services/CMakeLists.txt` | Service executable targets |
| `src/services/labios-dispatcher.cpp` | Dispatcher main: connect, subscribe, wait |
| `src/services/labios-worker.cpp` | Worker main: connect, subscribe, confirm via Redis |
| `src/services/labios-manager.cpp` | Manager main: connect, wait |
| `tests/CMakeLists.txt` | Test targets |
| `tests/unit/config_test.cpp` | Unit test for config loading |
| `tests/integration/smoke_test.cpp` | Integration test: NATS roundtrip + Redis check |
| `Dockerfile` | Multi-stage: builder, dispatcher, worker, manager, test |
| `docker-compose.yml` | Full system: NATS, Redis, 3 workers, dispatcher, manager, test |
| `.github/workflows/ci.yml` | Native build + Docker smoke test |

---

### Task 1: CMake Build System

**Files:**
- Create: `CMakeLists.txt`
- Create: `CMakePresets.json`
- Create: `cmake/LabiosDependencies.cmake`
- Create: `src/labios/CMakeLists.txt`
- Create: `src/services/CMakeLists.txt`
- Create: `tests/CMakeLists.txt`

- [ ] **Step 1: Create cmake/LabiosDependencies.cmake**

```cmake
include(FetchContent)

FetchContent_Declare(
    cnats
    GIT_REPOSITORY https://github.com/nats-io/nats.c.git
    GIT_TAG        v3.9.1
)

FetchContent_Declare(
    hiredis
    GIT_REPOSITORY https://github.com/redis/hiredis.git
    GIT_TAG        v1.2.0
)

FetchContent_Declare(
    tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG        v3.4.0
)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.7.1
)

# cnats build options
set(NATS_BUILD_STREAMING OFF CACHE BOOL "" FORCE)
set(NATS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

# hiredis build options
set(DISABLE_TESTS ON CACHE BOOL "" FORCE)
set(ENABLE_SSL OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(cnats hiredis tomlplusplus Catch2)
```

- [ ] **Step 2: Create src/labios/CMakeLists.txt**

This is a placeholder that will gain sources in Task 2 and 3. Start with an INTERFACE library so the project configures.

```cmake
add_library(labios STATIC)

target_include_directories(labios
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(labios
    PUBLIC
        nats
        hiredis
        tomlplusplus::tomlplusplus
)

target_compile_features(labios PUBLIC cxx_std_20)
```

- [ ] **Step 3: Create src/services/CMakeLists.txt**

Placeholder with no executables yet (sources added in Task 5).

```cmake
# Service executables added as sources are created
```

- [ ] **Step 4: Create tests/CMakeLists.txt**

```cmake
find_package(Catch2 3 REQUIRED)
include(Catch2::Catch2WithMain)
include(CTest)

# Test targets added as sources are created
```

- [ ] **Step 5: Create CMakePresets.json**

```json
{
    "version": 6,
    "cmakeMinimumRequired": { "major": 3, "minor": 25, "patch": 0 },
    "configurePresets": [
        {
            "name": "dev",
            "binaryDir": "${sourceDir}/build/dev",
            "cacheVariables": {
                "CMAKE_BUILD_TYPE": "Debug",
                "CMAKE_CXX_FLAGS": "-Wall -Wextra -Wpedantic -Werror",
                "CMAKE_EXPORT_COMPILE_COMMANDS": "ON"
            }
        },
        {
            "name": "release",
            "binaryDir": "${sourceDir}/build/release",
            "cacheVariables": {
                "CMAKE_BUILD_TYPE": "Release",
                "CMAKE_INTERPROCEDURAL_OPTIMIZATION": "ON"
            }
        },
        {
            "name": "ci",
            "binaryDir": "${sourceDir}/build/ci",
            "cacheVariables": {
                "CMAKE_BUILD_TYPE": "RelWithDebInfo"
            }
        }
    ]
}
```

- [ ] **Step 6: Create root CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.25)
project(labios VERSION 2.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(cmake/LabiosDependencies.cmake)

add_subdirectory(src/labios)
add_subdirectory(src/services)
add_subdirectory(tests)
```

Note: `LANGUAGES C CXX` because cnats is a C library.

- [ ] **Step 7: Test that the project configures and builds**

Run: `cmake --preset dev && cmake --build build/dev -j$(nproc)`
Expected: Configures successfully, fetches all dependencies, builds the empty liblabios static library with no errors. Warnings treated as errors.

- [ ] **Step 8: Commit**

```bash
git add CMakeLists.txt CMakePresets.json cmake/LabiosDependencies.cmake \
    src/labios/CMakeLists.txt src/services/CMakeLists.txt tests/CMakeLists.txt
git commit -m "build: CMake skeleton with C++20 presets and FetchContent deps"
```

---

### Task 2: Config System

**Files:**
- Create: `include/labios/config.h`
- Create: `src/labios/config.cpp`
- Create: `conf/labios.toml`
- Create: `tests/unit/config_test.cpp`
- Modify: `src/labios/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Create conf/labios.toml**

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

- [ ] **Step 2: Create include/labios/config.h**

```cpp
#pragma once

#include <filesystem>
#include <string>

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

/// Load config from TOML file. Environment variables override file values.
/// Env vars: LABIOS_NATS_URL, LABIOS_REDIS_HOST, LABIOS_REDIS_PORT,
///           LABIOS_WORKER_ID, LABIOS_WORKER_SPEED, LABIOS_WORKER_CAPACITY
Config load_config(const std::filesystem::path& path);

} // namespace labios
```

- [ ] **Step 3: Write the failing test for config loading**

Create `tests/unit/config_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <labios/config.h>
#include <cstdlib>
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

static fs::path write_temp_toml(const std::string& content) {
    auto path = fs::temp_directory_path() / "labios_test_config.toml";
    std::ofstream out(path);
    out << content;
    return path;
}

TEST_CASE("load_config reads TOML values", "[config]") {
    auto path = write_temp_toml(R"(
[nats]
url = "nats://testhost:4222"

[redis]
host = "redis-test"
port = 7777

[worker]
id = 42
speed = 5
capacity = "100GB"
)");

    auto cfg = labios::load_config(path);
    REQUIRE(cfg.nats_url == "nats://testhost:4222");
    REQUIRE(cfg.redis_host == "redis-test");
    REQUIRE(cfg.redis_port == 7777);
    REQUIRE(cfg.worker_id == 42);
    REQUIRE(cfg.worker_speed == 5);
    REQUIRE(cfg.worker_capacity == "100GB");

    fs::remove(path);
}

TEST_CASE("load_config uses defaults for missing file", "[config]") {
    auto cfg = labios::load_config("/nonexistent/path.toml");
    REQUIRE(cfg.nats_url == "nats://localhost:4222");
    REQUIRE(cfg.redis_host == "localhost");
    REQUIRE(cfg.redis_port == 6379);
}

TEST_CASE("environment variables override TOML values", "[config]") {
    auto path = write_temp_toml(R"(
[nats]
url = "nats://toml-host:4222"

[redis]
host = "toml-redis"
port = 6379
)");

    setenv("LABIOS_NATS_URL", "nats://env-host:9999", 1);
    setenv("LABIOS_REDIS_HOST", "env-redis", 1);

    auto cfg = labios::load_config(path);
    REQUIRE(cfg.nats_url == "nats://env-host:9999");
    REQUIRE(cfg.redis_host == "env-redis");

    unsetenv("LABIOS_NATS_URL");
    unsetenv("LABIOS_REDIS_HOST");
    fs::remove(path);
}
```

- [ ] **Step 4: Add config_test to tests/CMakeLists.txt**

Replace the contents of `tests/CMakeLists.txt` with:

```cmake
find_package(Catch2 3 REQUIRED)
include(CTest)
include(Catch2::Catch2WithMain)

add_executable(labios-config-test unit/config_test.cpp)
target_link_libraries(labios-config-test PRIVATE labios Catch2::Catch2WithMain)
catch_discover_tests(labios-config-test)
```

- [ ] **Step 5: Run test to verify it fails**

Run: `cmake --build build/dev -j$(nproc) && ctest --test-dir build/dev --output-on-failure`
Expected: Link error because `labios::load_config` is not defined yet.

- [ ] **Step 6: Implement config.cpp**

Create `src/labios/config.cpp`:

```cpp
#include <labios/config.h>

#include <toml++/toml.hpp>

#include <cstdlib>
#include <filesystem>
#include <string>

namespace labios {

namespace {

std::string env_or(const char* name, const std::string& fallback) {
    const char* val = std::getenv(name);
    if (val != nullptr && val[0] != '\0') return val;
    return fallback;
}

int env_int_or(const char* name, int fallback) {
    const char* val = std::getenv(name);
    if (val != nullptr && val[0] != '\0') return std::stoi(val);
    return fallback;
}

} // namespace

Config load_config(const std::filesystem::path& path) {
    Config cfg;

    if (std::filesystem::exists(path)) {
        auto tbl = toml::parse_file(path.string());

        cfg.nats_url       = tbl["nats"]["url"].value_or(cfg.nats_url);
        cfg.redis_host     = tbl["redis"]["host"].value_or(cfg.redis_host);
        cfg.redis_port     = tbl["redis"]["port"].value_or(cfg.redis_port);
        cfg.worker_id      = tbl["worker"]["id"].value_or(cfg.worker_id);
        cfg.worker_speed   = tbl["worker"]["speed"].value_or(cfg.worker_speed);
        cfg.worker_capacity = tbl["worker"]["capacity"].value_or(cfg.worker_capacity);
    }

    cfg.nats_url        = env_or("LABIOS_NATS_URL", cfg.nats_url);
    cfg.redis_host      = env_or("LABIOS_REDIS_HOST", cfg.redis_host);
    cfg.redis_port      = env_int_or("LABIOS_REDIS_PORT", cfg.redis_port);
    cfg.worker_id       = env_int_or("LABIOS_WORKER_ID", cfg.worker_id);
    cfg.worker_speed    = env_int_or("LABIOS_WORKER_SPEED", cfg.worker_speed);
    cfg.worker_capacity = env_or("LABIOS_WORKER_CAPACITY", cfg.worker_capacity);

    return cfg;
}

} // namespace labios
```

- [ ] **Step 7: Add config.cpp to liblabios sources**

Update `src/labios/CMakeLists.txt`, adding config.cpp as a source:

```cmake
add_library(labios STATIC
    config.cpp
)

target_include_directories(labios
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(labios
    PUBLIC
        nats
        hiredis
        tomlplusplus::tomlplusplus
)

target_compile_features(labios PUBLIC cxx_std_20)
```

- [ ] **Step 8: Run tests to verify they pass**

Run: `cmake --build build/dev -j$(nproc) && ctest --test-dir build/dev --output-on-failure`
Expected: All 3 config tests pass.

- [ ] **Step 9: Commit**

```bash
git add include/labios/config.h src/labios/config.cpp src/labios/CMakeLists.txt \
    conf/labios.toml tests/unit/config_test.cpp tests/CMakeLists.txt
git commit -m "feat: TOML config loader with env var override"
```

---

### Task 3: Redis Transport Wrapper

**Files:**
- Create: `include/labios/transport/redis.h`
- Create: `src/labios/transport/redis.cpp`
- Modify: `src/labios/CMakeLists.txt`

- [ ] **Step 1: Create include/labios/transport/redis.h**

```cpp
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

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

- [ ] **Step 2: Create src/labios/transport/redis.cpp**

```cpp
#include <labios/transport/redis.h>

#include <hiredis/hiredis.h>

#include <stdexcept>
#include <utility>

namespace labios::transport {

struct RedisConnection::Impl {
    redisContext* ctx = nullptr;

    ~Impl() {
        if (ctx != nullptr) redisFree(ctx);
    }
};

RedisConnection::RedisConnection(std::string_view host, int port)
    : impl_(std::make_unique<Impl>()) {
    impl_->ctx = redisConnect(std::string(host).c_str(), port);
    if (impl_->ctx == nullptr) {
        throw std::runtime_error("redis: allocation failure");
    }
    if (impl_->ctx->err != 0) {
        std::string msg = "redis: " + std::string(impl_->ctx->errstr);
        redisFree(impl_->ctx);
        impl_->ctx = nullptr;
        throw std::runtime_error(msg);
    }
}

RedisConnection::~RedisConnection() = default;
RedisConnection::RedisConnection(RedisConnection&&) noexcept = default;
RedisConnection& RedisConnection::operator=(RedisConnection&&) noexcept = default;

void RedisConnection::set(std::string_view key, std::string_view value) {
    auto* reply = static_cast<redisReply*>(
        redisCommand(impl_->ctx, "SET %b %b",
                     key.data(), key.size(),
                     value.data(), value.size()));
    if (reply == nullptr) {
        throw std::runtime_error("redis SET failed: " + std::string(impl_->ctx->errstr));
    }
    freeReplyObject(reply);
}

std::optional<std::string> RedisConnection::get(std::string_view key) {
    auto* reply = static_cast<redisReply*>(
        redisCommand(impl_->ctx, "GET %b", key.data(), key.size()));
    if (reply == nullptr) {
        throw std::runtime_error("redis GET failed: " + std::string(impl_->ctx->errstr));
    }
    std::optional<std::string> result;
    if (reply->type == REDIS_REPLY_STRING) {
        result.emplace(reply->str, reply->len);
    }
    freeReplyObject(reply);
    return result;
}

bool RedisConnection::connected() const {
    return impl_ && impl_->ctx != nullptr && impl_->ctx->err == 0;
}

} // namespace labios::transport
```

- [ ] **Step 3: Add transport/redis.cpp to liblabios**

Update `src/labios/CMakeLists.txt` sources:

```cmake
add_library(labios STATIC
    config.cpp
    transport/redis.cpp
)

target_include_directories(labios
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(labios
    PUBLIC
        nats
        hiredis
        tomlplusplus::tomlplusplus
)

target_compile_features(labios PUBLIC cxx_std_20)
```

- [ ] **Step 4: Verify it compiles**

Run: `cmake --build build/dev -j$(nproc)`
Expected: Compiles with no errors. Previous config tests still pass.

- [ ] **Step 5: Commit**

```bash
git add include/labios/transport/redis.h src/labios/transport/redis.cpp \
    src/labios/CMakeLists.txt
git commit -m "feat: Redis RAII transport wrapper (hiredis)"
```

---

### Task 4: NATS Transport Wrapper

**Files:**
- Create: `include/labios/transport/nats.h`
- Create: `src/labios/transport/nats.cpp`
- Modify: `src/labios/CMakeLists.txt`

- [ ] **Step 1: Create include/labios/transport/nats.h**

```cpp
#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>

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

    /// Convenience overload for string payloads.
    void publish(std::string_view subject, std::string_view data);

    using MessageCallback = std::function<void(std::string_view subject,
                                               std::span<const std::byte> data)>;

    /// Subscribe to a subject. The callback is invoked on a cnats-managed thread.
    /// The subscription lives until this NatsConnection is destroyed.
    void subscribe(std::string_view subject, MessageCallback callback);

    /// Drain pending messages and flush. Call before destruction if you need
    /// all published messages to reach the server.
    void drain();

    [[nodiscard]] bool connected() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace labios::transport
```

- [ ] **Step 2: Create src/labios/transport/nats.cpp**

```cpp
#include <labios/transport/nats.h>

#include <nats/nats.h>

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace labios::transport {

struct NatsConnection::Impl {
    natsConnection* conn = nullptr;
    std::vector<natsSubscription*> subs;
    MessageCallback cb;

    ~Impl() {
        for (auto* sub : subs) {
            natsSubscription_Drain(sub);
            natsSubscription_Destroy(sub);
        }
        if (conn != nullptr) {
            natsConnection_Drain(conn);
            natsConnection_Destroy(conn);
        }
    }
};

static void on_message(natsConnection* /*nc*/, natsSubscription* /*sub*/,
                       natsMsg* msg, void* closure) {
    auto* impl = static_cast<NatsConnection::Impl*>(closure);
    if (impl->cb) {
        const char* subj = natsMsg_GetSubject(msg);
        const char* data = natsMsg_GetData(msg);
        int len = natsMsg_GetDataLength(msg);
        auto span = std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(data),
            static_cast<size_t>(len));
        impl->cb(subj ? subj : "", span);
    }
    natsMsg_Destroy(msg);
}

NatsConnection::NatsConnection(std::string_view url)
    : impl_(std::make_unique<Impl>()) {
    natsOptions* opts = nullptr;
    natsStatus s = natsOptions_Create(&opts);
    if (s != NATS_OK) {
        throw std::runtime_error("nats: failed to create options");
    }
    natsOptions_SetURL(opts, std::string(url).c_str());
    natsOptions_SetRetryOnFailedConnect(opts, true, nullptr, nullptr);
    natsOptions_SetMaxReconnect(opts, 10);
    natsOptions_SetReconnectWait(opts, 500);

    s = natsConnection_Connect(&impl_->conn, opts);
    natsOptions_Destroy(opts);
    if (s != NATS_OK) {
        throw std::runtime_error("nats: connection failed to " + std::string(url));
    }
}

NatsConnection::~NatsConnection() = default;
NatsConnection::NatsConnection(NatsConnection&&) noexcept = default;
NatsConnection& NatsConnection::operator=(NatsConnection&&) noexcept = default;

void NatsConnection::publish(std::string_view subject,
                             std::span<const std::byte> data) {
    natsStatus s = natsConnection_Publish(
        impl_->conn,
        std::string(subject).c_str(),
        reinterpret_cast<const void*>(data.data()),
        static_cast<int>(data.size()));
    if (s != NATS_OK) {
        throw std::runtime_error("nats: publish failed on " + std::string(subject));
    }
}

void NatsConnection::publish(std::string_view subject, std::string_view data) {
    auto span = std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(data.data()), data.size());
    publish(subject, span);
}

void NatsConnection::subscribe(std::string_view subject,
                               MessageCallback callback) {
    impl_->cb = std::move(callback);
    natsSubscription* sub = nullptr;
    natsStatus s = natsSubscription_Create(
        &sub, impl_->conn,
        std::string(subject).c_str(),
        on_message, impl_.get());
    if (s != NATS_OK) {
        throw std::runtime_error("nats: subscribe failed on " + std::string(subject));
    }
    impl_->subs.push_back(sub);
}

void NatsConnection::drain() {
    if (impl_->conn != nullptr) {
        natsConnection_FlushTimeout(impl_->conn, 2000);
    }
}

bool NatsConnection::connected() const {
    return impl_ && impl_->conn != nullptr &&
           natsConnection_Status(impl_->conn) == NATS_CONN_STATUS_CONNECTED;
}

} // namespace labios::transport
```

- [ ] **Step 3: Add transport/nats.cpp to liblabios**

Update `src/labios/CMakeLists.txt` sources:

```cmake
add_library(labios STATIC
    config.cpp
    transport/nats.cpp
    transport/redis.cpp
)

target_include_directories(labios
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(labios
    PUBLIC
        nats
        hiredis
        tomlplusplus::tomlplusplus
)

target_compile_features(labios PUBLIC cxx_std_20)
```

- [ ] **Step 4: Verify it compiles**

Run: `cmake --build build/dev -j$(nproc)`
Expected: Compiles cleanly. Config tests still pass (they don't require NATS).

- [ ] **Step 5: Commit**

```bash
git add include/labios/transport/nats.h src/labios/transport/nats.cpp \
    src/labios/CMakeLists.txt
git commit -m "feat: NATS RAII transport wrapper (cnats)"
```

---

### Task 5: Service Executables

**Files:**
- Create: `src/services/labios-dispatcher.cpp`
- Create: `src/services/labios-worker.cpp`
- Create: `src/services/labios-manager.cpp`
- Modify: `src/services/CMakeLists.txt`

- [ ] **Step 1: Create src/services/labios-dispatcher.cpp**

```cpp
#include <labios/config.h>
#include <labios/transport/nats.h>
#include <labios/transport/redis.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

static std::atomic<bool> g_running{true};

static void signal_handler(int /*sig*/) { g_running.store(false); }

static std::string timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", std::gmtime(&time));
    return buf;
}

int main() {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    const char* config_path = std::getenv("LABIOS_CONFIG_PATH");
    auto cfg = labios::load_config(config_path ? config_path : "conf/labios.toml");

    labios::transport::NatsConnection nats(cfg.nats_url);
    labios::transport::RedisConnection redis(cfg.redis_host, cfg.redis_port);

    nats.subscribe("labios.labels",
        [](std::string_view /*subject*/, std::span<const std::byte> data) {
            std::cout << "[" << timestamp() << "] dispatcher: received label ("
                      << data.size() << " bytes)\n";
        });

    redis.set("labios:ready:dispatcher", "1");

    // Signal healthcheck
    { std::ofstream touch("/tmp/labios-ready"); }

    std::cout << "[" << timestamp() << "] dispatcher ready\n" << std::flush;

    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "[" << timestamp() << "] dispatcher shutting down\n";
    return 0;
}
```

- [ ] **Step 2: Create src/services/labios-worker.cpp**

```cpp
#include <labios/config.h>
#include <labios/transport/nats.h>
#include <labios/transport/redis.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

static std::atomic<bool> g_running{true};

static void signal_handler(int /*sig*/) { g_running.store(false); }

static std::string timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", std::gmtime(&time));
    return buf;
}

int main() {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    const char* config_path = std::getenv("LABIOS_CONFIG_PATH");
    auto cfg = labios::load_config(config_path ? config_path : "conf/labios.toml");

    labios::transport::NatsConnection nats(cfg.nats_url);
    labios::transport::RedisConnection redis(cfg.redis_host, cfg.redis_port);

    std::string worker_subject = "labios.worker." + std::to_string(cfg.worker_id);
    std::string worker_name = "worker-" + std::to_string(cfg.worker_id);

    nats.subscribe(worker_subject,
        [&redis, &cfg](std::string_view /*subject*/,
                       std::span<const std::byte> data) {
            std::string msg_id(reinterpret_cast<const char*>(data.data()),
                               data.size());
            std::cout << "[" << timestamp() << "] worker " << cfg.worker_id
                      << ": received message " << msg_id << "\n" << std::flush;

            std::string key = "labios:confirmation:" + msg_id;
            std::string val = "received_by_worker_" + std::to_string(cfg.worker_id);
            redis.set(key, val);
        });

    redis.set("labios:ready:" + worker_name, "1");

    // Signal healthcheck
    { std::ofstream touch("/tmp/labios-ready"); }

    std::cout << "[" << timestamp() << "] " << worker_name
              << " ready (speed=" << cfg.worker_speed
              << ", capacity=" << cfg.worker_capacity << ")\n"
              << std::flush;

    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "[" << timestamp() << "] " << worker_name
              << " shutting down\n";
    return 0;
}
```

- [ ] **Step 3: Create src/services/labios-manager.cpp**

```cpp
#include <labios/config.h>
#include <labios/transport/nats.h>
#include <labios/transport/redis.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

static std::atomic<bool> g_running{true};

static void signal_handler(int /*sig*/) { g_running.store(false); }

static std::string timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", std::gmtime(&time));
    return buf;
}

int main() {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    const char* config_path = std::getenv("LABIOS_CONFIG_PATH");
    auto cfg = labios::load_config(config_path ? config_path : "conf/labios.toml");

    labios::transport::NatsConnection nats(cfg.nats_url);
    labios::transport::RedisConnection redis(cfg.redis_host, cfg.redis_port);

    redis.set("labios:ready:manager", "1");

    // Signal healthcheck
    { std::ofstream touch("/tmp/labios-ready"); }

    std::cout << "[" << timestamp() << "] manager ready\n" << std::flush;

    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "[" << timestamp() << "] manager shutting down\n";
    return 0;
}
```

- [ ] **Step 4: Update src/services/CMakeLists.txt**

```cmake
add_executable(labios-dispatcher labios-dispatcher.cpp)
target_link_libraries(labios-dispatcher PRIVATE labios)

add_executable(labios-worker labios-worker.cpp)
target_link_libraries(labios-worker PRIVATE labios)

add_executable(labios-manager labios-manager.cpp)
target_link_libraries(labios-manager PRIVATE labios)
```

- [ ] **Step 5: Verify all three executables compile**

Run: `cmake --build build/dev -j$(nproc)`
Expected: Three new executables in `build/dev/src/services/`. Config tests still pass.

- [ ] **Step 6: Commit**

```bash
git add src/services/labios-dispatcher.cpp src/services/labios-worker.cpp \
    src/services/labios-manager.cpp src/services/CMakeLists.txt
git commit -m "feat: stub service executables (dispatcher, worker, manager)"
```

---

### Task 6: Smoke Test

**Files:**
- Create: `tests/integration/smoke_test.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Create tests/integration/smoke_test.cpp**

```cpp
#include <catch2/catch_test_macros.hpp>
#include <labios/transport/nats.h>
#include <labios/transport/redis.h>

#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>

static std::string env_or(const char* name, const char* fallback) {
    const char* val = std::getenv(name);
    return (val != nullptr && val[0] != '\0') ? val : fallback;
}

static std::string nats_url() {
    return env_or("LABIOS_NATS_URL", "nats://localhost:4222");
}

static std::string redis_host() {
    return env_or("LABIOS_REDIS_HOST", "localhost");
}

static int redis_port() {
    const char* val = std::getenv("LABIOS_REDIS_PORT");
    return (val != nullptr && val[0] != '\0') ? std::stoi(val) : 6379;
}

TEST_CASE("All services report ready", "[smoke]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());

    auto check = [&](const std::string& key) {
        auto val = redis.get(key);
        INFO("Checking readiness key: " << key);
        REQUIRE(val.has_value());
        REQUIRE(val.value() == "1");
    };

    check("labios:ready:dispatcher");
    check("labios:ready:worker-1");
    check("labios:ready:worker-2");
    check("labios:ready:worker-3");
    check("labios:ready:manager");
}

TEST_CASE("NATS message flows from client to worker", "[smoke]") {
    labios::transport::NatsConnection nats(nats_url());
    labios::transport::RedisConnection redis(redis_host(), redis_port());

    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    std::string msg_id = "smoke_" + std::to_string(now);

    // Publish to worker-1's subject
    nats.publish("labios.worker.1", msg_id);
    nats.drain();

    // Poll Redis for the confirmation key the worker writes
    std::string key = "labios:confirmation:" + msg_id;
    std::optional<std::string> result;
    for (int i = 0; i < 20; ++i) {
        result = redis.get(key);
        if (result.has_value()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    REQUIRE(result.has_value());
    REQUIRE(result.value() == "received_by_worker_1");
}
```

- [ ] **Step 2: Add smoke test to tests/CMakeLists.txt**

Append to `tests/CMakeLists.txt`:

```cmake
add_executable(labios-smoke-test integration/smoke_test.cpp)
target_link_libraries(labios-smoke-test PRIVATE labios Catch2::Catch2WithMain)
catch_discover_tests(labios-smoke-test)
```

- [ ] **Step 3: Verify it compiles**

Run: `cmake --build build/dev -j$(nproc)`
Expected: `labios-smoke-test` binary appears in `build/dev/tests/`. Config unit tests still pass. Smoke test is not run yet (needs live services).

- [ ] **Step 4: Commit**

```bash
git add tests/integration/smoke_test.cpp tests/CMakeLists.txt
git commit -m "feat: smoke test for NATS roundtrip and service readiness"
```

---

### Task 7: Dockerfile

**Files:**
- Create: `Dockerfile`

- [ ] **Step 1: Create Dockerfile**

```dockerfile
# =============================================================================
# Stage 1: Builder
# =============================================================================
FROM debian:bookworm-slim AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    git \
    ca-certificates \
    pkg-config \
    && rm -rf /var/lib/apt/lists/*

COPY . /src
WORKDIR /src

RUN cmake --preset release \
    && cmake --build build/release -j"$(nproc)"

# =============================================================================
# Stage 2: Dispatcher
# =============================================================================
FROM debian:bookworm-slim AS dispatcher

COPY --from=builder /src/build/release/src/services/labios-dispatcher /usr/local/bin/
COPY conf/ /etc/labios/

ENV LABIOS_CONFIG_PATH=/etc/labios/labios.toml

ENTRYPOINT ["labios-dispatcher"]

# =============================================================================
# Stage 3: Worker
# =============================================================================
FROM debian:bookworm-slim AS worker

COPY --from=builder /src/build/release/src/services/labios-worker /usr/local/bin/
COPY conf/ /etc/labios/

ENV LABIOS_CONFIG_PATH=/etc/labios/labios.toml

ENTRYPOINT ["labios-worker"]

# =============================================================================
# Stage 4: Manager
# =============================================================================
FROM debian:bookworm-slim AS manager

COPY --from=builder /src/build/release/src/services/labios-manager /usr/local/bin/
COPY conf/ /etc/labios/

ENV LABIOS_CONFIG_PATH=/etc/labios/labios.toml

ENTRYPOINT ["labios-manager"]

# =============================================================================
# Stage 5: Test runner
# =============================================================================
FROM debian:bookworm-slim AS test

COPY --from=builder /src/build/release/tests/labios-smoke-test /usr/local/bin/

ENV LABIOS_NATS_URL=nats://nats:4222
ENV LABIOS_REDIS_HOST=redis

ENTRYPOINT ["labios-smoke-test"]
```

- [ ] **Step 2: Create .dockerignore**

Update `.gitignore` or create `.dockerignore` to exclude build artifacts from the Docker context:

```
build/
.git/
.worktrees/
.planning/
.claude/
docs/
```

- [ ] **Step 3: Verify Docker build succeeds**

Run: `docker build --target builder -t labios-builder .`
Expected: Full build completes inside Docker. All binaries produced.

- [ ] **Step 4: Commit**

```bash
git add Dockerfile .dockerignore
git commit -m "build: multi-stage Dockerfile for all services and test runner"
```

---

### Task 8: Docker Compose

**Files:**
- Create: `docker-compose.yml`

- [ ] **Step 1: Create docker-compose.yml**

```yaml
services:
  nats:
    image: nats:2.10-alpine
    command: ["--jetstream", "--store_dir=/data", "--http_port=8222"]
    ports:
      - "4222:4222"
      - "8222:8222"
    healthcheck:
      test: ["CMD", "sh", "-c", "wget -qO- http://localhost:8222/healthz > /dev/null 2>&1"]
      interval: 2s
      timeout: 2s
      retries: 5
      start_period: 2s

  redis:
    image: redis:7-alpine
    ports:
      - "6379:6379"
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
      start_period: 3s

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
      start_period: 3s

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
      start_period: 3s

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
      start_period: 3s

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
      start_period: 3s

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

- [ ] **Step 2: Run the full system**

Run: `docker compose up -d && docker compose logs -f` (Ctrl+C after seeing readiness lines)
Expected output includes:
```
dispatcher  | [*] dispatcher ready
worker-1    | [*] worker-1 ready (speed=5, capacity=10GB)
worker-2    | [*] worker-2 ready (speed=3, capacity=50GB)
worker-3    | [*] worker-3 ready (speed=1, capacity=200GB)
manager     | [*] manager ready
```

- [ ] **Step 3: Run the smoke test**

Run: `docker compose run --rm test`
Expected: Both Catch2 tests pass (service readiness + NATS roundtrip).

- [ ] **Step 4: Teardown**

Run: `docker compose down -v`

- [ ] **Step 5: Commit**

```bash
git add docker-compose.yml
git commit -m "feat: Docker Compose with NATS, Redis, 3 workers, and smoke test"
```

---

### Task 9: GitHub Actions CI

**Files:**
- Create: `.github/workflows/ci.yml`

- [ ] **Step 1: Create .github/workflows/ci.yml**

```yaml
name: CI

on:
  push:
    branches: [master]
  pull_request:
    branches: [master]

jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4

      - name: Install build dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y --no-install-recommends build-essential cmake git

      - name: Configure
        run: cmake --preset ci

      - name: Build
        run: cmake --build build/ci -j"$(nproc)"

      - name: Run unit tests
        run: ctest --test-dir build/ci --output-on-failure -L unit

  smoke:
    runs-on: ubuntu-latest
    needs: build
    steps:
      - uses: actions/checkout@v4

      - name: Build Docker images
        run: docker compose build

      - name: Start system
        run: docker compose up -d

      - name: Wait for services
        run: |
          docker compose wait test || true
          docker compose logs test

      - name: Run smoke test
        run: docker compose run --rm test

      - name: Teardown
        if: always()
        run: docker compose down -v
```

- [ ] **Step 2: Verify CI YAML is valid**

Run: `python3 -c "import yaml; yaml.safe_load(open('.github/workflows/ci.yml'))"`
Expected: No errors. (Install PyYAML if needed: `pip install pyyaml`)

- [ ] **Step 3: Commit**

```bash
git add .github/workflows/ci.yml
git commit -m "ci: native build + Docker Compose smoke test"
```

---

### Task 10: Final Verification

- [ ] **Step 1: Clean build from scratch**

Run: `rm -rf build/dev && cmake --preset dev && cmake --build build/dev -j$(nproc)`
Expected: Full build with zero errors, zero warnings (-Werror is on).

- [ ] **Step 2: Run unit tests**

Run: `ctest --test-dir build/dev --output-on-failure`
Expected: All config tests pass. Smoke tests are skipped (no live services).

- [ ] **Step 3: Full Docker compose integration**

Run:
```bash
docker compose build
docker compose up -d
docker compose run --rm test
docker compose down -v
```
Expected: All services start, both smoke tests pass, clean teardown.

- [ ] **Step 4: Verify file inventory matches spec**

Run: `find CMakeLists.txt CMakePresets.json cmake/ conf/ include/ src/ tests/ Dockerfile docker-compose.yml .dockerignore .github/ -type f | sort`
Expected output:
```
.dockerignore
.github/workflows/ci.yml
CMakeLists.txt
CMakePresets.json
Dockerfile
cmake/LabiosDependencies.cmake
conf/labios.toml
docker-compose.yml
include/labios/config.h
include/labios/transport/nats.h
include/labios/transport/redis.h
src/labios/CMakeLists.txt
src/labios/config.cpp
src/labios/transport/nats.cpp
src/labios/transport/redis.cpp
src/services/CMakeLists.txt
src/services/labios-dispatcher.cpp
src/services/labios-manager.cpp
src/services/labios-worker.cpp
tests/CMakeLists.txt
tests/integration/smoke_test.cpp
tests/unit/config_test.cpp
```

22 files. Every one compiles and serves a purpose.

- [ ] **Step 5: Final commit if any fixups were needed**

```bash
git add -A && git status
# Only commit if there are changes
git diff --cached --quiet || git commit -m "fix: M0 final verification fixups"
```
