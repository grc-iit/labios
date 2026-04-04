# M1a: Label Data Path Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Labels flow from client through dispatcher to workers, data stages through Redis warehouse, workers execute READ/WRITE against local storage, completion notifications return via NATS request-reply, and the catalog tracks every label's lifecycle.

**Architecture:** Client creates labels via the 2.0 agent API, serializes with FlatBuffers, stages data in Redis warehouse, publishes via NATS request-reply. Dispatcher assigns labels to workers via a RoundRobin solver behind a concept interface. Workers execute WRITE (Redis to local file) and READ (local file to Redis), then publish completion to the client's reply inbox. CatalogManager records every state transition.

**Tech Stack:** C++20, FlatBuffers, cnats (NATS request-reply), hiredis (Redis HASH + binary ops), Catch2

**Spec:** `docs/superpowers/specs/2026-04-04-m1a-label-data-path-design.md`

---

## File Map

| File | Responsibility |
|---|---|
| `schemas/label.fbs` | FlatBuffers schema for Label, Completion, Pointer union |
| `include/labios/label.h` | C++ label types, serialization, ID generation |
| `src/labios/label.cpp` | Label implementation |
| `include/labios/warehouse.h` | Redis data staging interface |
| `src/labios/warehouse.cpp` | Warehouse implementation |
| `include/labios/catalog.h` | Label lifecycle tracking interface |
| `src/labios/catalog.cpp` | CatalogManager implementation |
| `include/labios/solver/solver.h` | Solver concept + WorkerInfo + AssignmentMap |
| `include/labios/solver/round_robin.h` | RoundRobinSolver class |
| `src/labios/solver/round_robin.cpp` | RoundRobin implementation |
| `include/labios/client.h` | Client class (2.0 agent API) |
| `src/labios/client.cpp` | Client implementation |
| `src/services/labios-demo.cpp` | 100MB write/read throughput demo |
| `tests/unit/label_test.cpp` | FlatBuffers roundtrip tests |
| `tests/unit/solver_test.cpp` | RoundRobin logic tests |
| `tests/integration/data_path_test.cpp` | End-to-end write/read tests |

**Modified:** `cmake/LabiosDependencies.cmake`, `src/labios/CMakeLists.txt`, `src/services/CMakeLists.txt`, `tests/CMakeLists.txt`, `include/labios/transport/redis.h`, `src/labios/transport/redis.cpp`, `include/labios/transport/nats.h`, `src/labios/transport/nats.cpp`, `src/services/labios-dispatcher.cpp`, `src/services/labios-worker.cpp`, `Dockerfile`

---

### Task 1: FlatBuffers Build Integration

**Files:**
- Create: `schemas/label.fbs`
- Modify: `cmake/LabiosDependencies.cmake`
- Modify: `src/labios/CMakeLists.txt`

- [ ] **Step 1: Add FlatBuffers to cmake/LabiosDependencies.cmake**

Add the FlatBuffers FetchContent declaration before the existing cnats declaration, and `FetchContent_MakeAvailable` it alongside the other non-test deps:

```cmake
FetchContent_Declare(
    flatbuffers
    URL https://github.com/google/flatbuffers/archive/refs/tags/v24.3.25.tar.gz
)
set(FLATBUFFERS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FLATBUFFERS_BUILD_FLATHASH OFF CACHE BOOL "" FORCE)
```

Add `flatbuffers` to the `FetchContent_MakeAvailable(cnats hiredis tomlplusplus)` call so it becomes `FetchContent_MakeAvailable(cnats hiredis tomlplusplus flatbuffers)`.

- [ ] **Step 2: Create schemas/label.fbs**

```fbs
namespace labios.schema;

enum LabelType : byte { Read, Write, Delete, Flush, Composite }
enum CompletionStatus : byte { Complete, Error }
enum Intent : byte { None, Checkpoint, Cache, ToolOutput, FinalResult, Intermediate, SharedState }
enum Isolation : byte { None, Application, Agent }

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
  id:           uint64;
  type:         LabelType;
  source:       Pointer;
  destination:  Pointer;
  operation:    string;
  flags:        uint32;
  priority:     uint8;
  app_id:       uint32;
  dependencies: [uint64];
  data_size:    uint64;
  intent:       Intent;
  ttl_seconds:  uint32;
  isolation:    Isolation;
  reply_to:     string;
}

table Completion {
  label_id: uint64;
  status:   CompletionStatus;
  error:    string;
  data_key: string;
}

root_type Label;
```

- [ ] **Step 3: Add flatc code generation to src/labios/CMakeLists.txt**

Add a custom command that runs `flatc --cpp` on the schema, and add the generated header's directory to the include path. Insert this before the `add_library(labios STATIC ...)`:

```cmake
# FlatBuffers code generation
set(LABIOS_FBS_SCHEMA ${PROJECT_SOURCE_DIR}/schemas/label.fbs)
set(LABIOS_FBS_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${LABIOS_FBS_GENERATED_DIR})

add_custom_command(
    OUTPUT ${LABIOS_FBS_GENERATED_DIR}/label_generated.h
    COMMAND flatc --cpp -o ${LABIOS_FBS_GENERATED_DIR} ${LABIOS_FBS_SCHEMA}
    DEPENDS ${LABIOS_FBS_SCHEMA} flatc
    COMMENT "Generating FlatBuffers C++ header from label.fbs"
)

add_custom_target(labios_fbs_generated DEPENDS ${LABIOS_FBS_GENERATED_DIR}/label_generated.h)
```

Then make the `labios` library depend on the generated target and add the generated dir to includes:

```cmake
add_dependencies(labios labios_fbs_generated)

target_include_directories(labios
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
        ${LABIOS_FBS_GENERATED_DIR}
)
```

Also add `flatbuffers` to `target_link_libraries` for the labios target.

- [ ] **Step 4: Verify the build generates the header**

Run: `rm -rf build/dev && cmake --preset dev && cmake --build build/dev -j$(nproc)`
Expected: `build/dev/src/labios/generated/label_generated.h` exists. Build succeeds.

- [ ] **Step 5: Commit**

```bash
git add schemas/label.fbs cmake/LabiosDependencies.cmake src/labios/CMakeLists.txt
git commit -m "build: FlatBuffers schema and code generation for Label"
```

---

### Task 2: Redis Transport Extensions

**Files:**
- Modify: `include/labios/transport/redis.h`
- Modify: `src/labios/transport/redis.cpp`

- [ ] **Step 1: Add new method declarations to redis.h**

Add these methods to the `RedisConnection` class after the existing `get()`:

```cpp
    void set_binary(std::string_view key, std::span<const std::byte> data);
    [[nodiscard]] std::vector<std::byte> get_binary(std::string_view key);
    void del(std::string_view key);
    void hset(std::string_view key, std::string_view field, std::string_view value);
    [[nodiscard]] std::optional<std::string> hget(std::string_view key,
                                                   std::string_view field) const;
```

Add these includes to the header: `<span>`, `<vector>`, `<cstddef>`.

- [ ] **Step 2: Implement set_binary and get_binary in redis.cpp**

```cpp
void RedisConnection::set_binary(std::string_view key,
                                  std::span<const std::byte> data) {
    auto* reply = static_cast<redisReply*>(
        redisCommand(impl_->ctx, "SET %b %b",
                     key.data(), key.size(),
                     data.data(), data.size()));
    if (reply == nullptr) {
        throw std::runtime_error("redis SET binary failed: " +
                                 std::string(impl_->ctx->errstr));
    }
    freeReplyObject(reply);
}

std::vector<std::byte> RedisConnection::get_binary(std::string_view key) {
    auto* reply = static_cast<redisReply*>(
        redisCommand(impl_->ctx, "GET %b", key.data(), key.size()));
    if (reply == nullptr) {
        throw std::runtime_error("redis GET binary failed: " +
                                 std::string(impl_->ctx->errstr));
    }
    std::vector<std::byte> result;
    if (reply->type == REDIS_REPLY_STRING) {
        auto* begin = reinterpret_cast<const std::byte*>(reply->str);
        result.assign(begin, begin + reply->len);
    }
    freeReplyObject(reply);
    return result;
}
```

- [ ] **Step 3: Implement del, hset, hget in redis.cpp**

```cpp
void RedisConnection::del(std::string_view key) {
    auto* reply = static_cast<redisReply*>(
        redisCommand(impl_->ctx, "DEL %b", key.data(), key.size()));
    if (reply == nullptr) {
        throw std::runtime_error("redis DEL failed: " +
                                 std::string(impl_->ctx->errstr));
    }
    freeReplyObject(reply);
}

void RedisConnection::hset(std::string_view key, std::string_view field,
                            std::string_view value) {
    auto* reply = static_cast<redisReply*>(
        redisCommand(impl_->ctx, "HSET %b %b %b",
                     key.data(), key.size(),
                     field.data(), field.size(),
                     value.data(), value.size()));
    if (reply == nullptr) {
        throw std::runtime_error("redis HSET failed: " +
                                 std::string(impl_->ctx->errstr));
    }
    freeReplyObject(reply);
}

std::optional<std::string> RedisConnection::hget(std::string_view key,
                                                  std::string_view field) const {
    auto* reply = static_cast<redisReply*>(
        redisCommand(impl_->ctx, "HGET %b %b",
                     key.data(), key.size(),
                     field.data(), field.size()));
    if (reply == nullptr) {
        throw std::runtime_error("redis HGET failed: " +
                                 std::string(impl_->ctx->errstr));
    }
    std::optional<std::string> result;
    if (reply->type == REDIS_REPLY_STRING) {
        result.emplace(reply->str, reply->len);
    }
    freeReplyObject(reply);
    return result;
}
```

- [ ] **Step 4: Verify build succeeds**

Run: `cmake --build build/dev -j$(nproc)`
Expected: Compiles, unit tests still pass.

- [ ] **Step 5: Commit**

```bash
git add include/labios/transport/redis.h src/labios/transport/redis.cpp
git commit -m "feat: Redis binary ops, HASH ops, and DEL for warehouse and catalog"
```

---

### Task 3: NATS Transport Extensions

**Files:**
- Modify: `include/labios/transport/nats.h`
- Modify: `src/labios/transport/nats.cpp`
- Modify: `src/services/labios-dispatcher.cpp` (update callback signature)
- Modify: `src/services/labios-worker.cpp` (update callback signature)

- [ ] **Step 1: Update the callback signature and add request/publish_to in nats.h**

Replace the existing `MessageCallback` typedef and add new methods:

```cpp
    using MessageCallback = std::function<void(std::string_view subject,
                                               std::span<const std::byte> data,
                                               std::string_view reply_to)>;

    struct Reply {
        std::vector<std::byte> data;
    };

    /// Send a request and wait for a reply (NATS request-reply pattern).
    Reply request(std::string_view subject, std::span<const std::byte> data,
                  std::chrono::milliseconds timeout);
```

Add `#include <chrono>` to the includes.

- [ ] **Step 2: Update on_message callback in nats.cpp to pass reply_to**

In the `Impl::on_message` static method, extract the reply subject from the message:

```cpp
    static void on_message(natsConnection* /*nc*/, natsSubscription* /*sub*/,
                           natsMsg* msg, void* closure) {
        auto* self = static_cast<Impl*>(closure);
        if (self->cb) {
            const char* subj = natsMsg_GetSubject(msg);
            const char* raw = natsMsg_GetData(msg);
            int len = natsMsg_GetDataLength(msg);
            const char* reply = natsMsg_GetReply(msg);
            auto span = std::span<const std::byte>(
                reinterpret_cast<const std::byte*>(raw),
                static_cast<size_t>(len));
            self->cb(subj != nullptr ? subj : "",
                     span,
                     reply != nullptr ? reply : "");
        }
        natsMsg_Destroy(msg);
    }
```

- [ ] **Step 3: Implement request() in nats.cpp**

```cpp
NatsConnection::Reply NatsConnection::request(
    std::string_view subject,
    std::span<const std::byte> data,
    std::chrono::milliseconds timeout) {
    natsMsg* reply_msg = nullptr;
    natsStatus s = natsConnection_Request(
        &reply_msg, impl_->conn,
        std::string(subject).c_str(),
        reinterpret_cast<const void*>(data.data()),
        static_cast<int>(data.size()),
        static_cast<int64_t>(timeout.count()));
    if (s != NATS_OK) {
        throw std::runtime_error("nats: request failed on " + std::string(subject));
    }
    const char* rdata = natsMsg_GetData(reply_msg);
    int rlen = natsMsg_GetDataLength(reply_msg);
    Reply result;
    if (rdata != nullptr && rlen > 0) {
        auto* begin = reinterpret_cast<const std::byte*>(rdata);
        result.data.assign(begin, begin + rlen);
    }
    natsMsg_Destroy(reply_msg);
    return result;
}
```

- [ ] **Step 4: Update existing service callbacks for new signature**

In `labios-dispatcher.cpp`, update the subscribe lambda to accept the third `reply_to` parameter:

```cpp
    nats.subscribe("labios.labels",
        [](std::string_view /*subject*/, std::span<const std::byte> data,
           std::string_view /*reply_to*/) {
            std::cout << "[" << timestamp() << "] dispatcher: received label ("
                      << data.size() << " bytes)\n";
        });
```

In `labios-worker.cpp`, update the subscribe lambda similarly (add `std::string_view /*reply_to*/` as third param).

In `tests/integration/smoke_test.cpp`, the test does NOT use subscribe, only publish and Redis checks, so no change needed there.

- [ ] **Step 5: Verify build and tests pass**

Run: `cmake --build build/dev -j$(nproc) && ctest --test-dir build/dev -L unit --output-on-failure`
Expected: Compiles, all unit tests pass.

- [ ] **Step 6: Commit**

```bash
git add include/labios/transport/nats.h src/labios/transport/nats.cpp \
    src/services/labios-dispatcher.cpp src/services/labios-worker.cpp
git commit -m "feat: NATS request-reply and reply_to in subscribe callback"
```

---

### Task 4: Label Serialization and ID Generation

**Files:**
- Create: `include/labios/label.h`
- Create: `src/labios/label.cpp`
- Create: `tests/unit/label_test.cpp`
- Modify: `src/labios/CMakeLists.txt` (add label.cpp)
- Modify: `tests/CMakeLists.txt` (add label_test)

- [ ] **Step 1: Create include/labios/label.h**

```cpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace labios {

enum class LabelType : uint8_t { Read, Write, Delete, Flush, Composite };
enum class Intent : uint8_t { None, Checkpoint, Cache, ToolOutput, FinalResult, Intermediate, SharedState };
enum class Isolation : uint8_t { None, Application, Agent };
enum class CompletionStatus : uint8_t { Complete, Error };

namespace LabelFlags {
    constexpr uint32_t Queued      = 1 << 0;
    constexpr uint32_t Scheduled   = 1 << 1;
    constexpr uint32_t Pending     = 1 << 2;
    constexpr uint32_t Cached      = 1 << 3;
    constexpr uint32_t Invalidated = 1 << 4;
    constexpr uint32_t Async       = 1 << 5;
}

struct MemoryPtr {
    uint64_t address;
    uint64_t size;
};

struct FilePath {
    std::string path;
    uint64_t offset = 0;
    uint64_t length = 0;
};

struct NetworkEndpoint {
    std::string host;
    uint16_t port;
};

using Pointer = std::variant<std::monostate, MemoryPtr, FilePath, NetworkEndpoint>;

Pointer memory_ptr(const void* addr, uint64_t size);
Pointer file_path(std::string_view path);
Pointer file_path(std::string_view path, uint64_t offset, uint64_t length);
Pointer network_endpoint(std::string_view host, uint16_t port);

struct LabelData {
    uint64_t id = 0;
    LabelType type = LabelType::Write;
    Pointer source;
    Pointer destination;
    std::string operation;
    uint32_t flags = 0;
    uint8_t priority = 0;
    uint32_t app_id = 0;
    std::vector<uint64_t> dependencies;
    uint64_t data_size = 0;
    Intent intent = Intent::None;
    uint32_t ttl_seconds = 0;
    Isolation isolation = Isolation::None;
    std::string reply_to;
};

struct CompletionData {
    uint64_t label_id = 0;
    CompletionStatus status = CompletionStatus::Complete;
    std::string error;
    std::string data_key;
};

struct LabelParams {
    LabelType type = LabelType::Write;
    Pointer source;
    Pointer destination;
    std::string operation;
    uint32_t flags = 0;
    uint8_t priority = 0;
    Intent intent = Intent::None;
    uint32_t ttl_seconds = 0;
    Isolation isolation = Isolation::None;
};

uint64_t generate_label_id(uint32_t app_id);

std::vector<std::byte> serialize_label(const LabelData& label);
LabelData deserialize_label(std::span<const std::byte> buf);

std::vector<std::byte> serialize_completion(const CompletionData& completion);
CompletionData deserialize_completion(std::span<const std::byte> buf);

} // namespace labios
```

- [ ] **Step 2: Write the failing tests**

Create `tests/unit/label_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <labios/label.h>

TEST_CASE("Label serialization roundtrip with FilePath", "[label]") {
    labios::LabelData original;
    original.id = 12345;
    original.type = labios::LabelType::Write;
    original.source = labios::file_path("/src/data.bin", 0, 1024);
    original.destination = labios::file_path("/dst/data.bin");
    original.flags = labios::LabelFlags::Queued | labios::LabelFlags::Async;
    original.priority = 128;
    original.app_id = 42;
    original.data_size = 1024;
    original.intent = labios::Intent::Checkpoint;
    original.reply_to = "_INBOX.abc123";

    auto bytes = labios::serialize_label(original);
    REQUIRE(!bytes.empty());

    auto decoded = labios::deserialize_label(bytes);
    REQUIRE(decoded.id == 12345);
    REQUIRE(decoded.type == labios::LabelType::Write);
    REQUIRE(decoded.flags == (labios::LabelFlags::Queued | labios::LabelFlags::Async));
    REQUIRE(decoded.priority == 128);
    REQUIRE(decoded.app_id == 42);
    REQUIRE(decoded.data_size == 1024);
    REQUIRE(decoded.intent == labios::Intent::Checkpoint);
    REQUIRE(decoded.reply_to == "_INBOX.abc123");

    auto* dst = std::get_if<labios::FilePath>(&decoded.destination);
    REQUIRE(dst != nullptr);
    REQUIRE(dst->path == "/dst/data.bin");
}

TEST_CASE("Label serialization roundtrip with MemoryPtr", "[label]") {
    labios::LabelData original;
    original.id = 99;
    original.type = labios::LabelType::Read;
    original.source = labios::memory_ptr(reinterpret_cast<void*>(0xDEADBEEF), 4096);

    auto bytes = labios::serialize_label(original);
    auto decoded = labios::deserialize_label(bytes);

    auto* src = std::get_if<labios::MemoryPtr>(&decoded.source);
    REQUIRE(src != nullptr);
    REQUIRE(src->address == 0xDEADBEEF);
    REQUIRE(src->size == 4096);
}

TEST_CASE("Label ID generation produces unique IDs", "[label]") {
    auto id1 = labios::generate_label_id(1);
    auto id2 = labios::generate_label_id(1);
    auto id3 = labios::generate_label_id(2);
    REQUIRE(id1 != id2);
    REQUIRE(id2 != id3);
}

TEST_CASE("Completion serialization roundtrip", "[label]") {
    labios::CompletionData original;
    original.label_id = 555;
    original.status = labios::CompletionStatus::Error;
    original.error = "file not found";
    original.data_key = "labios:data:555";

    auto bytes = labios::serialize_completion(original);
    auto decoded = labios::deserialize_completion(bytes);

    REQUIRE(decoded.label_id == 555);
    REQUIRE(decoded.status == labios::CompletionStatus::Error);
    REQUIRE(decoded.error == "file not found");
    REQUIRE(decoded.data_key == "labios:data:555");
}
```

- [ ] **Step 3: Add label_test target to tests/CMakeLists.txt**

```cmake
add_executable(labios-label-test unit/label_test.cpp)
target_link_libraries(labios-label-test PRIVATE labios Catch2::Catch2WithMain)
catch_discover_tests(labios-label-test TEST_PREFIX "unit/" PROPERTIES LABELS "unit")
```

- [ ] **Step 4: Run tests to verify they fail (link error)**

Run: `cmake --build build/dev -j$(nproc) 2>&1 | tail -5`
Expected: Link error for `serialize_label`, `deserialize_label`, etc.

- [ ] **Step 5: Implement src/labios/label.cpp**

```cpp
#include <labios/label.h>
#include <label_generated.h>
#include <flatbuffers/flatbuffers.h>

#include <atomic>
#include <chrono>

namespace labios {

Pointer memory_ptr(const void* addr, uint64_t size) {
    return MemoryPtr{reinterpret_cast<uint64_t>(addr), size};
}

Pointer file_path(std::string_view path) {
    return FilePath{std::string(path), 0, 0};
}

Pointer file_path(std::string_view path, uint64_t offset, uint64_t length) {
    return FilePath{std::string(path), offset, length};
}

Pointer network_endpoint(std::string_view host, uint16_t port) {
    return NetworkEndpoint{std::string(host), port};
}

static std::atomic<uint64_t> g_id_counter{0};

uint64_t generate_label_id(uint32_t app_id) {
    auto now = std::chrono::high_resolution_clock::now();
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        now.time_since_epoch()).count();
    uint64_t seq = g_id_counter.fetch_add(1, std::memory_order_relaxed);
    return (static_cast<uint64_t>(ns) & 0xFFFFFFFFFFFF0000ULL)
         | ((static_cast<uint64_t>(app_id) & 0xFF) << 8)
         | (seq & 0xFF);
}

namespace {

using namespace labios::schema;

flatbuffers::Offset<schema::Pointer> build_pointer(
    flatbuffers::FlatBufferBuilder& fbb,
    const labios::Pointer& ptr) {
    if (auto* mp = std::get_if<labios::MemoryPtr>(&ptr)) {
        auto mem = CreateMemoryPtr(fbb, mp->address, mp->size);
        return CreatePointer(fbb, PointerVariant_MemoryPtr, mem.Union());
    }
    if (auto* fp = std::get_if<labios::FilePath>(&ptr)) {
        auto p = fbb.CreateString(fp->path);
        auto file = CreateFilePath(fbb, p, fp->offset, fp->length);
        return CreatePointer(fbb, PointerVariant_FilePath, file.Union());
    }
    if (auto* ne = std::get_if<labios::NetworkEndpoint>(&ptr)) {
        auto h = fbb.CreateString(ne->host);
        auto net = CreateNetworkEndpoint(fbb, h, ne->port);
        return CreatePointer(fbb, PointerVariant_NetworkEndpoint, net.Union());
    }
    return CreatePointer(fbb);
}

labios::Pointer extract_pointer(const schema::Pointer* ptr) {
    if (ptr == nullptr) return std::monostate{};
    switch (ptr->ptr_type()) {
    case PointerVariant_MemoryPtr: {
        auto* mp = ptr->ptr_as_MemoryPtr();
        return labios::MemoryPtr{mp->address(), mp->size()};
    }
    case PointerVariant_FilePath: {
        auto* fp = ptr->ptr_as_FilePath();
        return labios::FilePath{
            fp->path() ? fp->path()->str() : "",
            fp->offset(), fp->length()};
    }
    case PointerVariant_NetworkEndpoint: {
        auto* ne = ptr->ptr_as_NetworkEndpoint();
        return labios::NetworkEndpoint{
            ne->host() ? ne->host()->str() : "",
            ne->port()};
    }
    default:
        return std::monostate{};
    }
}

} // namespace

std::vector<std::byte> serialize_label(const LabelData& label) {
    flatbuffers::FlatBufferBuilder fbb(1024);
    auto src = build_pointer(fbb, label.source);
    auto dst = build_pointer(fbb, label.destination);
    auto op = label.operation.empty()
        ? flatbuffers::Offset<flatbuffers::String>()
        : fbb.CreateString(label.operation);
    auto deps = label.dependencies.empty()
        ? flatbuffers::Offset<flatbuffers::Vector<uint64_t>>()
        : fbb.CreateVector(label.dependencies);
    auto reply = label.reply_to.empty()
        ? flatbuffers::Offset<flatbuffers::String>()
        : fbb.CreateString(label.reply_to);

    schema::LabelBuilder lb(fbb);
    lb.add_id(label.id);
    lb.add_type(static_cast<schema::LabelType>(label.type));
    lb.add_source(src);
    lb.add_destination(dst);
    if (!label.operation.empty()) lb.add_operation(op);
    lb.add_flags(label.flags);
    lb.add_priority(label.priority);
    lb.add_app_id(label.app_id);
    if (!label.dependencies.empty()) lb.add_dependencies(deps);
    lb.add_data_size(label.data_size);
    lb.add_intent(static_cast<schema::Intent>(label.intent));
    lb.add_ttl_seconds(label.ttl_seconds);
    lb.add_isolation(static_cast<schema::Isolation>(label.isolation));
    if (!label.reply_to.empty()) lb.add_reply_to(reply);
    fbb.Finish(lb.Finish());

    auto* buf = fbb.GetBufferPointer();
    auto size = fbb.GetSize();
    auto* begin = reinterpret_cast<const std::byte*>(buf);
    return {begin, begin + size};
}

LabelData deserialize_label(std::span<const std::byte> buf) {
    auto* fb = schema::GetLabel(buf.data());
    LabelData label;
    label.id = fb->id();
    label.type = static_cast<LabelType>(fb->type());
    label.source = extract_pointer(fb->source());
    label.destination = extract_pointer(fb->destination());
    if (fb->operation()) label.operation = fb->operation()->str();
    label.flags = fb->flags();
    label.priority = fb->priority();
    label.app_id = fb->app_id();
    if (fb->dependencies()) {
        label.dependencies.assign(fb->dependencies()->begin(),
                                   fb->dependencies()->end());
    }
    label.data_size = fb->data_size();
    label.intent = static_cast<Intent>(fb->intent());
    label.ttl_seconds = fb->ttl_seconds();
    label.isolation = static_cast<Isolation>(fb->isolation());
    if (fb->reply_to()) label.reply_to = fb->reply_to()->str();
    return label;
}

std::vector<std::byte> serialize_completion(const CompletionData& comp) {
    flatbuffers::FlatBufferBuilder fbb(256);
    auto err = comp.error.empty()
        ? flatbuffers::Offset<flatbuffers::String>()
        : fbb.CreateString(comp.error);
    auto dk = comp.data_key.empty()
        ? flatbuffers::Offset<flatbuffers::String>()
        : fbb.CreateString(comp.data_key);
    schema::CompletionBuilder cb(fbb);
    cb.add_label_id(comp.label_id);
    cb.add_status(static_cast<schema::CompletionStatus>(comp.status));
    if (!comp.error.empty()) cb.add_error(err);
    if (!comp.data_key.empty()) cb.add_data_key(dk);
    fbb.Finish(cb.Finish());
    auto* buf = fbb.GetBufferPointer();
    auto size = fbb.GetSize();
    auto* begin = reinterpret_cast<const std::byte*>(buf);
    return {begin, begin + size};
}

CompletionData deserialize_completion(std::span<const std::byte> buf) {
    auto* fb = schema::GetCompletion(buf.data());
    CompletionData comp;
    comp.label_id = fb->label_id();
    comp.status = static_cast<CompletionStatus>(fb->status());
    if (fb->error()) comp.error = fb->error()->str();
    if (fb->data_key()) comp.data_key = fb->data_key()->str();
    return comp;
}

} // namespace labios
```

- [ ] **Step 6: Add label.cpp to src/labios/CMakeLists.txt**

Add `label.cpp` to the `add_library(labios STATIC ...)` sources list.

- [ ] **Step 7: Run tests to verify they pass**

Run: `cmake --build build/dev -j$(nproc) && ctest --test-dir build/dev -L unit --output-on-failure`
Expected: All label tests pass (4 new + 3 existing config tests).

- [ ] **Step 8: Commit**

```bash
git add include/labios/label.h src/labios/label.cpp src/labios/CMakeLists.txt \
    tests/unit/label_test.cpp tests/CMakeLists.txt
git commit -m "feat: label serialization with FlatBuffers and ID generation"
```

---

### Task 5: Warehouse (Redis Data Staging)

**Files:**
- Create: `include/labios/warehouse.h`
- Create: `src/labios/warehouse.cpp`
- Modify: `src/labios/CMakeLists.txt`

- [ ] **Step 1: Create include/labios/warehouse.h**

```cpp
#pragma once

#include <labios/transport/redis.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace labios {

class Warehouse {
public:
    explicit Warehouse(transport::RedisConnection& redis);

    void stage(uint64_t label_id, std::span<const std::byte> data);
    std::vector<std::byte> retrieve(uint64_t label_id);
    void remove(uint64_t label_id);
    bool exists(uint64_t label_id) const;

    static std::string data_key(uint64_t label_id);

private:
    transport::RedisConnection& redis_;
};

} // namespace labios
```

- [ ] **Step 2: Create src/labios/warehouse.cpp**

```cpp
#include <labios/warehouse.h>

#include <stdexcept>

namespace labios {

Warehouse::Warehouse(transport::RedisConnection& redis) : redis_(redis) {}

std::string Warehouse::data_key(uint64_t label_id) {
    return "labios:data:" + std::to_string(label_id);
}

void Warehouse::stage(uint64_t label_id, std::span<const std::byte> data) {
    redis_.set_binary(data_key(label_id), data);
}

std::vector<std::byte> Warehouse::retrieve(uint64_t label_id) {
    return redis_.get_binary(data_key(label_id));
}

void Warehouse::remove(uint64_t label_id) {
    redis_.del(data_key(label_id));
}

bool Warehouse::exists(uint64_t label_id) const {
    auto val = redis_.get(data_key(label_id));
    return val.has_value();
}

} // namespace labios
```

- [ ] **Step 3: Add warehouse.cpp to src/labios/CMakeLists.txt**

Add `warehouse.cpp` to the `add_library(labios STATIC ...)` sources list.

- [ ] **Step 4: Verify build**

Run: `cmake --build build/dev -j$(nproc)`
Expected: Compiles, existing tests pass.

- [ ] **Step 5: Commit**

```bash
git add include/labios/warehouse.h src/labios/warehouse.cpp src/labios/CMakeLists.txt
git commit -m "feat: warehouse for Redis data staging"
```

---

### Task 6: Catalog Manager

**Files:**
- Create: `include/labios/catalog.h`
- Create: `src/labios/catalog.cpp`
- Modify: `src/labios/CMakeLists.txt`

- [ ] **Step 1: Create include/labios/catalog.h**

```cpp
#pragma once

#include <labios/label.h>
#include <labios/transport/redis.h>

#include <cstdint>
#include <optional>
#include <string>

namespace labios {

enum class LabelStatus : uint8_t {
    Queued, Scheduled, Executing, Complete, Error
};

std::string to_string(LabelStatus status);
LabelStatus label_status_from_string(std::string_view s);

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
    std::string catalog_key(uint64_t label_id) const;
};

} // namespace labios
```

- [ ] **Step 2: Create src/labios/catalog.cpp**

```cpp
#include <labios/catalog.h>

#include <chrono>
#include <stdexcept>

namespace labios {

std::string to_string(LabelStatus status) {
    switch (status) {
    case LabelStatus::Queued:    return "queued";
    case LabelStatus::Scheduled: return "scheduled";
    case LabelStatus::Executing: return "executing";
    case LabelStatus::Complete:  return "complete";
    case LabelStatus::Error:     return "error";
    }
    return "unknown";
}

LabelStatus label_status_from_string(std::string_view s) {
    if (s == "queued")    return LabelStatus::Queued;
    if (s == "scheduled") return LabelStatus::Scheduled;
    if (s == "executing") return LabelStatus::Executing;
    if (s == "complete")  return LabelStatus::Complete;
    if (s == "error")     return LabelStatus::Error;
    throw std::runtime_error("unknown label status: " + std::string(s));
}

static std::string now_ms() {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    return std::to_string(ms);
}

CatalogManager::CatalogManager(transport::RedisConnection& redis)
    : redis_(redis) {}

std::string CatalogManager::catalog_key(uint64_t label_id) const {
    return "labios:catalog:" + std::to_string(label_id);
}

void CatalogManager::create(uint64_t label_id, uint32_t app_id,
                             LabelType type) {
    auto key = catalog_key(label_id);
    auto ts = now_ms();
    redis_.hset(key, "status", to_string(LabelStatus::Queued));
    redis_.hset(key, "app_id", std::to_string(app_id));
    redis_.hset(key, "type", std::to_string(static_cast<int>(type)));
    redis_.hset(key, "created_at", ts);
    redis_.hset(key, "updated_at", ts);
}

void CatalogManager::set_status(uint64_t label_id, LabelStatus status) {
    auto key = catalog_key(label_id);
    redis_.hset(key, "status", to_string(status));
    redis_.hset(key, "updated_at", now_ms());
}

LabelStatus CatalogManager::get_status(uint64_t label_id) const {
    auto key = catalog_key(label_id);
    auto val = redis_.hget(key, "status");
    if (!val.has_value()) {
        throw std::runtime_error("catalog: label " + std::to_string(label_id)
                                 + " not found");
    }
    return label_status_from_string(val.value());
}

void CatalogManager::set_worker(uint64_t label_id, int worker_id) {
    auto key = catalog_key(label_id);
    redis_.hset(key, "worker_id", std::to_string(worker_id));
    redis_.hset(key, "updated_at", now_ms());
}

std::optional<int> CatalogManager::get_worker(uint64_t label_id) const {
    auto key = catalog_key(label_id);
    auto val = redis_.hget(key, "worker_id");
    if (!val.has_value()) return std::nullopt;
    return std::stoi(val.value());
}

} // namespace labios
```

- [ ] **Step 3: Add catalog.cpp to src/labios/CMakeLists.txt**

- [ ] **Step 4: Verify build**

Run: `cmake --build build/dev -j$(nproc)`

- [ ] **Step 5: Commit**

```bash
git add include/labios/catalog.h src/labios/catalog.cpp src/labios/CMakeLists.txt
git commit -m "feat: catalog manager for label lifecycle tracking"
```

---

### Task 7: Solver Concept + Round Robin

**Files:**
- Create: `include/labios/solver/solver.h`
- Create: `include/labios/solver/round_robin.h`
- Create: `src/labios/solver/round_robin.cpp`
- Create: `tests/unit/solver_test.cpp`
- Modify: `src/labios/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Create include/labios/solver/solver.h**

```cpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace labios {

struct WorkerInfo {
    int id;
    bool available = true;
};

using AssignmentMap = std::unordered_map<int, std::vector<std::vector<std::byte>>>;

template<typename T>
concept Solver = requires(T s,
    std::vector<std::vector<std::byte>> labels,
    std::vector<WorkerInfo> workers) {
    { s.assign(std::move(labels), std::move(workers)) } -> std::same_as<AssignmentMap>;
};

} // namespace labios
```

- [ ] **Step 2: Create include/labios/solver/round_robin.h**

```cpp
#pragma once

#include <labios/solver/solver.h>

namespace labios {

class RoundRobinSolver {
public:
    AssignmentMap assign(std::vector<std::vector<std::byte>> labels,
                         std::vector<WorkerInfo> workers);

private:
    size_t next_ = 0;
};

static_assert(Solver<RoundRobinSolver>);

} // namespace labios
```

- [ ] **Step 3: Write the failing solver tests**

Create `tests/unit/solver_test.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <labios/solver/round_robin.h>

#include <cstddef>
#include <vector>

static std::vector<std::byte> fake_label(uint8_t tag) {
    return {static_cast<std::byte>(tag)};
}

TEST_CASE("RoundRobin distributes evenly across workers", "[solver]") {
    labios::RoundRobinSolver solver;
    std::vector<labios::WorkerInfo> workers = {{1, true}, {2, true}, {3, true}};
    std::vector<std::vector<std::byte>> labels;
    for (int i = 0; i < 6; ++i) labels.push_back(fake_label(static_cast<uint8_t>(i)));

    auto result = solver.assign(std::move(labels), std::move(workers));
    REQUIRE(result.size() == 3);
    REQUIRE(result[1].size() == 2);
    REQUIRE(result[2].size() == 2);
    REQUIRE(result[3].size() == 2);
}

TEST_CASE("RoundRobin skips unavailable workers", "[solver]") {
    labios::RoundRobinSolver solver;
    std::vector<labios::WorkerInfo> workers = {{1, true}, {2, false}, {3, true}};
    std::vector<std::vector<std::byte>> labels;
    for (int i = 0; i < 4; ++i) labels.push_back(fake_label(static_cast<uint8_t>(i)));

    auto result = solver.assign(std::move(labels), std::move(workers));
    REQUIRE(result.count(2) == 0);
    REQUIRE(result[1].size() == 2);
    REQUIRE(result[3].size() == 2);
}

TEST_CASE("RoundRobin with no available workers returns empty", "[solver]") {
    labios::RoundRobinSolver solver;
    std::vector<labios::WorkerInfo> workers = {{1, false}, {2, false}};
    std::vector<std::vector<std::byte>> labels = {fake_label(0)};

    auto result = solver.assign(std::move(labels), std::move(workers));
    REQUIRE(result.empty());
}
```

- [ ] **Step 4: Add solver_test to tests/CMakeLists.txt**

```cmake
add_executable(labios-solver-test unit/solver_test.cpp)
target_link_libraries(labios-solver-test PRIVATE labios Catch2::Catch2WithMain)
catch_discover_tests(labios-solver-test TEST_PREFIX "unit/" PROPERTIES LABELS "unit")
```

- [ ] **Step 5: Implement src/labios/solver/round_robin.cpp**

```cpp
#include <labios/solver/round_robin.h>

namespace labios {

AssignmentMap RoundRobinSolver::assign(
    std::vector<std::vector<std::byte>> labels,
    std::vector<WorkerInfo> workers) {

    std::vector<WorkerInfo> available;
    for (auto& w : workers) {
        if (w.available) available.push_back(w);
    }

    AssignmentMap result;
    if (available.empty()) return result;

    for (auto& label : labels) {
        if (next_ >= available.size()) next_ = 0;
        int wid = available[next_].id;
        result[wid].push_back(std::move(label));
        ++next_;
    }
    return result;
}

} // namespace labios
```

- [ ] **Step 6: Add round_robin.cpp to src/labios/CMakeLists.txt**

Add `solver/round_robin.cpp` to the labios STATIC sources.

- [ ] **Step 7: Run tests**

Run: `cmake --build build/dev -j$(nproc) && ctest --test-dir build/dev -L unit --output-on-failure`
Expected: All tests pass (config + label + solver).

- [ ] **Step 8: Commit**

```bash
git add include/labios/solver/solver.h include/labios/solver/round_robin.h \
    src/labios/solver/round_robin.cpp tests/unit/solver_test.cpp \
    src/labios/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: solver concept interface with round-robin implementation"
```

---

### Task 8: Client Library

**Files:**
- Create: `include/labios/client.h`
- Create: `src/labios/client.cpp`
- Modify: `src/labios/CMakeLists.txt`

- [ ] **Step 1: Create include/labios/client.h**

```cpp
#pragma once

#include <labios/config.h>
#include <labios/label.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace labios {

class Status {
public:
    void wait();
    [[nodiscard]] bool ready() const;
    [[nodiscard]] CompletionStatus result() const;
    [[nodiscard]] std::string error() const;
    [[nodiscard]] std::string data_key() const;
    [[nodiscard]] uint64_t label_id() const;

private:
    friend class Client;
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

class Label {
public:
    [[nodiscard]] uint64_t id() const { return data_.id; }
    [[nodiscard]] LabelType type() const { return data_.type; }
    [[nodiscard]] const LabelData& data() const { return data_; }
    [[nodiscard]] const std::vector<std::byte>& serialized() const { return serialized_; }

private:
    friend class Client;
    LabelData data_;
    std::vector<std::byte> serialized_;
};

class Client {
public:
    explicit Client(const Config& cfg);
    ~Client();

    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    Label create_label(const LabelParams& params);
    Status publish(const Label& label,
                   std::span<const std::byte> data = {});

    void write(std::string_view filepath,
               std::span<const std::byte> data,
               uint64_t offset = 0);

    std::vector<std::byte> read(std::string_view filepath,
                                uint64_t offset,
                                uint64_t size);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

Client connect(const Config& cfg);

} // namespace labios
```

- [ ] **Step 2: Create src/labios/client.cpp**

```cpp
#include <labios/client.h>
#include <labios/catalog.h>
#include <labios/warehouse.h>
#include <labios/transport/nats.h>
#include <labios/transport/redis.h>

#include <chrono>
#include <mutex>
#include <stdexcept>
#include <unistd.h>

namespace labios {

struct Status::Impl {
    uint64_t label_id = 0;
    std::vector<std::byte> reply_data;
    bool completed = false;
    CompletionData completion;
    mutable std::mutex mu;
};

void Status::wait() {
    // reply_data is populated by Client::publish before returning Status
    // For synchronous request-reply, the data is already available
    std::lock_guard lock(impl_->mu);
    if (!impl_->completed && !impl_->reply_data.empty()) {
        impl_->completion = deserialize_completion(impl_->reply_data);
        impl_->completed = true;
    }
}

bool Status::ready() const {
    std::lock_guard lock(impl_->mu);
    return impl_->completed;
}

CompletionStatus Status::result() const {
    std::lock_guard lock(impl_->mu);
    return impl_->completion.status;
}

std::string Status::error() const {
    std::lock_guard lock(impl_->mu);
    return impl_->completion.error;
}

std::string Status::data_key() const {
    std::lock_guard lock(impl_->mu);
    return impl_->completion.data_key;
}

uint64_t Status::label_id() const {
    return impl_->label_id;
}

struct Client::Impl {
    Config cfg;
    transport::RedisConnection redis;
    transport::NatsConnection nats;
    Warehouse warehouse;
    CatalogManager catalog;
    uint32_t app_id;

    explicit Impl(const Config& c)
        : cfg(c)
        , redis(c.redis_host, c.redis_port)
        , nats(c.nats_url)
        , warehouse(redis)
        , catalog(redis)
        , app_id(static_cast<uint32_t>(getpid())) {}
};

Client::Client(const Config& cfg)
    : impl_(std::make_unique<Impl>(cfg)) {}

Client::~Client() = default;

Label Client::create_label(const LabelParams& params) {
    Label label;
    label.data_.id = generate_label_id(impl_->app_id);
    label.data_.type = params.type;
    label.data_.source = params.source;
    label.data_.destination = params.destination;
    label.data_.operation = params.operation;
    label.data_.flags = params.flags;
    label.data_.priority = params.priority;
    label.data_.app_id = impl_->app_id;
    label.data_.intent = params.intent;
    label.data_.ttl_seconds = params.ttl_seconds;
    label.data_.isolation = params.isolation;
    label.serialized_ = serialize_label(label.data_);
    return label;
}

Status Client::publish(const Label& label,
                       std::span<const std::byte> data) {
    // Stage data for WRITE
    if (label.type() == LabelType::Write && !data.empty()) {
        impl_->warehouse.stage(label.id(), data);
        // Update data_size in the serialized label
        LabelData updated = label.data();
        updated.data_size = data.size();
        auto reserialized = serialize_label(updated);

        impl_->catalog.create(label.id(), impl_->app_id, label.type());

        auto reply = impl_->nats.request(
            "labios.labels", reserialized,
            std::chrono::milliseconds(30000));

        auto status_impl = std::make_shared<Status::Impl>();
        status_impl->label_id = label.id();
        status_impl->reply_data = std::move(reply.data);
        Status s;
        s.impl_ = status_impl;
        s.wait();
        return s;
    }

    // READ or other types
    impl_->catalog.create(label.id(), impl_->app_id, label.type());

    auto reply = impl_->nats.request(
        "labios.labels", label.serialized(),
        std::chrono::milliseconds(30000));

    auto status_impl = std::make_shared<Status::Impl>();
    status_impl->label_id = label.id();
    status_impl->reply_data = std::move(reply.data);
    Status s;
    s.impl_ = status_impl;
    s.wait();
    return s;
}

void Client::write(std::string_view filepath,
                   std::span<const std::byte> data,
                   uint64_t offset) {
    auto label = create_label({
        .type = LabelType::Write,
        .source = memory_ptr(data.data(), data.size()),
        .destination = file_path(filepath, offset, data.size()),
    });
    auto status = publish(label, data);
    if (status.result() == CompletionStatus::Error) {
        throw std::runtime_error("write failed: " + status.error());
    }
}

std::vector<std::byte> Client::read(std::string_view filepath,
                                     uint64_t offset,
                                     uint64_t size) {
    auto label = create_label({
        .type = LabelType::Read,
        .source = file_path(filepath, offset, size),
        .destination = memory_ptr(nullptr, size),
    });
    // Set data_size on the label for the worker
    LabelData updated = label.data();
    updated.data_size = size;
    Label read_label;
    read_label.data_ = updated;
    read_label.serialized_ = serialize_label(updated);

    auto status = publish(read_label);
    if (status.result() == CompletionStatus::Error) {
        throw std::runtime_error("read failed: " + status.error());
    }
    auto result = impl_->warehouse.retrieve(label.id());
    impl_->warehouse.remove(label.id());
    return result;
}

Client connect(const Config& cfg) {
    return Client(cfg);
}

} // namespace labios
```

- [ ] **Step 3: Add client.cpp to CMakeLists**

Add `client.cpp` to the labios STATIC library sources.

- [ ] **Step 4: Verify build**

Run: `cmake --build build/dev -j$(nproc)`
Expected: Compiles. All unit tests pass.

- [ ] **Step 5: Commit**

```bash
git add include/labios/client.h src/labios/client.cpp src/labios/CMakeLists.txt
git commit -m "feat: client library with 2.0 agent API (create_label/publish/wait)"
```

---

### Task 9: Dispatcher Rewrite

**Files:**
- Modify: `src/services/labios-dispatcher.cpp`

- [ ] **Step 1: Rewrite labios-dispatcher.cpp**

Replace the M0 stub with a real dispatcher that deserializes labels, runs the round-robin solver, and forwards to workers. The dispatcher needs to:

1. Build a worker list from config (3 workers, IDs 1-3)
2. Subscribe to `labios.labels`
3. On each message: deserialize label, extract reply_to from NATS message, inject reply_to into label, run solver, publish to assigned worker subject

```cpp
#include <labios/catalog.h>
#include <labios/config.h>
#include <labios/label.h>
#include <labios/solver/round_robin.h>
#include <labios/transport/nats.h>
#include <labios/transport/redis.h>

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

static std::jthread g_service_thread;

static std::string timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
    gmtime_r(&time, &tm_buf);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm_buf);
    return buf;
}

static void signal_handler(int) {
    if (g_service_thread.joinable()) g_service_thread.request_stop();
}

int main() {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    const char* config_path = std::getenv("LABIOS_CONFIG_PATH");
    auto cfg = labios::load_config(config_path ? config_path : "conf/labios.toml");

    labios::transport::RedisConnection redis(cfg.redis_host, cfg.redis_port);
    labios::transport::NatsConnection nats(cfg.nats_url);
    labios::CatalogManager catalog(redis);
    labios::RoundRobinSolver solver;

    // Build worker list (hardcoded for M1a; M3+ reads from worker manager)
    std::vector<labios::WorkerInfo> workers = {{1, true}, {2, true}, {3, true}};

    std::mutex mu;

    nats.subscribe("labios.labels",
        [&](std::string_view /*subject*/, std::span<const std::byte> data,
            std::string_view reply_to) {
            std::lock_guard lock(mu);
            auto label = labios::deserialize_label(data);

            // Inject reply-to so the worker can respond to the client
            label.reply_to = std::string(reply_to);
            auto reserialized = labios::serialize_label(label);

            catalog.set_status(label.id, labios::LabelStatus::Scheduled);

            auto assignments = solver.assign({reserialized}, workers);
            for (auto& [worker_id, labels] : assignments) {
                catalog.set_worker(label.id, worker_id);
                std::string subject = "labios.worker." + std::to_string(worker_id);
                for (auto& lbl : labels) {
                    nats.publish(subject, std::span<const std::byte>(lbl));
                }
            }
            nats.flush();

            std::cout << "[" << timestamp() << "] dispatcher: label "
                      << label.id << " -> worker "
                      << (assignments.empty() ? -1 : assignments.begin()->first)
                      << "\n" << std::flush;
        });

    redis.set("labios:ready:dispatcher", "1");
    { std::ofstream touch("/tmp/labios-ready"); }

    std::cout << "[" << timestamp()
              << "] dispatcher ready (solver: round-robin, workers: "
              << workers.size() << ")\n" << std::flush;

    g_service_thread = std::jthread([](std::stop_token stoken) {
        while (!stoken.stop_requested())
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    });
    g_service_thread.join();

    std::cout << "[" << timestamp() << "] dispatcher shutting down\n";
    return 0;
}
```

- [ ] **Step 2: Verify build**

Run: `cmake --build build/dev -j$(nproc)`
Expected: Compiles, unit tests pass.

- [ ] **Step 3: Commit**

```bash
git add src/services/labios-dispatcher.cpp
git commit -m "feat: dispatcher with label deserialization, solver, and worker routing"
```

---

### Task 10: Worker Rewrite

**Files:**
- Modify: `src/services/labios-worker.cpp`

- [ ] **Step 1: Rewrite labios-worker.cpp**

Replace the M0 stub with a real worker that deserializes labels, executes READ/WRITE against local storage, and publishes completion.

```cpp
#include <labios/catalog.h>
#include <labios/config.h>
#include <labios/label.h>
#include <labios/warehouse.h>
#include <labios/transport/nats.h>
#include <labios/transport/redis.h>

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

namespace fs = std::filesystem;

static std::jthread g_service_thread;

static std::string timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
    gmtime_r(&time, &tm_buf);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm_buf);
    return buf;
}

static void signal_handler(int) {
    if (g_service_thread.joinable()) g_service_thread.request_stop();
}

static std::string get_storage_root() {
    const char* root = std::getenv("LABIOS_STORAGE_ROOT");
    return root ? root : "/labios/data";
}

int main() {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    const char* config_path = std::getenv("LABIOS_CONFIG_PATH");
    auto cfg = labios::load_config(config_path ? config_path : "conf/labios.toml");

    labios::transport::RedisConnection redis(cfg.redis_host, cfg.redis_port);
    labios::transport::NatsConnection nats(cfg.nats_url);
    labios::Warehouse warehouse(redis);
    labios::CatalogManager catalog(redis);

    std::string storage_root = get_storage_root();
    fs::create_directories(storage_root);

    int worker_id = cfg.worker_id;
    std::string worker_subject = "labios.worker." + std::to_string(worker_id);
    std::string worker_name = "worker-" + std::to_string(worker_id);

    std::mutex mu;

    nats.subscribe(worker_subject,
        [&](std::string_view /*subject*/, std::span<const std::byte> data,
            std::string_view /*reply_to*/) {
            std::lock_guard lock(mu);
            auto label = labios::deserialize_label(data);
            catalog.set_status(label.id, labios::LabelStatus::Executing);

            labios::CompletionData completion;
            completion.label_id = label.id;

            try {
                if (label.type == labios::LabelType::Write) {
                    // WRITE: get data from warehouse, write to local file
                    auto file_data = warehouse.retrieve(label.id);

                    auto* dst = std::get_if<labios::FilePath>(&label.destination);
                    if (!dst) throw std::runtime_error("WRITE label missing FilePath destination");

                    fs::path filepath = fs::path(storage_root) / dst->path;
                    fs::create_directories(filepath.parent_path());

                    std::ofstream out(filepath, std::ios::binary | std::ios::in | std::ios::out);
                    if (!out.is_open()) {
                        // File might not exist yet; create it
                        out.open(filepath, std::ios::binary | std::ios::out);
                    }
                    if (dst->offset > 0) out.seekp(static_cast<std::streamoff>(dst->offset));
                    out.write(reinterpret_cast<const char*>(file_data.data()),
                              static_cast<std::streamsize>(file_data.size()));
                    out.close();

                    warehouse.remove(label.id);
                    completion.status = labios::CompletionStatus::Complete;

                    std::cout << "[" << timestamp() << "] " << worker_name
                              << ": WRITE " << dst->path
                              << " (" << file_data.size() << " bytes)\n" << std::flush;

                } else if (label.type == labios::LabelType::Read) {
                    // READ: read from local file, stage in warehouse
                    auto* src = std::get_if<labios::FilePath>(&label.source);
                    if (!src) throw std::runtime_error("READ label missing FilePath source");

                    fs::path filepath = fs::path(storage_root) / src->path;
                    std::ifstream in(filepath, std::ios::binary);
                    if (!in.is_open()) throw std::runtime_error("file not found: " + filepath.string());

                    if (src->offset > 0) in.seekg(static_cast<std::streamoff>(src->offset));
                    uint64_t read_size = label.data_size > 0 ? label.data_size : src->length;
                    std::vector<std::byte> file_data(read_size);
                    in.read(reinterpret_cast<char*>(file_data.data()),
                            static_cast<std::streamsize>(read_size));
                    auto actual = static_cast<uint64_t>(in.gcount());
                    file_data.resize(actual);

                    warehouse.stage(label.id, file_data);
                    completion.status = labios::CompletionStatus::Complete;
                    completion.data_key = labios::Warehouse::data_key(label.id);

                    std::cout << "[" << timestamp() << "] " << worker_name
                              << ": READ " << src->path
                              << " (" << actual << " bytes)\n" << std::flush;
                }

                catalog.set_status(label.id, labios::LabelStatus::Complete);

            } catch (const std::exception& e) {
                completion.status = labios::CompletionStatus::Error;
                completion.error = e.what();
                catalog.set_status(label.id, labios::LabelStatus::Error);
                std::cerr << "[" << timestamp() << "] " << worker_name
                          << ": ERROR " << e.what() << "\n" << std::flush;
            }

            // Send completion to client's reply inbox
            if (!label.reply_to.empty()) {
                auto comp_bytes = labios::serialize_completion(completion);
                nats.publish(label.reply_to,
                             std::span<const std::byte>(comp_bytes));
                nats.flush();
            }
        });

    redis.set("labios:ready:" + worker_name, "1");
    { std::ofstream touch("/tmp/labios-ready"); }

    std::cout << "[" << timestamp() << "] " << worker_name
              << " ready (speed=" << cfg.worker_speed
              << ", capacity=" << cfg.worker_capacity << ")\n" << std::flush;

    g_service_thread = std::jthread([](std::stop_token stoken) {
        while (!stoken.stop_requested())
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    });
    g_service_thread.join();

    std::cout << "[" << timestamp() << "] " << worker_name << " shutting down\n";
    return 0;
}
```

- [ ] **Step 2: Update docker-compose.yml to add storage volume**

Add a volume for worker local storage. Each worker needs a `/labios/data` directory. Add to each worker service:

```yaml
    volumes:
      - worker-1-data:/labios/data
```

And at the bottom of the file:
```yaml
volumes:
  worker-1-data:
  worker-2-data:
  worker-3-data:
```

- [ ] **Step 3: Verify build**

Run: `cmake --build build/dev -j$(nproc)`
Expected: Compiles, unit tests pass.

- [ ] **Step 4: Commit**

```bash
git add src/services/labios-worker.cpp docker-compose.yml
git commit -m "feat: worker executes WRITE and READ with warehouse and catalog"
```

---

### Task 11: Integration Tests

**Files:**
- Create: `tests/integration/data_path_test.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `Dockerfile` (add client lib to test image)

- [ ] **Step 1: Create tests/integration/data_path_test.cpp**

```cpp
#include <catch2/catch_test_macros.hpp>
#include <labios/client.h>
#include <labios/catalog.h>
#include <labios/config.h>
#include <labios/transport/redis.h>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <numeric>
#include <string>
#include <vector>

static labios::Config test_config() {
    labios::Config cfg;
    const char* nats = std::getenv("LABIOS_NATS_URL");
    if (nats) cfg.nats_url = nats;
    const char* redis_host = std::getenv("LABIOS_REDIS_HOST");
    if (redis_host) cfg.redis_host = redis_host;
    return cfg;
}

TEST_CASE("Write 1MB and read it back", "[data_path]") {
    auto cfg = test_config();
    auto client = labios::connect(cfg);

    // Generate 1MB of known data
    std::vector<std::byte> data(1024 * 1024);
    std::iota(reinterpret_cast<uint8_t*>(data.data()),
              reinterpret_cast<uint8_t*>(data.data()) + data.size(),
              static_cast<uint8_t>(0));

    client.write("/test/data_path_1mb.bin", data);

    auto result = client.read("/test/data_path_1mb.bin", 0, data.size());
    REQUIRE(result.size() == data.size());
    REQUIRE(std::equal(result.begin(), result.end(), data.begin()));
}

TEST_CASE("Write 10 labels and verify all complete", "[data_path]") {
    auto cfg = test_config();
    auto client = labios::connect(cfg);
    labios::transport::RedisConnection redis(cfg.redis_host, cfg.redis_port);
    labios::CatalogManager catalog(redis);

    std::vector<uint64_t> label_ids;

    for (int i = 0; i < 10; ++i) {
        std::vector<std::byte> data(1024, static_cast<std::byte>(i));
        std::string path = "/test/batch_" + std::to_string(i) + ".bin";

        auto label = client.create_label({
            .type = labios::LabelType::Write,
            .source = labios::memory_ptr(data.data(), data.size()),
            .destination = labios::file_path(path),
        });
        auto status = client.publish(label, data);
        REQUIRE(status.result() == labios::CompletionStatus::Complete);
        label_ids.push_back(status.label_id());
    }

    for (auto id : label_ids) {
        auto status = catalog.get_status(id);
        REQUIRE(status == labios::LabelStatus::Complete);
    }
}
```

- [ ] **Step 2: Add data_path_test to tests/CMakeLists.txt**

```cmake
add_executable(labios-data-path-test integration/data_path_test.cpp)
target_link_libraries(labios-data-path-test PRIVATE labios Catch2::Catch2WithMain)
catch_discover_tests(labios-data-path-test TEST_PREFIX "smoke/" PROPERTIES LABELS "smoke")
```

- [ ] **Step 3: Update Dockerfile test stage**

The test stage needs the `labios-data-path-test` binary too. Add a COPY line:

```dockerfile
COPY --from=builder /src/build/release/tests/labios-data-path-test /usr/local/bin/
```

- [ ] **Step 4: Verify build**

Run: `cmake --build build/dev -j$(nproc)`
Expected: Compiles.

- [ ] **Step 5: Docker build and integration test**

Run:
```bash
docker compose build
docker compose up -d
docker compose run --rm test labios-data-path-test
docker compose down -v
```
Expected: Both data_path tests pass.

- [ ] **Step 6: Commit**

```bash
git add tests/integration/data_path_test.cpp tests/CMakeLists.txt Dockerfile
git commit -m "feat: integration tests for end-to-end write/read data path"
```

---

### Task 12: Demo Binary + Final Verification

**Files:**
- Create: `src/services/labios-demo.cpp`
- Modify: `src/services/CMakeLists.txt`
- Modify: `Dockerfile`

- [ ] **Step 1: Create src/services/labios-demo.cpp**

```cpp
#include <labios/client.h>
#include <labios/config.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

int main() {
    const char* config_path = std::getenv("LABIOS_CONFIG_PATH");
    auto cfg = labios::load_config(config_path ? config_path : "conf/labios.toml");
    auto client = labios::connect(cfg);

    constexpr uint64_t label_size = 1024 * 1024;  // 1MB
    constexpr int num_labels = 100;                // 100MB total
    constexpr uint64_t total_size = label_size * num_labels;

    // Generate data pattern
    std::vector<std::byte> label_data(label_size);
    std::iota(reinterpret_cast<uint8_t*>(label_data.data()),
              reinterpret_cast<uint8_t*>(label_data.data()) + label_size,
              static_cast<uint8_t>(0));

    // WRITE phase
    auto write_start = std::chrono::steady_clock::now();
    for (int i = 0; i < num_labels; ++i) {
        std::string path = "/demo/chunk_" + std::to_string(i) + ".bin";
        client.write(path, label_data);
    }
    auto write_end = std::chrono::steady_clock::now();
    double write_sec = std::chrono::duration<double>(write_end - write_start).count();
    double write_mbps = (static_cast<double>(total_size) / (1024.0 * 1024.0)) / write_sec;

    std::cout << "Written: " << (total_size / (1024 * 1024)) << "MB ("
              << num_labels << " labels) in " << write_sec << "s ("
              << write_mbps << " MB/s)\n";

    // READ phase
    auto read_start = std::chrono::steady_clock::now();
    bool verify_ok = true;
    for (int i = 0; i < num_labels; ++i) {
        std::string path = "/demo/chunk_" + std::to_string(i) + ".bin";
        auto result = client.read(path, 0, label_size);
        if (result.size() != label_size ||
            !std::equal(result.begin(), result.end(), label_data.begin())) {
            verify_ok = false;
            std::cerr << "Mismatch at chunk " << i << "\n";
        }
    }
    auto read_end = std::chrono::steady_clock::now();
    double read_sec = std::chrono::duration<double>(read_end - read_start).count();
    double read_mbps = (static_cast<double>(total_size) / (1024.0 * 1024.0)) / read_sec;

    std::cout << "Read:    " << (total_size / (1024 * 1024)) << "MB ("
              << num_labels << " labels) in " << read_sec << "s ("
              << read_mbps << " MB/s)\n";
    std::cout << "Verify:  " << (verify_ok ? "OK (all bytes match)" : "FAILED") << "\n";

    return verify_ok ? 0 : 1;
}
```

- [ ] **Step 2: Add demo binary to src/services/CMakeLists.txt**

```cmake
add_executable(labios-demo labios-demo.cpp)
target_link_libraries(labios-demo PRIVATE labios)
```

- [ ] **Step 3: Update Dockerfile test stage**

Add to the test stage:
```dockerfile
COPY --from=builder /src/build/release/src/services/labios-demo /usr/local/bin/
```

- [ ] **Step 4: Full Docker integration test**

```bash
docker compose build
docker compose up -d
docker compose run --rm test labios-smoke-test  # M0 smoke tests still pass
docker compose run --rm test labios-data-path-test  # M1a integration tests
docker compose run --rm test labios-demo  # 100MB throughput demo
docker compose down -v
```

Expected: All tests pass. Demo prints write/read throughput and "Verify: OK".

- [ ] **Step 5: Commit**

```bash
git add src/services/labios-demo.cpp src/services/CMakeLists.txt Dockerfile
git commit -m "feat: 100MB write/read throughput demo binary"
```
