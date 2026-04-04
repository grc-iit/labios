# M1b: Client Architecture Rebuild + POSIX Intercept — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Decompose the monolithic Client into LabelManager/ContentManager/CatalogManager, implement label splitting (1-to-N) and small-I/O cache (N-to-1), and build a production-grade POSIX intercept layer with the adapter pattern.

**Architecture:** Two phases. Phase 1 decomposes the Client into three managers with splitting and caching, keeping existing tests passing. Phase 2 layers a POSIX intercept (LD_PRELOAD) on top using the adapter pattern with memfd_create fds, full POSIX data path, and path prefix filtering.

**Tech Stack:** C++20 (jthread, shared_mutex, concepts, variant), FlatBuffers, NATS JetStream, Redis 7, Catch2, memfd_create, dlsym/LD_PRELOAD

**Spec:** `docs/superpowers/specs/2026-04-04-m1b-client-architecture-posix-intercept-design.md`

---

## File Map

### New Files

| File | Responsibility |
|---|---|
| `include/labios/session.h` | Session: owns connections, creates managers |
| `src/labios/session.cpp` | Session implementation |
| `include/labios/label_manager.h` | LabelManager: split, publish, wait |
| `src/labios/label_manager.cpp` | LabelManager implementation |
| `include/labios/content_manager.h` | ContentManager: warehouse + cache |
| `src/labios/content_manager.cpp` | ContentManager implementation |
| `include/labios/adapter/adapter.h` | IOAdapter concept definition |
| `include/labios/adapter/fd_table.h` | FdTable: thread-safe fd tracking |
| `src/labios/adapter/fd_table.cpp` | FdTable implementation |
| `include/labios/adapter/posix_adapter.h` | POSIXAdapter: fd lifecycle, offset, sync |
| `src/labios/adapter/posix_adapter.cpp` | POSIXAdapter implementation |
| `src/drivers/posix_intercept.cpp` | LD_PRELOAD symbol overrides |
| `tests/unit/label_manager_test.cpp` | Splitting logic tests |
| `tests/unit/content_manager_test.cpp` | Cache logic tests |
| `tests/unit/catalog_manager_test.cpp` | File metadata tests |
| `tests/unit/fd_table_test.cpp` | Fd allocation + thread safety tests |
| `tests/integration/intercept_test.cpp` | LD_PRELOAD integration tests |

### Modified Files

| File | Change |
|---|---|
| `include/labios/config.h` | Add label/cache/intercept config fields |
| `src/labios/config.cpp` | Parse new TOML sections + env vars |
| `include/labios/catalog.h` → `include/labios/catalog_manager.h` | Rename + add file metadata |
| `src/labios/catalog.cpp` → `src/labios/catalog_manager.cpp` | Rename + add file metadata |
| `include/labios/client.h` | Rewrite to compose Session + managers |
| `src/labios/client.cpp` | Rewrite write/read orchestration |
| `src/services/labios-dispatcher.cpp` | Write-locality for split chunks |
| `src/services/labios-worker.cpp` | Use CatalogManager rename |
| `src/services/labios-demo.cpp` | Use new Client API |
| `conf/labios.toml` | Add label/cache/intercept sections |
| `src/labios/CMakeLists.txt` | New source files + shared library target |
| `tests/CMakeLists.txt` | New test executables |
| `Dockerfile` | Add liblabios_intercept.so to test image |
| `docker-compose.yml` | No changes needed |

### Deleted Files

| File | Replaced By |
|---|---|
| `include/labios/warehouse.h` | `include/labios/content_manager.h` |
| `src/labios/warehouse.cpp` | `src/labios/content_manager.cpp` |

---

## Phase 1: Client Architecture Rebuild

### Task 1: Extend Config

**Files:**
- Modify: `include/labios/config.h`
- Modify: `src/labios/config.cpp`
- Modify: `conf/labios.toml`
- Modify: `tests/unit/config_test.cpp`

- [ ] **Step 1: Add new fields to Config struct**

In `include/labios/config.h`, add after `std::string service_name;`:

```cpp
struct Config {
    std::string nats_url = "nats://localhost:4222";
    std::string redis_host = "localhost";
    int redis_port = 6379;
    int worker_id = 0;
    int worker_speed = 1;
    std::string worker_capacity = "1GB";
    std::string service_name;

    // Label granularity (Section 2.2)
    uint64_t label_min_size = 64 * 1024;       // 64KB
    uint64_t label_max_size = 1024 * 1024;     // 1MB

    // Small-I/O cache (Content Manager)
    int cache_flush_interval_ms = 500;
    std::string cache_read_policy = "read-through"; // or "write-only"

    // POSIX intercept
    std::vector<std::string> intercept_prefixes = {"/labios"};
};

uint64_t parse_size(std::string_view s);  // "64KB" → 65536
```

- [ ] **Step 2: Implement parse_size and config loading**

In `src/labios/config.cpp`, add `parse_size()` and extend `load_config()`:

```cpp
uint64_t parse_size(std::string_view s) {
    if (s.empty()) return 0;
    char* end = nullptr;
    double val = std::strtod(std::string(s).c_str(), &end);
    std::string_view suffix(end);
    if (suffix == "KB" || suffix == "kb") return static_cast<uint64_t>(val * 1024);
    if (suffix == "MB" || suffix == "mb") return static_cast<uint64_t>(val * 1024 * 1024);
    if (suffix == "GB" || suffix == "gb") return static_cast<uint64_t>(val * 1024 * 1024 * 1024);
    return static_cast<uint64_t>(val);
}
```

In `load_config()`, after existing parsing, add:

```cpp
    // Label granularity
    if (auto v = tbl["label"]["min_size"].value<std::string>())
        cfg.label_min_size = parse_size(*v);
    if (auto v = tbl["label"]["max_size"].value<std::string>())
        cfg.label_max_size = parse_size(*v);

    // Cache
    cfg.cache_flush_interval_ms = tbl["cache"]["flush_interval_ms"].value_or(cfg.cache_flush_interval_ms);
    cfg.cache_read_policy = tbl["cache"]["default_read_policy"].value_or(cfg.cache_read_policy);

    // Intercept
    if (auto arr = tbl["intercept"]["prefixes"].as_array()) {
        cfg.intercept_prefixes.clear();
        for (auto& elem : *arr) {
            if (auto s = elem.value<std::string>())
                cfg.intercept_prefixes.push_back(*s);
        }
    }
```

And env var overrides:

```cpp
    auto env_size = [](const char* name, uint64_t fallback) -> uint64_t {
        const char* val = std::getenv(name);
        if (val != nullptr && val[0] != '\0') return parse_size(val);
        return fallback;
    };

    cfg.label_min_size = env_size("LABIOS_LABEL_MIN_SIZE", cfg.label_min_size);
    cfg.label_max_size = env_size("LABIOS_LABEL_MAX_SIZE", cfg.label_max_size);
    cfg.cache_flush_interval_ms = env_int_or("LABIOS_CACHE_FLUSH_MS", cfg.cache_flush_interval_ms);
    cfg.cache_read_policy = env_or("LABIOS_CACHE_READ_POLICY", cfg.cache_read_policy);

    const char* prefixes_env = std::getenv("LABIOS_INTERCEPT_PREFIXES");
    if (prefixes_env != nullptr && prefixes_env[0] != '\0') {
        cfg.intercept_prefixes.clear();
        std::string_view sv(prefixes_env);
        size_t pos = 0;
        while (pos < sv.size()) {
            auto comma = sv.find(',', pos);
            if (comma == std::string_view::npos) comma = sv.size();
            cfg.intercept_prefixes.emplace_back(sv.substr(pos, comma - pos));
            pos = comma + 1;
        }
    }
```

- [ ] **Step 3: Update TOML config file**

In `conf/labios.toml`, add:

```toml
[label]
min_size = "64KB"
max_size = "1MB"

[cache]
flush_interval_ms = 500
default_read_policy = "read-through"

[intercept]
prefixes = ["/labios"]
```

- [ ] **Step 4: Write config tests**

Add to `tests/unit/config_test.cpp`:

```cpp
TEST_CASE("parse_size handles units", "[config]") {
    REQUIRE(labios::parse_size("64KB") == 65536);
    REQUIRE(labios::parse_size("1MB") == 1048576);
    REQUIRE(labios::parse_size("2GB") == 2147483648ULL);
    REQUIRE(labios::parse_size("4096") == 4096);
}

TEST_CASE("load_config reads label and cache settings", "[config]") {
    auto path = write_temp_toml(R"(
[nats]
url = "nats://localhost:4222"

[label]
min_size = "128KB"
max_size = "4MB"

[cache]
flush_interval_ms = 1000
default_read_policy = "write-only"

[intercept]
prefixes = ["/labios", "/scratch"]
)");

    auto cfg = labios::load_config(path);
    REQUIRE(cfg.label_min_size == 128 * 1024);
    REQUIRE(cfg.label_max_size == 4 * 1024 * 1024);
    REQUIRE(cfg.cache_flush_interval_ms == 1000);
    REQUIRE(cfg.cache_read_policy == "write-only");
    REQUIRE(cfg.intercept_prefixes.size() == 2);
    REQUIRE(cfg.intercept_prefixes[0] == "/labios");
    REQUIRE(cfg.intercept_prefixes[1] == "/scratch");

    fs::remove(path);
}
```

- [ ] **Step 5: Build and run tests**

Run: `cmake --build build/dev -j$(nproc) && ctest --test-dir build/dev -R "unit/" --output-on-failure`

Expected: All unit tests pass including new config tests.

- [ ] **Step 6: Commit**

```bash
git add include/labios/config.h src/labios/config.cpp conf/labios.toml tests/unit/config_test.cpp
git commit -m "feat: extend Config with label granularity, cache, and intercept settings"
```

---

### Task 2: Rename CatalogManager + Add File Metadata

**Files:**
- Rename: `include/labios/catalog.h` → `include/labios/catalog_manager.h`
- Rename: `src/labios/catalog.cpp` → `src/labios/catalog_manager.cpp`
- Modify: all files that `#include <labios/catalog.h>`
- Create: `tests/unit/catalog_manager_test.cpp`

- [ ] **Step 1: Rename files and update includes**

```bash
git mv include/labios/catalog.h include/labios/catalog_manager.h
git mv src/labios/catalog.cpp src/labios/catalog_manager.cpp
```

Update `#include <labios/catalog.h>` → `#include <labios/catalog_manager.h>` in:
- `src/labios/client.cpp`
- `src/services/labios-dispatcher.cpp`
- `src/services/labios-worker.cpp`
- `tests/integration/data_path_test.cpp`
- `tests/integration/smoke_test.cpp` (if it includes it)
- `src/labios/CMakeLists.txt` (source file name)

In `src/labios/CMakeLists.txt`, change `catalog.cpp` to `catalog_manager.cpp`.

- [ ] **Step 2: Add FileInfo and file metadata methods to catalog_manager.h**

Add after the existing `get_worker` method:

```cpp
struct FileInfo {
    uint64_t size = 0;
    uint64_t mtime_ms = 0;
    bool exists = false;
};

class CatalogManager {
public:
    // ... existing methods ...

    // File metadata for POSIX stat/fstat
    void track_open(std::string_view filepath, int flags);
    void track_write(std::string_view filepath, uint64_t offset, uint64_t size);
    void track_unlink(std::string_view filepath);
    void track_truncate(std::string_view filepath, uint64_t new_size);
    std::optional<FileInfo> get_file_info(std::string_view filepath);

private:
    // ... existing ...
    static std::string filemeta_key(std::string_view filepath);
};
```

- [ ] **Step 3: Implement file metadata methods in catalog_manager.cpp**

```cpp
std::string CatalogManager::filemeta_key(std::string_view filepath) {
    return "labios:filemeta:" + std::string(filepath);
}

void CatalogManager::track_open(std::string_view filepath, int flags) {
    auto key = filemeta_key(filepath);
    // O_CREAT = 0100 on Linux
    if (flags & 0100) {
        redis_.hset(key, "exists", "1");
        auto existing = redis_.hget(key, "size");
        if (!existing.has_value()) {
            redis_.hset(key, "size", "0");
        }
        redis_.hset(key, "mtime", now_ms());
    }
}

void CatalogManager::track_write(std::string_view filepath,
                                  uint64_t offset, uint64_t size) {
    auto key = filemeta_key(filepath);
    redis_.hset(key, "exists", "1");
    uint64_t end = offset + size;
    auto cur = redis_.hget(key, "size");
    uint64_t cur_size = cur.has_value() ? std::stoull(*cur) : 0;
    if (end > cur_size) {
        redis_.hset(key, "size", std::to_string(end));
    }
    redis_.hset(key, "mtime", now_ms());
}

void CatalogManager::track_unlink(std::string_view filepath) {
    auto key = filemeta_key(filepath);
    redis_.hset(key, "exists", "0");
    redis_.hset(key, "size", "0");
    redis_.hset(key, "mtime", now_ms());
    // Also remove location mapping
    redis_.del(location_key(filepath));
}

void CatalogManager::track_truncate(std::string_view filepath,
                                     uint64_t new_size) {
    auto key = filemeta_key(filepath);
    redis_.hset(key, "size", std::to_string(new_size));
    redis_.hset(key, "mtime", now_ms());
}

std::optional<FileInfo> CatalogManager::get_file_info(std::string_view filepath) {
    auto key = filemeta_key(filepath);
    auto exists_val = redis_.hget(key, "exists");
    if (!exists_val.has_value()) {
        return std::nullopt;
    }
    FileInfo info;
    info.exists = (*exists_val == "1");
    auto size_val = redis_.hget(key, "size");
    if (size_val.has_value()) {
        info.size = std::stoull(*size_val);
    }
    auto mtime_val = redis_.hget(key, "mtime");
    if (mtime_val.has_value()) {
        info.mtime_ms = std::stoull(*mtime_val);
    }
    return info;
}
```

- [ ] **Step 4: Write catalog_manager_test.cpp**

```cpp
#include <catch2/catch_test_macros.hpp>
#include <labios/catalog_manager.h>
#include <labios/transport/redis.h>
#include <cstdlib>
#include <fcntl.h>

static std::string redis_host() {
    const char* h = std::getenv("LABIOS_REDIS_HOST");
    return (h && h[0]) ? h : "localhost";
}

static int redis_port() {
    const char* val = std::getenv("LABIOS_REDIS_PORT");
    return (val && val[0]) ? std::stoi(val) : 6379;
}

TEST_CASE("File metadata tracks writes", "[catalog_manager]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::CatalogManager catalog(redis);

    catalog.track_open("/test/meta.bin", O_CREAT | O_WRONLY);
    auto info = catalog.get_file_info("/test/meta.bin");
    REQUIRE(info.has_value());
    REQUIRE(info->exists == true);
    REQUIRE(info->size == 0);

    catalog.track_write("/test/meta.bin", 0, 1024);
    info = catalog.get_file_info("/test/meta.bin");
    REQUIRE(info->size == 1024);

    catalog.track_write("/test/meta.bin", 1024, 2048);
    info = catalog.get_file_info("/test/meta.bin");
    REQUIRE(info->size == 3072);

    // Non-extending write within existing range
    catalog.track_write("/test/meta.bin", 0, 512);
    info = catalog.get_file_info("/test/meta.bin");
    REQUIRE(info->size == 3072);  // Unchanged
}

TEST_CASE("File metadata tracks unlink", "[catalog_manager]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::CatalogManager catalog(redis);

    catalog.track_open("/test/del.bin", O_CREAT | O_WRONLY);
    catalog.track_write("/test/del.bin", 0, 100);

    catalog.track_unlink("/test/del.bin");
    auto info = catalog.get_file_info("/test/del.bin");
    REQUIRE(info.has_value());
    REQUIRE(info->exists == false);
}

TEST_CASE("File metadata tracks truncate", "[catalog_manager]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::CatalogManager catalog(redis);

    catalog.track_open("/test/trunc.bin", O_CREAT | O_WRONLY);
    catalog.track_write("/test/trunc.bin", 0, 10000);
    catalog.track_truncate("/test/trunc.bin", 5000);

    auto info = catalog.get_file_info("/test/trunc.bin");
    REQUIRE(info->size == 5000);
}

TEST_CASE("get_file_info returns nullopt for unknown file", "[catalog_manager]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::CatalogManager catalog(redis);

    auto info = catalog.get_file_info("/nonexistent/file.bin");
    REQUIRE_FALSE(info.has_value());
}
```

- [ ] **Step 5: Update CMakeLists.txt and build**

In `tests/CMakeLists.txt`, add:

```cmake
add_executable(labios-catalog-manager-test unit/catalog_manager_test.cpp)
target_link_libraries(labios-catalog-manager-test PRIVATE labios Catch2::Catch2WithMain)
catch_discover_tests(labios-catalog-manager-test TEST_PREFIX "smoke/")
```

Note: these tests need Redis, so use "smoke/" prefix.

Build: `cmake --build build/dev -j$(nproc)`

- [ ] **Step 6: Run all tests to verify rename didn't break anything**

Run: `ctest --test-dir build/dev -R "unit/" --output-on-failure`

Expected: All existing unit tests pass. Catalog manager tests need Redis (run in Docker later).

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "refactor: rename CatalogManager, add file metadata tracking for POSIX"
```

---

### Task 3: ContentManager (warehouse + cache)

**Files:**
- Create: `include/labios/content_manager.h`
- Create: `src/labios/content_manager.cpp`
- Delete: `include/labios/warehouse.h`, `src/labios/warehouse.cpp`
- Create: `tests/unit/content_manager_test.cpp`

- [ ] **Step 1: Write content_manager.h**

```cpp
#pragma once

#include <labios/transport/redis.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace labios {

struct FlushRegion {
    std::string filepath;
    uint64_t offset;
    std::vector<std::byte> data;
};

enum class ReadPolicy { ReadThrough, WriteOnly };

ReadPolicy read_policy_from_string(std::string_view s);

class ContentManager {
public:
    ContentManager(transport::RedisConnection& redis,
                   uint64_t min_label_size,
                   int flush_interval_ms,
                   ReadPolicy default_read_policy);
    ~ContentManager();

    ContentManager(const ContentManager&) = delete;
    ContentManager& operator=(const ContentManager&) = delete;

    // --- Warehouse (label-ID keyed staging) ---
    void stage(uint64_t label_id, std::span<const std::byte> data);
    std::vector<std::byte> retrieve(uint64_t label_id);
    void remove(uint64_t label_id);
    bool exists(uint64_t label_id);
    static std::string data_key(uint64_t label_id);

    // --- Small-I/O Cache ---

    /// Cache a small write. Returns flush regions if size threshold reached.
    std::vector<FlushRegion> cache_write(int fd, std::string_view filepath,
                                          uint64_t offset,
                                          std::span<const std::byte> data);

    /// Read from cache if read-through and data overlaps.
    std::optional<std::vector<std::byte>> cache_read(int fd, uint64_t offset,
                                                      uint64_t size);

    /// Flush all cached data for an fd.
    std::vector<FlushRegion> flush(int fd);

    /// Flush all fds (timer and shutdown).
    std::vector<std::pair<int, std::vector<FlushRegion>>> flush_all();

    /// Remove cache state for a closed fd.
    void evict(int fd);

    /// Set read policy for a specific fd.
    void set_read_policy(int fd, ReadPolicy policy);

    /// Start the background flush timer.
    void start_flush_timer();

    /// Register a callback invoked when the timer flushes.
    /// The callback receives (fd, vector<FlushRegion>) and must publish labels.
    using FlushCallback = std::function<void(int, std::vector<FlushRegion>)>;
    void set_flush_callback(FlushCallback cb);

private:
    transport::RedisConnection& redis_;
    uint64_t min_label_size_;
    int flush_interval_ms_;
    ReadPolicy default_read_policy_;

    struct FdCache {
        std::string filepath;
        ReadPolicy read_policy;
        std::map<uint64_t, std::vector<std::byte>> regions;
        uint64_t total_bytes = 0;
    };

    std::shared_mutex cache_mu_;
    std::unordered_map<int, FdCache> caches_;

    std::jthread flush_thread_;
    FlushCallback flush_callback_;

    std::vector<FlushRegion> flush_locked(FdCache& cache);
};

} // namespace labios
```

- [ ] **Step 2: Write content_manager.cpp**

```cpp
#include <labios/content_manager.h>

#include <algorithm>

namespace labios {

ReadPolicy read_policy_from_string(std::string_view s) {
    if (s == "write-only") return ReadPolicy::WriteOnly;
    return ReadPolicy::ReadThrough;
}

ContentManager::ContentManager(transport::RedisConnection& redis,
                               uint64_t min_label_size,
                               int flush_interval_ms,
                               ReadPolicy default_read_policy)
    : redis_(redis),
      min_label_size_(min_label_size),
      flush_interval_ms_(flush_interval_ms),
      default_read_policy_(default_read_policy) {}

ContentManager::~ContentManager() {
    if (flush_thread_.joinable()) {
        flush_thread_.request_stop();
    }
}

// --- Warehouse ---

std::string ContentManager::data_key(uint64_t label_id) {
    return "labios:data:" + std::to_string(label_id);
}

void ContentManager::stage(uint64_t label_id, std::span<const std::byte> data) {
    redis_.set_binary(data_key(label_id), data);
}

std::vector<std::byte> ContentManager::retrieve(uint64_t label_id) {
    return redis_.get_binary(data_key(label_id));
}

void ContentManager::remove(uint64_t label_id) {
    redis_.del(data_key(label_id));
}

bool ContentManager::exists(uint64_t label_id) {
    return redis_.get(data_key(label_id)).has_value();
}

// --- Cache ---

std::vector<FlushRegion> ContentManager::cache_write(
    int fd, std::string_view filepath, uint64_t offset,
    std::span<const std::byte> data) {

    std::unique_lock lock(cache_mu_);
    auto& cache = caches_[fd];
    if (cache.filepath.empty()) {
        cache.filepath = std::string(filepath);
        cache.read_policy = default_read_policy_;
    }

    cache.regions[offset].assign(data.begin(), data.end());
    cache.total_bytes += data.size();

    if (cache.total_bytes >= min_label_size_) {
        return flush_locked(cache);
    }
    return {};
}

std::optional<std::vector<std::byte>> ContentManager::cache_read(
    int fd, uint64_t offset, uint64_t size) {

    std::shared_lock lock(cache_mu_);
    auto it = caches_.find(fd);
    if (it == caches_.end()) return std::nullopt;
    if (it->second.read_policy == ReadPolicy::WriteOnly) return std::nullopt;

    // Check for exact match at this offset
    auto& regions = it->second.regions;
    auto rit = regions.find(offset);
    if (rit == regions.end()) return std::nullopt;
    if (rit->second.size() < size) return std::nullopt;

    std::vector<std::byte> result(rit->second.begin(),
                                   rit->second.begin() + size);
    return result;
}

std::vector<FlushRegion> ContentManager::flush(int fd) {
    std::unique_lock lock(cache_mu_);
    auto it = caches_.find(fd);
    if (it == caches_.end()) return {};
    return flush_locked(it->second);
}

std::vector<std::pair<int, std::vector<FlushRegion>>>
ContentManager::flush_all() {
    std::unique_lock lock(cache_mu_);
    std::vector<std::pair<int, std::vector<FlushRegion>>> result;
    for (auto& [fd, cache] : caches_) {
        if (cache.total_bytes > 0) {
            result.emplace_back(fd, flush_locked(cache));
        }
    }
    return result;
}

void ContentManager::evict(int fd) {
    std::unique_lock lock(cache_mu_);
    caches_.erase(fd);
}

void ContentManager::set_read_policy(int fd, ReadPolicy policy) {
    std::unique_lock lock(cache_mu_);
    caches_[fd].read_policy = policy;
}

std::vector<FlushRegion> ContentManager::flush_locked(FdCache& cache) {
    std::vector<FlushRegion> result;
    for (auto& [offset, data] : cache.regions) {
        result.push_back({cache.filepath, offset, std::move(data)});
    }
    cache.regions.clear();
    cache.total_bytes = 0;
    return result;
}

void ContentManager::set_flush_callback(FlushCallback cb) {
    flush_callback_ = std::move(cb);
}

void ContentManager::start_flush_timer() {
    if (flush_interval_ms_ <= 0) return;

    flush_thread_ = std::jthread([this](std::stop_token stoken) {
        while (!stoken.stop_requested()) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(flush_interval_ms_));
            if (stoken.stop_requested()) break;

            auto flushed = flush_all();
            if (flush_callback_) {
                for (auto& [fd, regions] : flushed) {
                    flush_callback_(fd, std::move(regions));
                }
            }
        }
    });
}

} // namespace labios
```

- [ ] **Step 3: Write content_manager_test.cpp (cache logic, no Redis)**

```cpp
#include <catch2/catch_test_macros.hpp>
#include <labios/content_manager.h>
#include <labios/transport/redis.h>

#include <cstdlib>
#include <numeric>

static std::string redis_host() {
    const char* h = std::getenv("LABIOS_REDIS_HOST");
    return (h && h[0]) ? h : "localhost";
}

TEST_CASE("Cache accumulates small writes and flushes at threshold", "[content_manager]") {
    labios::transport::RedisConnection redis(redis_host(), 6379);
    // min_label_size = 4KB, no timer, read-through
    labios::ContentManager cm(redis, 4096, 0, labios::ReadPolicy::ReadThrough);

    std::vector<std::byte> data(1024, static_cast<std::byte>(0xAB));

    // 3 writes of 1KB each = 3KB < 4KB threshold → no flush
    auto r1 = cm.cache_write(10, "/test/cache.bin", 0, data);
    REQUIRE(r1.empty());
    auto r2 = cm.cache_write(10, "/test/cache.bin", 1024, data);
    REQUIRE(r2.empty());
    auto r3 = cm.cache_write(10, "/test/cache.bin", 2048, data);
    REQUIRE(r3.empty());

    // 4th write pushes to 4KB → flush
    auto r4 = cm.cache_write(10, "/test/cache.bin", 3072, data);
    REQUIRE(r4.size() == 4);
    REQUIRE(r4[0].offset == 0);
    REQUIRE(r4[0].data.size() == 1024);
    REQUIRE(r4[3].offset == 3072);
}

TEST_CASE("Explicit flush returns all cached data", "[content_manager]") {
    labios::transport::RedisConnection redis(redis_host(), 6379);
    labios::ContentManager cm(redis, 1048576, 0, labios::ReadPolicy::ReadThrough);

    std::vector<std::byte> data(100, static_cast<std::byte>(0xCD));
    cm.cache_write(20, "/test/flush.bin", 0, data);
    cm.cache_write(20, "/test/flush.bin", 100, data);

    auto regions = cm.flush(20);
    REQUIRE(regions.size() == 2);
    REQUIRE(regions[0].filepath == "/test/flush.bin");
}

TEST_CASE("Read-through returns cached data", "[content_manager]") {
    labios::transport::RedisConnection redis(redis_host(), 6379);
    labios::ContentManager cm(redis, 1048576, 0, labios::ReadPolicy::ReadThrough);

    std::vector<std::byte> data(256);
    std::iota(reinterpret_cast<uint8_t*>(data.data()),
              reinterpret_cast<uint8_t*>(data.data()) + 256,
              static_cast<uint8_t>(0));

    cm.cache_write(30, "/test/rt.bin", 0, data);

    auto result = cm.cache_read(30, 0, 256);
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 256);
    REQUIRE((*result)[0] == std::byte{0});
    REQUIRE((*result)[255] == std::byte{255});
}

TEST_CASE("Write-only cache_read returns nullopt", "[content_manager]") {
    labios::transport::RedisConnection redis(redis_host(), 6379);
    labios::ContentManager cm(redis, 1048576, 0, labios::ReadPolicy::WriteOnly);

    std::vector<std::byte> data(256, std::byte{0});
    cm.cache_write(40, "/test/wo.bin", 0, data);

    auto result = cm.cache_read(40, 0, 256);
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("Evict removes cache state", "[content_manager]") {
    labios::transport::RedisConnection redis(redis_host(), 6379);
    labios::ContentManager cm(redis, 1048576, 0, labios::ReadPolicy::ReadThrough);

    std::vector<std::byte> data(100, std::byte{0});
    cm.cache_write(50, "/test/evict.bin", 0, data);
    cm.evict(50);

    auto regions = cm.flush(50);
    REQUIRE(regions.empty());
}

TEST_CASE("Warehouse stage and retrieve", "[content_manager]") {
    labios::transport::RedisConnection redis(redis_host(), 6379);
    labios::ContentManager cm(redis, 4096, 0, labios::ReadPolicy::ReadThrough);

    std::vector<std::byte> data(512);
    std::iota(reinterpret_cast<uint8_t*>(data.data()),
              reinterpret_cast<uint8_t*>(data.data()) + 512,
              static_cast<uint8_t>(0));

    cm.stage(99999, data);
    REQUIRE(cm.exists(99999));

    auto result = cm.retrieve(99999);
    REQUIRE(result.size() == 512);
    REQUIRE(result == data);

    cm.remove(99999);
    REQUIRE_FALSE(cm.exists(99999));
}
```

- [ ] **Step 4: Delete old warehouse files, update CMakeLists.txt**

```bash
git rm include/labios/warehouse.h src/labios/warehouse.cpp
```

In `src/labios/CMakeLists.txt`, replace `warehouse.cpp` with `content_manager.cpp`.

Update all files that `#include <labios/warehouse.h>` to `#include <labios/content_manager.h>`:
- `src/labios/client.cpp`
- `src/services/labios-worker.cpp`

In `tests/CMakeLists.txt`, add:

```cmake
add_executable(labios-content-manager-test unit/content_manager_test.cpp)
target_link_libraries(labios-content-manager-test PRIVATE labios Catch2::Catch2WithMain)
catch_discover_tests(labios-content-manager-test TEST_PREFIX "smoke/")
```

- [ ] **Step 5: Temporarily update client.cpp and worker.cpp to use ContentManager**

The Client and Worker currently use `Warehouse`. Update them to use `ContentManager` with matching method calls (stage/retrieve/remove are the same). The Client constructor needs to create a ContentManager instead of Warehouse. This is a temporary bridge until the full rewrite in Task 7.

In `client.cpp`, replace:
```cpp
Warehouse warehouse;
```
with:
```cpp
ContentManager content_manager;
```

And update the constructor to pass min_label_size, flush_interval_ms, and read_policy from config. Update all `warehouse.` calls to `content_manager.` calls.

Similarly in `labios-worker.cpp`, replace `Warehouse warehouse(redis)` with `ContentManager content_manager(redis, cfg.label_min_size, 0, ReadPolicy::ReadThrough)` and update calls. The worker only uses stage/retrieve/remove, which have the same signatures.

- [ ] **Step 6: Build and run all tests**

Run: `cmake --build build/dev -j$(nproc) && ctest --test-dir build/dev -R "unit/" --output-on-failure`

Expected: All existing tests pass. Content manager tests need Redis.

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "feat: ContentManager with warehouse and small-I/O cache, replaces Warehouse"
```

---

### Task 4: LabelManager with Splitting

**Files:**
- Create: `include/labios/label_manager.h`
- Create: `src/labios/label_manager.cpp`
- Create: `tests/unit/label_manager_test.cpp`

- [ ] **Step 1: Write label_manager.h**

```cpp
#pragma once

#include <labios/catalog_manager.h>
#include <labios/content_manager.h>
#include <labios/label.h>
#include <labios/transport/nats.h>

#include <chrono>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace labios {

struct PendingLabel {
    uint64_t label_id = 0;
    std::vector<std::byte> reply_data;
};

class LabelManager {
public:
    LabelManager(ContentManager& content, CatalogManager& catalog,
                 transport::NatsConnection& nats,
                 uint64_t max_label_size, uint32_t app_id);

    /// Split data into labels of at most max_label_size, stage each chunk,
    /// create catalog entries, publish to NATS. Returns async handles.
    std::vector<PendingLabel> publish_write(
        std::string_view filepath, uint64_t offset,
        std::span<const std::byte> data);

    /// Split read request. Publishes READ labels for each chunk.
    std::vector<PendingLabel> publish_read(
        std::string_view filepath, uint64_t offset, uint64_t size);

    /// Block until all pending labels complete.
    void wait(std::span<PendingLabel> pending);

    /// Wait for read labels and return concatenated data from warehouse.
    std::vector<std::byte> wait_read(std::span<PendingLabel> pending);

    /// Compute how many labels a given size produces.
    uint64_t label_count(uint64_t data_size) const;

private:
    ContentManager& content_;
    CatalogManager& catalog_;
    transport::NatsConnection& nats_;
    uint64_t max_label_size_;
    uint32_t app_id_;
};

} // namespace labios
```

- [ ] **Step 2: Write label_manager.cpp**

```cpp
#include <labios/label_manager.h>

#include <algorithm>
#include <stdexcept>

namespace labios {

LabelManager::LabelManager(ContentManager& content, CatalogManager& catalog,
                           transport::NatsConnection& nats,
                           uint64_t max_label_size, uint32_t app_id)
    : content_(content), catalog_(catalog), nats_(nats),
      max_label_size_(max_label_size), app_id_(app_id) {}

uint64_t LabelManager::label_count(uint64_t data_size) const {
    if (data_size == 0) return 0;
    return (data_size + max_label_size_ - 1) / max_label_size_;
}

std::vector<PendingLabel> LabelManager::publish_write(
    std::string_view filepath, uint64_t offset,
    std::span<const std::byte> data) {

    uint64_t remaining = data.size();
    uint64_t pos = 0;
    std::vector<PendingLabel> pending;

    while (remaining > 0) {
        uint64_t chunk_size = std::min(remaining, max_label_size_);
        auto chunk = data.subspan(pos, chunk_size);

        LabelData label;
        label.id = generate_label_id(app_id_);
        label.type = LabelType::Write;
        label.source = memory_ptr(chunk.data(), chunk_size);
        label.destination = file_path(filepath, offset + pos, chunk_size);
        label.app_id = app_id_;
        label.data_size = chunk_size;
        auto serialized = serialize_label(label);

        content_.stage(label.id, chunk);
        catalog_.create(label.id, app_id_, LabelType::Write);

        auto reply = nats_.request("labios.labels", serialized,
                                    std::chrono::milliseconds(30000));

        pending.push_back({label.id, std::move(reply.data)});

        pos += chunk_size;
        remaining -= chunk_size;
    }

    return pending;
}

std::vector<PendingLabel> LabelManager::publish_read(
    std::string_view filepath, uint64_t offset, uint64_t size) {

    uint64_t remaining = size;
    uint64_t pos = 0;
    std::vector<PendingLabel> pending;

    while (remaining > 0) {
        uint64_t chunk_size = std::min(remaining, max_label_size_);

        LabelData label;
        label.id = generate_label_id(app_id_);
        label.type = LabelType::Read;
        label.source = file_path(filepath, offset + pos, chunk_size);
        label.app_id = app_id_;
        label.data_size = chunk_size;
        auto serialized = serialize_label(label);

        catalog_.create(label.id, app_id_, LabelType::Read);

        auto reply = nats_.request("labios.labels", serialized,
                                    std::chrono::milliseconds(30000));

        pending.push_back({label.id, std::move(reply.data)});

        pos += chunk_size;
        remaining -= chunk_size;
    }

    return pending;
}

void LabelManager::wait(std::span<PendingLabel> pending) {
    for (auto& p : pending) {
        if (p.reply_data.empty()) continue;
        auto comp = deserialize_completion(p.reply_data);
        if (comp.status == CompletionStatus::Error) {
            throw std::runtime_error("label " + std::to_string(p.label_id)
                                     + " failed: " + comp.error);
        }
    }
}

std::vector<std::byte> LabelManager::wait_read(
    std::span<PendingLabel> pending) {

    std::vector<std::byte> result;
    for (auto& p : pending) {
        if (p.reply_data.empty()) continue;
        auto comp = deserialize_completion(p.reply_data);
        if (comp.status == CompletionStatus::Error) {
            throw std::runtime_error("read label " + std::to_string(p.label_id)
                                     + " failed: " + comp.error);
        }
        auto chunk = content_.retrieve(
            std::stoull(comp.data_key.substr(comp.data_key.rfind(':') + 1)));
        // Retrieve using the key directly
        auto chunk_data = nats_.connected() ?
            content_.retrieve(p.label_id) : std::vector<std::byte>{};

        // Actually, the completion has data_key. Use redis directly.
        // But we should retrieve via ContentManager.
        // The data_key is "labios:data:<label_id>", so label_id is p.label_id.
        auto data = content_.retrieve(p.label_id);
        result.insert(result.end(), data.begin(), data.end());
        content_.remove(p.label_id);
    }
    return result;
}

} // namespace labios
```

Wait, the wait_read has a problem. The completion's data_key contains the warehouse key. Let me simplify: the worker stages read data under the label_id, so we retrieve using label_id.

Let me rewrite wait_read more cleanly:

```cpp
std::vector<std::byte> LabelManager::wait_read(
    std::span<PendingLabel> pending) {

    std::vector<std::byte> result;
    for (auto& p : pending) {
        if (p.reply_data.empty()) continue;
        auto comp = deserialize_completion(p.reply_data);
        if (comp.status == CompletionStatus::Error) {
            throw std::runtime_error("read label " + std::to_string(p.label_id)
                                     + " failed: " + comp.error);
        }
        auto data = content_.retrieve(p.label_id);
        result.insert(result.end(), data.begin(), data.end());
        content_.remove(p.label_id);
    }
    return result;
}
```

- [ ] **Step 3: Write label_manager_test.cpp (splitting logic)**

```cpp
#include <catch2/catch_test_macros.hpp>
#include <labios/label_manager.h>
#include <labios/content_manager.h>
#include <labios/catalog_manager.h>
#include <labios/transport/nats.h>
#include <labios/transport/redis.h>

#include <cstdlib>
#include <numeric>

static std::string redis_host() {
    const char* h = std::getenv("LABIOS_REDIS_HOST");
    return (h && h[0]) ? h : "localhost";
}

static std::string nats_url() {
    const char* u = std::getenv("LABIOS_NATS_URL");
    return (u && u[0]) ? u : "nats://localhost:4222";
}

TEST_CASE("label_count computes correct split count", "[label_manager]") {
    labios::transport::RedisConnection redis(redis_host(), 6379);
    labios::transport::NatsConnection nats(nats_url());
    labios::ContentManager cm(redis, 4096, 0, labios::ReadPolicy::ReadThrough);
    labios::CatalogManager catalog(redis);
    labios::LabelManager lm(cm, catalog, nats, 1048576, 1); // 1MB max

    REQUIRE(lm.label_count(0) == 0);
    REQUIRE(lm.label_count(1) == 1);
    REQUIRE(lm.label_count(1048576) == 1);   // Exactly 1MB
    REQUIRE(lm.label_count(1048577) == 2);   // 1MB + 1 byte
    REQUIRE(lm.label_count(10485760) == 10); // 10MB
    REQUIRE(lm.label_count(500000) == 1);    // Below max
}

TEST_CASE("publish_write splits 2MB into 2 labels", "[label_manager]") {
    labios::transport::RedisConnection redis(redis_host(), 6379);
    labios::transport::NatsConnection nats(nats_url());
    labios::ContentManager cm(redis, 4096, 0, labios::ReadPolicy::ReadThrough);
    labios::CatalogManager catalog(redis);
    labios::LabelManager lm(cm, catalog, nats, 1048576, 1);

    std::vector<std::byte> data(2 * 1048576);
    std::iota(reinterpret_cast<uint8_t*>(data.data()),
              reinterpret_cast<uint8_t*>(data.data()) + data.size(),
              static_cast<uint8_t>(0));

    auto pending = lm.publish_write("/test/split_2mb.bin", 0, data);
    REQUIRE(pending.size() == 2);
    REQUIRE(pending[0].label_id != 0);
    REQUIRE(pending[1].label_id != 0);
    REQUIRE(pending[0].label_id != pending[1].label_id);

    // Both should have completed (NATS request-reply is synchronous)
    lm.wait(pending);
}

TEST_CASE("Split write then split read returns original data", "[label_manager]") {
    labios::transport::RedisConnection redis(redis_host(), 6379);
    labios::transport::NatsConnection nats(nats_url());
    labios::ContentManager cm(redis, 4096, 0, labios::ReadPolicy::ReadThrough);
    labios::CatalogManager catalog(redis);
    labios::LabelManager lm(cm, catalog, nats, 1048576, 1);

    // Write 3MB
    std::vector<std::byte> data(3 * 1048576);
    std::iota(reinterpret_cast<uint8_t*>(data.data()),
              reinterpret_cast<uint8_t*>(data.data()) + data.size(),
              static_cast<uint8_t>(0));

    auto write_pending = lm.publish_write("/test/split_3mb.bin", 0, data);
    REQUIRE(write_pending.size() == 3);
    lm.wait(write_pending);

    // Read 3MB back
    auto read_pending = lm.publish_read("/test/split_3mb.bin", 0, data.size());
    REQUIRE(read_pending.size() == 3);
    auto result = lm.wait_read(read_pending);

    REQUIRE(result.size() == data.size());
    REQUIRE(std::equal(result.begin(), result.end(), data.begin()));
}
```

- [ ] **Step 4: Update CMakeLists.txt**

In `src/labios/CMakeLists.txt`, add `label_manager.cpp` to the sources.

In `tests/CMakeLists.txt`, add:

```cmake
add_executable(labios-label-manager-test unit/label_manager_test.cpp)
target_link_libraries(labios-label-manager-test PRIVATE labios Catch2::Catch2WithMain)
catch_discover_tests(labios-label-manager-test TEST_PREFIX "smoke/")
```

- [ ] **Step 5: Build and test**

Run: `cmake --build build/dev -j$(nproc)`

Expected: Compiles. label_count test is pure logic (no NATS/Redis). The publish tests need live infrastructure.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "feat: LabelManager with 1-to-N label splitting"
```

---

### Task 5: Update Dispatcher for Write-Locality

Split writes send multiple labels for the same file. Without write-locality, chunks scatter across workers. The dispatcher should set the file location at assignment time so subsequent chunks of the same file go to the same worker.

**Files:**
- Modify: `src/services/labios-dispatcher.cpp`

- [ ] **Step 1: Set location at dispatch time for WRITE labels**

In the dispatcher's subscribe callback, after the solver assigns a WRITE label, add location tracking:

```cpp
            if (target_worker > 0) {
                // ... existing read-locality code ...
            } else {
                // For WRITE labels, check if this file already has a location.
                // If so, assign to the same worker (write-locality for split chunks).
                if (label.type == labios::LabelType::Write) {
                    auto* dst = std::get_if<labios::FilePath>(&label.destination);
                    if (dst) {
                        auto loc = catalog.get_location(dst->path);
                        if (loc.has_value()) {
                            target_worker = *loc;
                        }
                    }
                }

                if (target_worker > 0) {
                    // Write-locality: same file goes to same worker
                    catalog.set_worker(label.id, target_worker);
                    std::string subject = "labios.worker." + std::to_string(target_worker);
                    nats.publish(subject,
                                 std::span<const std::byte>(reserialized.data(), reserialized.size()));
                    nats.flush();

                    std::cout << "[" << timestamp() << "] dispatcher: label "
                              << label.id << " WRITE -> worker " << target_worker
                              << " (locality)\n" << std::flush;
                } else {
                    // First write to this file: use solver, set location
                    std::vector<std::vector<std::byte>> label_batch;
                    label_batch.push_back(std::move(reserialized));
                    auto assignments = solver.assign(std::move(label_batch), workers);

                    for (auto& [worker_id, assigned_labels] : assignments) {
                        catalog.set_worker(label.id, worker_id);

                        // Set location now so subsequent chunks go here too
                        if (label.type == labios::LabelType::Write) {
                            auto* dst = std::get_if<labios::FilePath>(&label.destination);
                            if (dst) {
                                catalog.set_location(dst->path, worker_id);
                            }
                        }

                        for (auto& payload : assigned_labels) {
                            std::string subject = "labios.worker." + std::to_string(worker_id);
                            nats.publish(subject,
                                         std::span<const std::byte>(payload.data(), payload.size()));
                        }
                        nats.flush();

                        std::cout << "[" << timestamp() << "] dispatcher: label "
                                  << label.id << " -> worker " << worker_id << "\n"
                                  << std::flush;
                    }
                }
            }
```

This restructures the dispatch logic:
1. READ labels: check location → direct assignment (already done)
2. WRITE labels with existing location: direct assignment (write-locality)
3. WRITE labels without location: solver assigns → set location immediately

- [ ] **Step 2: Build and verify existing tests pass**

Run: `cmake --build build/dev -j$(nproc)`

- [ ] **Step 3: Commit**

```bash
git add src/services/labios-dispatcher.cpp
git commit -m "feat: write-locality in dispatcher for split chunk co-location"
```

---

### Task 6: Session Class

**Files:**
- Create: `include/labios/session.h`
- Create: `src/labios/session.cpp`

- [ ] **Step 1: Write session.h**

```cpp
#pragma once

#include <labios/catalog_manager.h>
#include <labios/config.h>
#include <labios/content_manager.h>
#include <labios/label_manager.h>
#include <labios/transport/nats.h>
#include <labios/transport/redis.h>

#include <cstdint>
#include <memory>

namespace labios {

class Session {
public:
    explicit Session(const Config& cfg);
    ~Session();

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    LabelManager& label_manager();
    ContentManager& content_manager();
    CatalogManager& catalog_manager();
    transport::RedisConnection& redis();
    transport::NatsConnection& nats();
    const Config& config() const;
    uint32_t app_id() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace labios
```

- [ ] **Step 2: Write session.cpp**

```cpp
#include <labios/session.h>

#include <unistd.h>

namespace labios {

struct Session::Impl {
    Config cfg;
    transport::RedisConnection redis;
    transport::NatsConnection nats;
    ContentManager content;
    CatalogManager catalog;
    LabelManager labels;
    uint32_t app_id;

    explicit Impl(const Config& c)
        : cfg(c),
          redis(c.redis_host, c.redis_port),
          nats(c.nats_url),
          content(redis, c.label_min_size, c.cache_flush_interval_ms,
                  read_policy_from_string(c.cache_read_policy)),
          catalog(redis),
          labels(content, catalog, nats, c.label_max_size,
                 static_cast<uint32_t>(getpid())),
          app_id(static_cast<uint32_t>(getpid())) {}
};

Session::Session(const Config& cfg) : impl_(std::make_unique<Impl>(cfg)) {}
Session::~Session() = default;

LabelManager& Session::label_manager() { return impl_->labels; }
ContentManager& Session::content_manager() { return impl_->content; }
CatalogManager& Session::catalog_manager() { return impl_->catalog; }
transport::RedisConnection& Session::redis() { return impl_->redis; }
transport::NatsConnection& Session::nats() { return impl_->nats; }
const Config& Session::config() const { return impl_->cfg; }
uint32_t Session::app_id() const { return impl_->app_id; }

} // namespace labios
```

- [ ] **Step 3: Add to CMakeLists.txt, build**

In `src/labios/CMakeLists.txt`, add `session.cpp`.

Run: `cmake --build build/dev -j$(nproc)`

- [ ] **Step 4: Commit**

```bash
git add include/labios/session.h src/labios/session.cpp src/labios/CMakeLists.txt
git commit -m "feat: Session class owns connections and creates three managers"
```

---

### Task 7: Rewrite Client to Compose Managers

**Files:**
- Modify: `include/labios/client.h`
- Modify: `src/labios/client.cpp`

- [ ] **Step 1: Rewrite client.h**

```cpp
#pragma once

#include <labios/config.h>
#include <labios/label.h>
#include <labios/session.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
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

class Client {
public:
    explicit Client(const Config& cfg);
    ~Client();

    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    void write(std::string_view filepath, std::span<const std::byte> data,
               uint64_t offset = 0);
    std::vector<std::byte> read(std::string_view filepath, uint64_t offset,
                                uint64_t size);

    Session& session();

private:
    std::unique_ptr<Session> session_;
};

Client connect(const Config& cfg);

} // namespace labios
```

- [ ] **Step 2: Rewrite client.cpp**

```cpp
#include <labios/client.h>

#include <mutex>
#include <stdexcept>

namespace labios {

// ---------------------------------------------------------------------------
// Status::Impl (kept for backward compatibility with demo/tests)
// ---------------------------------------------------------------------------

struct Status::Impl {
    uint64_t label_id = 0;
    std::vector<std::byte> reply_data;
    bool completed = false;
    CompletionData completion;
    std::mutex mu;
};

void Status::wait() {
    std::lock_guard lock(impl_->mu);
    if (impl_->completed) return;
    impl_->completion = deserialize_completion(impl_->reply_data);
    impl_->completed = true;
}

bool Status::ready() const { return !impl_->reply_data.empty(); }

CompletionStatus Status::result() const {
    const_cast<Status*>(this)->wait();
    std::lock_guard lock(impl_->mu);
    return impl_->completion.status;
}

std::string Status::error() const {
    const_cast<Status*>(this)->wait();
    std::lock_guard lock(impl_->mu);
    return impl_->completion.error;
}

std::string Status::data_key() const {
    const_cast<Status*>(this)->wait();
    std::lock_guard lock(impl_->mu);
    return impl_->completion.data_key;
}

uint64_t Status::label_id() const { return impl_->label_id; }

// ---------------------------------------------------------------------------
// Client
// ---------------------------------------------------------------------------

Client::Client(const Config& cfg) : session_(std::make_unique<Session>(cfg)) {}
Client::~Client() = default;

void Client::write(std::string_view filepath, std::span<const std::byte> data,
                   uint64_t offset) {
    auto& cfg = session_->config();
    auto& label_mgr = session_->label_manager();
    auto& content_mgr = session_->content_manager();
    auto& catalog_mgr = session_->catalog_manager();

    if (data.size() < cfg.label_min_size) {
        // Small write → cache
        auto flush_regions = content_mgr.cache_write(
            -1, filepath, offset, data);  // fd=-1 for native API
        for (auto& region : flush_regions) {
            auto pending = label_mgr.publish_write(
                region.filepath, region.offset, region.data);
            label_mgr.wait(pending);
            catalog_mgr.track_write(region.filepath, region.offset,
                                     region.data.size());
        }
    } else {
        // Normal/large write → split into labels
        auto pending = label_mgr.publish_write(filepath, offset, data);
        label_mgr.wait(pending);
        catalog_mgr.track_write(filepath, offset, data.size());
    }
}

std::vector<std::byte> Client::read(std::string_view filepath,
                                    uint64_t offset, uint64_t size) {
    auto& label_mgr = session_->label_manager();
    auto& content_mgr = session_->content_manager();

    // Check cache first (read-through)
    auto cached = content_mgr.cache_read(-1, offset, size);
    if (cached.has_value() && cached->size() == size) {
        return *cached;
    }

    // Issue READ labels
    auto pending = label_mgr.publish_read(filepath, offset, size);
    return label_mgr.wait_read(pending);
}

Session& Client::session() { return *session_; }

Client connect(const Config& cfg) { return Client(cfg); }

} // namespace labios
```

- [ ] **Step 3: Update demo to use new Client API**

The demo (`src/services/labios-demo.cpp`) uses `client.write()` and `client.read()` which have the same signatures. No changes needed.

- [ ] **Step 4: Build and run all existing tests**

```bash
cmake --build build/dev -j$(nproc)
ctest --test-dir build/dev -R "unit/" --output-on-failure
```

Expected: All unit tests pass. The demo and integration tests need Docker.

- [ ] **Step 5: Commit**

```bash
git add include/labios/client.h src/labios/client.cpp
git commit -m "refactor: Client composes Session with three managers"
```

---

### Task 8: Update Worker + Docker Rebuild + Integration Tests

**Files:**
- Modify: `src/services/labios-worker.cpp`
- Modify: `src/labios/CMakeLists.txt`

- [ ] **Step 1: Update worker to use ContentManager and renamed CatalogManager**

The worker already uses ContentManager (from Task 3). Verify the includes are correct (`content_manager.h`, `catalog_manager.h`). The worker's Warehouse::data_key() calls should now be ContentManager::data_key(). Update if needed.

- [ ] **Step 2: Full rebuild and Docker test**

```bash
cmake --preset release && cmake --build build/release -j$(nproc)
docker compose build
docker compose down -v && docker compose up -d
docker compose logs test
```

Expected: Smoke tests pass. Then run data path and demo:

```bash
docker compose run --rm --entrypoint labios-data-path-test test
docker compose run --rm --entrypoint labios-demo test
```

Expected: All pass. Data verification OK. The demo uses 1MB writes which equal max_label_size, so no splitting occurs. This confirms backward compatibility.

- [ ] **Step 3: Commit**

```bash
git add -A
git commit -m "feat: Phase 1 complete, all existing tests pass with new client architecture"
```

---

### Task 9: Phase 1 Integration Tests for Splitting and Cache

**Files:**
- Modify: `tests/integration/data_path_test.cpp`

- [ ] **Step 1: Add split write/read test**

```cpp
TEST_CASE("Write 3MB (split into 3 labels) and read back", "[data_path]") {
    auto cfg = test_config();
    auto client = labios::connect(cfg);

    // 3MB of known data, split into 3 × 1MB labels
    std::vector<std::byte> data(3 * 1024 * 1024);
    std::iota(reinterpret_cast<uint8_t*>(data.data()),
              reinterpret_cast<uint8_t*>(data.data()) + data.size(),
              static_cast<uint8_t>(0));

    client.write("/test/split_3mb.bin", data);
    auto result = client.read("/test/split_3mb.bin", 0, data.size());

    REQUIRE(result.size() == data.size());
    REQUIRE(std::equal(result.begin(), result.end(), data.begin()));
}
```

- [ ] **Step 2: Docker rebuild and test**

```bash
cmake --preset release && cmake --build build/release -j$(nproc)
docker compose build
docker compose down -v && docker compose up -d
docker compose run --rm --entrypoint labios-data-path-test test
```

Expected: All data path tests pass, including the new 3MB split test.

- [ ] **Step 3: Commit**

```bash
git add tests/integration/data_path_test.cpp
git commit -m "test: integration test for 1-to-N label splitting (3MB → 3 labels)"
```

---

## Phase 2: POSIX Intercept

### Task 10: FdTable

**Files:**
- Create: `include/labios/adapter/fd_table.h`
- Create: `src/labios/adapter/fd_table.cpp`
- Create: `tests/unit/fd_table_test.cpp`

- [ ] **Step 1: Write fd_table.h**

```cpp
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace labios {

enum class ReadPolicy;  // Forward declaration

struct FileState {
    std::string filepath;
    uint64_t    offset = 0;
    int         open_flags = 0;
    bool        sync_mode = false;
    std::mutex  mu;
    std::atomic<int> ref_count{1};
};

class FdTable {
public:
    FdTable();
    ~FdTable();

    /// Allocate a real kernel fd via memfd_create, track it.
    int allocate(const std::string& filepath, int flags);

    /// Look up a fd. Returns nullptr if not tracked.
    FileState* get(int fd);

    /// Check if fd is tracked (lock-free fast path).
    bool is_labios_fd(int fd) const;

    /// Release a fd. Decrements ref_count. Returns true if last ref.
    bool release(int fd);

    /// Duplicate a fd (for dup/dup2). Both fds share the same FileState.
    int duplicate(int old_fd);

    /// Number of tracked fds.
    size_t size() const;

private:
    mutable std::shared_mutex mu_;
    std::unordered_map<int, std::shared_ptr<FileState>> table_;

    // Lock-free bitset for fast is_labios_fd checks
    static constexpr int BITSET_SIZE = 1024 * 1024;  // Supports fds up to 1M
    std::vector<std::atomic<bool>> bitset_;

    int (*real_close_)(int) = nullptr;
};

} // namespace labios
```

- [ ] **Step 2: Write fd_table.cpp**

```cpp
#include <labios/adapter/fd_table.h>

#include <dlfcn.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace labios {

FdTable::FdTable() : bitset_(BITSET_SIZE) {
    real_close_ = reinterpret_cast<int(*)(int)>(dlsym(RTLD_NEXT, "close"));
    if (!real_close_) {
        real_close_ = ::close;
    }
}

FdTable::~FdTable() = default;

int FdTable::allocate(const std::string& filepath, int flags) {
    int fd = memfd_create("labios", MFD_CLOEXEC);
    if (fd < 0) {
        // Fallback
        fd = open("/dev/null", O_RDWR | O_CLOEXEC);
        if (fd < 0) return -1;
    }

    auto state = std::make_shared<FileState>();
    state->filepath = filepath;
    state->open_flags = flags;
    state->sync_mode = (flags & O_SYNC) || (flags & O_DSYNC);

    {
        std::unique_lock lock(mu_);
        table_[fd] = state;
    }

    if (fd >= 0 && fd < BITSET_SIZE) {
        bitset_[fd].store(true, std::memory_order_release);
    }

    return fd;
}

FileState* FdTable::get(int fd) {
    std::shared_lock lock(mu_);
    auto it = table_.find(fd);
    if (it == table_.end()) return nullptr;
    return it->second.get();
}

bool FdTable::is_labios_fd(int fd) const {
    if (fd < 0 || fd >= BITSET_SIZE) return false;
    return bitset_[fd].load(std::memory_order_acquire);
}

bool FdTable::release(int fd) {
    std::shared_ptr<FileState> state;
    {
        std::unique_lock lock(mu_);
        auto it = table_.find(fd);
        if (it == table_.end()) return false;
        state = it->second;
        table_.erase(it);
    }

    if (fd >= 0 && fd < BITSET_SIZE) {
        bitset_[fd].store(false, std::memory_order_release);
    }

    // Close the underlying memfd
    if (real_close_) real_close_(fd);

    int remaining = state->ref_count.fetch_sub(1, std::memory_order_acq_rel);
    return remaining == 1;  // true if this was the last reference
}

int FdTable::duplicate(int old_fd) {
    std::shared_ptr<FileState> state;
    {
        std::shared_lock lock(mu_);
        auto it = table_.find(old_fd);
        if (it == table_.end()) return -1;
        state = it->second;
    }

    int new_fd = dup(old_fd);
    if (new_fd < 0) return -1;

    state->ref_count.fetch_add(1, std::memory_order_relaxed);

    {
        std::unique_lock lock(mu_);
        table_[new_fd] = state;
    }

    if (new_fd >= 0 && new_fd < BITSET_SIZE) {
        bitset_[new_fd].store(true, std::memory_order_release);
    }

    return new_fd;
}

size_t FdTable::size() const {
    std::shared_lock lock(mu_);
    return table_.size();
}

} // namespace labios
```

- [ ] **Step 3: Write fd_table_test.cpp**

```cpp
#include <catch2/catch_test_macros.hpp>
#include <labios/adapter/fd_table.h>

#include <thread>
#include <vector>

TEST_CASE("FdTable allocates valid fds", "[fd_table]") {
    labios::FdTable table;
    int fd = table.allocate("/test/file.bin", O_WRONLY | O_CREAT);
    REQUIRE(fd >= 0);
    REQUIRE(table.is_labios_fd(fd));
    REQUIRE(table.size() == 1);

    auto* state = table.get(fd);
    REQUIRE(state != nullptr);
    REQUIRE(state->filepath == "/test/file.bin");

    bool was_last = table.release(fd);
    REQUIRE(was_last);
    REQUIRE_FALSE(table.is_labios_fd(fd));
    REQUIRE(table.size() == 0);
}

TEST_CASE("FdTable duplicate shares state", "[fd_table]") {
    labios::FdTable table;
    int fd1 = table.allocate("/test/dup.bin", O_RDWR);
    REQUIRE(fd1 >= 0);

    int fd2 = table.duplicate(fd1);
    REQUIRE(fd2 >= 0);
    REQUIRE(fd2 != fd1);
    REQUIRE(table.is_labios_fd(fd2));
    REQUIRE(table.size() == 2);

    // Both point to same FileState
    auto* s1 = table.get(fd1);
    auto* s2 = table.get(fd2);
    REQUIRE(s1 == s2);

    // Release first fd: not last ref
    bool was_last = table.release(fd1);
    REQUIRE_FALSE(was_last);

    // Release second fd: last ref
    was_last = table.release(fd2);
    REQUIRE(was_last);
}

TEST_CASE("FdTable O_SYNC detection", "[fd_table]") {
    labios::FdTable table;
    int fd1 = table.allocate("/test/sync.bin", O_WRONLY | O_SYNC);
    auto* s1 = table.get(fd1);
    REQUIRE(s1->sync_mode == true);

    int fd2 = table.allocate("/test/async.bin", O_WRONLY);
    auto* s2 = table.get(fd2);
    REQUIRE(s2->sync_mode == false);

    table.release(fd1);
    table.release(fd2);
}

TEST_CASE("FdTable concurrent access", "[fd_table]") {
    labios::FdTable table;
    constexpr int num_threads = 8;
    constexpr int ops_per_thread = 100;

    std::vector<std::thread> threads;
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&table, t]() {
            for (int i = 0; i < ops_per_thread; ++i) {
                std::string path = "/test/t" + std::to_string(t)
                                 + "_" + std::to_string(i) + ".bin";
                int fd = table.allocate(path, O_WRONLY);
                REQUIRE(fd >= 0);
                REQUIRE(table.is_labios_fd(fd));
                table.release(fd);
            }
        });
    }
    for (auto& t : threads) t.join();

    REQUIRE(table.size() == 0);
}

TEST_CASE("is_labios_fd returns false for unknown fds", "[fd_table]") {
    labios::FdTable table;
    REQUIRE_FALSE(table.is_labios_fd(0));
    REQUIRE_FALSE(table.is_labios_fd(1));
    REQUIRE_FALSE(table.is_labios_fd(999));
    REQUIRE_FALSE(table.is_labios_fd(-1));
}
```

- [ ] **Step 4: Add to CMake, build, test**

In `src/labios/CMakeLists.txt`, add `adapter/fd_table.cpp` to sources. Also add `${CMAKE_DL_LIBS}` to link libraries (for dlsym).

In `tests/CMakeLists.txt`, add:

```cmake
add_executable(labios-fd-table-test unit/fd_table_test.cpp)
target_link_libraries(labios-fd-table-test PRIVATE labios Catch2::Catch2WithMain)
catch_discover_tests(labios-fd-table-test TEST_PREFIX "unit/")
```

Build and run: `cmake --build build/dev -j$(nproc) && ctest --test-dir build/dev -R "unit/.*fd" --output-on-failure`

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "feat: FdTable with memfd_create, dup support, lock-free bitset"
```

---

### Task 11: POSIXAdapter + Interceptor

**Files:**
- Create: `include/labios/adapter/adapter.h`
- Create: `include/labios/adapter/posix_adapter.h`
- Create: `src/labios/adapter/posix_adapter.cpp`
- Create: `src/drivers/posix_intercept.cpp`

- [ ] **Step 1: Write adapter.h (concept definition)**

```cpp
#pragma once

#include <concepts>
#include <cstddef>
#include <sys/types.h>
#include <sys/stat.h>

namespace labios {

template<typename T>
concept IOAdapter = requires(T a, const char* path, int fd, int flags,
                             mode_t mode, void* buf, size_t count,
                             off_t offset, struct stat* st) {
    { a.open(path, flags, mode) } -> std::same_as<int>;
    { a.close(fd) } -> std::same_as<int>;
    { a.write(fd, buf, count) } -> std::same_as<ssize_t>;
    { a.read(fd, buf, count) } -> std::same_as<ssize_t>;
    { a.lseek(fd, offset, flags) } -> std::same_as<off_t>;
    { a.stat(path, st) } -> std::same_as<int>;
    { a.fsync(fd) } -> std::same_as<int>;
};

} // namespace labios
```

- [ ] **Step 2: Write posix_adapter.h**

```cpp
#pragma once

#include <labios/adapter/fd_table.h>
#include <labios/session.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <vector>

namespace labios {

class POSIXAdapter {
public:
    POSIXAdapter(Session& session, FdTable& fd_table);

    int open(const char* path, int flags, mode_t mode);
    int close(int fd);
    ssize_t write(int fd, const void* buf, size_t count);
    ssize_t pwrite(int fd, const void* buf, size_t count, off_t offset);
    ssize_t read(int fd, void* buf, size_t count);
    ssize_t pread(int fd, void* buf, size_t count, off_t offset);
    off_t lseek(int fd, off_t offset, int whence);
    int fsync(int fd);
    int stat(const char* path, struct stat* st);
    int fstat(int fd, struct stat* st);
    int unlink(const char* path);
    int access(const char* path, int mode);
    int mkdir(const char* path, mode_t mode);
    int ftruncate(int fd, off_t length);

private:
    Session& session_;
    FdTable& fd_table_;

    ssize_t do_write(int fd, const void* buf, size_t count, off_t offset,
                     bool update_offset);
    ssize_t do_read(int fd, void* buf, size_t count, off_t offset,
                    bool update_offset);
    int populate_stat(std::string_view filepath, struct stat* st);
};

} // namespace labios
```

- [ ] **Step 3: Write posix_adapter.cpp**

This is the largest file. Core operations:

```cpp
#include <labios/adapter/posix_adapter.h>
#include <labios/content_manager.h>

#include <cerrno>
#include <cstring>
#include <fcntl.h>

namespace labios {

POSIXAdapter::POSIXAdapter(Session& session, FdTable& fd_table)
    : session_(session), fd_table_(fd_table) {}

int POSIXAdapter::open(const char* path, int flags, mode_t /*mode*/) {
    int fd = fd_table_.allocate(path, flags);
    if (fd < 0) {
        errno = ENOMEM;
        return -1;
    }
    session_.catalog_manager().track_open(path, flags);
    return fd;
}

int POSIXAdapter::close(int fd) {
    auto* state = fd_table_.get(fd);
    if (!state) { errno = EBADF; return -1; }

    try {
        // Flush cached data and publish as labels
        auto regions = session_.content_manager().flush(fd);
        for (auto& region : regions) {
            auto pending = session_.label_manager().publish_write(
                region.filepath, region.offset, region.data);
            session_.label_manager().wait(pending);
            session_.catalog_manager().track_write(
                region.filepath, region.offset, region.data.size());
        }
    } catch (...) {
        // Best effort flush on close
    }

    session_.content_manager().evict(fd);
    fd_table_.release(fd);
    return 0;
}

ssize_t POSIXAdapter::write(int fd, const void* buf, size_t count) {
    auto* state = fd_table_.get(fd);
    if (!state) { errno = EBADF; return -1; }
    std::lock_guard lock(state->mu);
    ssize_t result = do_write(fd, buf, count, state->offset, true);
    return result;
}

ssize_t POSIXAdapter::pwrite(int fd, const void* buf, size_t count, off_t offset) {
    auto* state = fd_table_.get(fd);
    if (!state) { errno = EBADF; return -1; }
    std::lock_guard lock(state->mu);
    return do_write(fd, buf, count, offset, false);
}

ssize_t POSIXAdapter::do_write(int fd, const void* buf, size_t count,
                                off_t offset, bool update_offset) {
    auto* state = fd_table_.get(fd);
    auto data = std::span<const std::byte>(
        static_cast<const std::byte*>(buf), count);
    auto& cfg = session_.config();
    auto& label_mgr = session_.label_manager();
    auto& content_mgr = session_.content_manager();
    auto& catalog_mgr = session_.catalog_manager();

    try {
        if (count < cfg.label_min_size) {
            auto flush_regions = content_mgr.cache_write(
                fd, state->filepath, offset, data);
            for (auto& region : flush_regions) {
                auto pending = label_mgr.publish_write(
                    region.filepath, region.offset, region.data);
                if (state->sync_mode) {
                    label_mgr.wait(pending);
                }
                catalog_mgr.track_write(region.filepath, region.offset,
                                         region.data.size());
            }
        } else {
            auto pending = label_mgr.publish_write(
                state->filepath, offset, data);
            if (state->sync_mode) {
                label_mgr.wait(pending);
            }
            catalog_mgr.track_write(state->filepath, offset, count);
        }

        if (update_offset) {
            state->offset = offset + count;
        }
        return static_cast<ssize_t>(count);
    } catch (const std::exception& e) {
        errno = EIO;
        return -1;
    }
}

ssize_t POSIXAdapter::read(int fd, void* buf, size_t count) {
    auto* state = fd_table_.get(fd);
    if (!state) { errno = EBADF; return -1; }
    std::lock_guard lock(state->mu);
    return do_read(fd, buf, count, state->offset, true);
}

ssize_t POSIXAdapter::pread(int fd, void* buf, size_t count, off_t offset) {
    auto* state = fd_table_.get(fd);
    if (!state) { errno = EBADF; return -1; }
    std::lock_guard lock(state->mu);
    return do_read(fd, buf, count, offset, false);
}

ssize_t POSIXAdapter::do_read(int fd, void* buf, size_t count,
                               off_t offset, bool update_offset) {
    auto* state = fd_table_.get(fd);
    auto& label_mgr = session_.label_manager();
    auto& content_mgr = session_.content_manager();

    try {
        // Check cache first (read-through)
        auto cached = content_mgr.cache_read(fd, offset, count);
        if (cached.has_value() && cached->size() == count) {
            std::memcpy(buf, cached->data(), count);
            if (update_offset) state->offset = offset + count;
            return static_cast<ssize_t>(count);
        }

        // Issue READ labels
        auto pending = label_mgr.publish_read(state->filepath, offset, count);
        auto data = label_mgr.wait_read(pending);

        size_t bytes = std::min(data.size(), count);
        std::memcpy(buf, data.data(), bytes);
        if (update_offset) state->offset = offset + bytes;
        return static_cast<ssize_t>(bytes);
    } catch (const std::exception& e) {
        errno = EIO;
        return -1;
    }
}

off_t POSIXAdapter::lseek(int fd, off_t offset, int whence) {
    auto* state = fd_table_.get(fd);
    if (!state) { errno = EBADF; return -1; }
    std::lock_guard lock(state->mu);

    switch (whence) {
        case SEEK_SET:
            state->offset = offset;
            break;
        case SEEK_CUR:
            state->offset += offset;
            break;
        case SEEK_END: {
            auto info = session_.catalog_manager().get_file_info(state->filepath);
            uint64_t file_size = info.has_value() ? info->size : 0;
            state->offset = file_size + offset;
            break;
        }
        default:
            errno = EINVAL;
            return -1;
    }
    return static_cast<off_t>(state->offset);
}

int POSIXAdapter::fsync(int fd) {
    auto* state = fd_table_.get(fd);
    if (!state) { errno = EBADF; return -1; }
    std::lock_guard lock(state->mu);

    try {
        auto regions = session_.content_manager().flush(fd);
        for (auto& region : regions) {
            auto pending = session_.label_manager().publish_write(
                region.filepath, region.offset, region.data);
            session_.label_manager().wait(pending);
            session_.catalog_manager().track_write(
                region.filepath, region.offset, region.data.size());
        }
    } catch (const std::exception&) {
        errno = EIO;
        return -1;
    }
    return 0;
}

int POSIXAdapter::populate_stat(std::string_view filepath, struct stat* st) {
    auto info = session_.catalog_manager().get_file_info(filepath);
    if (!info.has_value() || !info->exists) {
        errno = ENOENT;
        return -1;
    }
    std::memset(st, 0, sizeof(*st));
    st->st_mode = S_IFREG | 0644;
    st->st_size = static_cast<off_t>(info->size);
    st->st_mtim.tv_sec = static_cast<time_t>(info->mtime_ms / 1000);
    st->st_mtim.tv_nsec = static_cast<long>((info->mtime_ms % 1000) * 1000000);
    st->st_blksize = 4096;
    st->st_blocks = (info->size + 511) / 512;
    return 0;
}

int POSIXAdapter::stat(const char* path, struct stat* st) {
    return populate_stat(path, st);
}

int POSIXAdapter::fstat(int fd, struct stat* st) {
    auto* state = fd_table_.get(fd);
    if (!state) { errno = EBADF; return -1; }
    return populate_stat(state->filepath, st);
}

int POSIXAdapter::unlink(const char* path) {
    session_.catalog_manager().track_unlink(path);
    return 0;
}

int POSIXAdapter::access(const char* path, int /*mode*/) {
    auto info = session_.catalog_manager().get_file_info(path);
    if (!info.has_value() || !info->exists) {
        errno = ENOENT;
        return -1;
    }
    return 0;
}

int POSIXAdapter::mkdir(const char* /*path*/, mode_t /*mode*/) {
    // Directories are implicit in LABIOS (catalog tracks files by path)
    return 0;
}

int POSIXAdapter::ftruncate(int fd, off_t length) {
    auto* state = fd_table_.get(fd);
    if (!state) { errno = EBADF; return -1; }
    session_.catalog_manager().track_truncate(state->filepath, length);
    return 0;
}

} // namespace labios
```

- [ ] **Step 4: Write posix_intercept.cpp**

```cpp
#include <labios/adapter/fd_table.h>
#include <labios/adapter/posix_adapter.h>
#include <labios/config.h>
#include <labios/session.h>

#include <dlfcn.h>
#include <fcntl.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <memory>
#include <mutex>
#include <string_view>

namespace {

// Real libc function pointers
using open_fn     = int(*)(const char*, int, ...);
using close_fn    = int(*)(int);
using write_fn    = ssize_t(*)(int, const void*, size_t);
using read_fn     = ssize_t(*)(int, void*, size_t);
using pwrite_fn   = ssize_t(*)(int, const void*, size_t, off_t);
using pread_fn    = ssize_t(*)(int, void*, size_t, off_t);
using lseek_fn    = off_t(*)(int, off_t, int);
using fsync_fn    = int(*)(int);
using stat_fn     = int(*)(const char*, struct stat*);
using fstat_fn    = int(*)(int, struct stat*);
using lstat_fn    = int(*)(const char*, struct stat*);
using unlink_fn   = int(*)(const char*);
using access_fn   = int(*)(const char*, int);
using mkdir_fn    = int(*)(const char*, mode_t);
using ftruncate_fn = int(*)(int, off_t);

open_fn     real_open = nullptr;
close_fn    real_close = nullptr;
write_fn    real_write = nullptr;
read_fn     real_read = nullptr;
pwrite_fn   real_pwrite = nullptr;
pread_fn    real_pread = nullptr;
lseek_fn    real_lseek = nullptr;
fsync_fn    real_fsync = nullptr;
stat_fn     real_stat = nullptr;
fstat_fn    real_fstat = nullptr;
lstat_fn    real_lstat = nullptr;
unlink_fn   real_unlink = nullptr;
access_fn   real_access = nullptr;
mkdir_fn    real_mkdir = nullptr;
ftruncate_fn real_ftruncate = nullptr;

labios::Config g_config;
std::unique_ptr<labios::Session> g_session;
std::unique_ptr<labios::FdTable> g_fd_table;
std::unique_ptr<labios::POSIXAdapter> g_adapter;
std::once_flag g_symbols_flag;
std::once_flag g_session_flag;
bool g_initialized_symbols = false;

template<typename T>
T load_sym(const char* name) {
    return reinterpret_cast<T>(dlsym(RTLD_NEXT, name));
}

void init_symbols() {
    std::call_once(g_symbols_flag, []() {
        real_open     = load_sym<open_fn>("open");
        real_close    = load_sym<close_fn>("close");
        real_write    = load_sym<write_fn>("write");
        real_read     = load_sym<read_fn>("read");
        real_pwrite   = load_sym<pwrite_fn>("pwrite");
        real_pread    = load_sym<pread_fn>("pread");
        real_lseek    = load_sym<lseek_fn>("lseek");
        real_fsync    = load_sym<fsync_fn>("fsync");
        real_stat     = load_sym<stat_fn>("__xstat");  // Linux glibc
        real_fstat    = load_sym<fstat_fn>("__fxstat");
        real_lstat    = load_sym<lstat_fn>("__lxstat");
        real_unlink   = load_sym<unlink_fn>("unlink");
        real_access   = load_sym<access_fn>("access");
        real_mkdir    = load_sym<mkdir_fn>("mkdir");
        real_ftruncate = load_sym<ftruncate_fn>("ftruncate");

        // Fallback if glibc stat wrappers not found
        if (!real_stat) real_stat = load_sym<stat_fn>("stat");
        if (!real_fstat) real_fstat = load_sym<fstat_fn>("fstat");
        if (!real_lstat) real_lstat = load_sym<lstat_fn>("lstat");

        const char* config_path = std::getenv("LABIOS_CONFIG_PATH");
        g_config = labios::load_config(
            config_path ? config_path : "conf/labios.toml");

        g_fd_table = std::make_unique<labios::FdTable>();
        g_initialized_symbols = true;
    });
}

void init_session() {
    std::call_once(g_session_flag, []() {
        g_session = std::make_unique<labios::Session>(g_config);
        g_adapter = std::make_unique<labios::POSIXAdapter>(
            *g_session, *g_fd_table);
    });
}

bool is_labios_path(const char* path) {
    if (!path || !g_initialized_symbols) return false;
    std::string_view sv(path);
    for (auto& prefix : g_config.intercept_prefixes) {
        if (sv.starts_with(prefix)) return true;
    }
    return false;
}

bool is_labios_fd(int fd) {
    if (!g_fd_table) return false;
    return g_fd_table->is_labios_fd(fd);
}

} // anonymous namespace

// Constructor: load symbols and config early
__attribute__((constructor))
static void labios_intercept_init() {
    init_symbols();
}

// Destructor: flush and cleanup
__attribute__((destructor))
static void labios_intercept_fini() {
    g_adapter.reset();
    g_session.reset();
    g_fd_table.reset();
}

// --- Intercepted functions ---

extern "C" int open(const char* path, int flags, ...) {
    init_symbols();
    va_list ap;
    va_start(ap, flags);
    mode_t mode = (flags & O_CREAT) ? va_arg(ap, int) : 0;
    va_end(ap);

    if (is_labios_path(path)) {
        init_session();
        return g_adapter->open(path, flags, mode);
    }
    return real_open(path, flags, mode);
}

extern "C" int open64(const char* path, int flags, ...) {
    init_symbols();
    va_list ap;
    va_start(ap, flags);
    mode_t mode = (flags & O_CREAT) ? va_arg(ap, int) : 0;
    va_end(ap);

    if (is_labios_path(path)) {
        init_session();
        return g_adapter->open(path, flags, mode);
    }
    return real_open(path, flags | O_LARGEFILE, mode);
}

extern "C" int close(int fd) {
    init_symbols();
    if (is_labios_fd(fd)) {
        return g_adapter->close(fd);
    }
    return real_close(fd);
}

extern "C" ssize_t write(int fd, const void* buf, size_t count) {
    init_symbols();
    if (is_labios_fd(fd)) {
        return g_adapter->write(fd, buf, count);
    }
    return real_write(fd, buf, count);
}

extern "C" ssize_t read(int fd, void* buf, size_t count) {
    init_symbols();
    if (is_labios_fd(fd)) {
        return g_adapter->read(fd, buf, count);
    }
    return real_read(fd, buf, count);
}

extern "C" ssize_t pwrite(int fd, const void* buf, size_t count, off_t offset) {
    init_symbols();
    if (is_labios_fd(fd)) {
        return g_adapter->pwrite(fd, buf, count, offset);
    }
    return real_pwrite(fd, buf, count, offset);
}

extern "C" ssize_t pread(int fd, void* buf, size_t count, off_t offset) {
    init_symbols();
    if (is_labios_fd(fd)) {
        return g_adapter->pread(fd, buf, count, offset);
    }
    return real_pread(fd, buf, count, offset);
}

extern "C" off_t lseek(int fd, off_t offset, int whence) {
    init_symbols();
    if (is_labios_fd(fd)) {
        return g_adapter->lseek(fd, offset, whence);
    }
    return real_lseek(fd, offset, whence);
}

extern "C" int fsync(int fd) {
    init_symbols();
    if (is_labios_fd(fd)) {
        return g_adapter->fsync(fd);
    }
    return real_fsync(fd);
}

extern "C" int fdatasync(int fd) {
    init_symbols();
    if (is_labios_fd(fd)) {
        return g_adapter->fsync(fd);
    }
    return real_fsync(fd);
}

extern "C" int unlink(const char* path) {
    init_symbols();
    if (is_labios_path(path)) {
        init_session();
        return g_adapter->unlink(path);
    }
    return real_unlink(path);
}

extern "C" int access(const char* path, int mode) {
    init_symbols();
    if (is_labios_path(path)) {
        init_session();
        return g_adapter->access(path, mode);
    }
    return real_access(path, mode);
}

extern "C" int mkdir(const char* path, mode_t mode) {
    init_symbols();
    if (is_labios_path(path)) {
        init_session();
        return g_adapter->mkdir(path, mode);
    }
    return real_mkdir(path, mode);
}

extern "C" int ftruncate(int fd, off_t length) {
    init_symbols();
    if (is_labios_fd(fd)) {
        return g_adapter->ftruncate(fd, length);
    }
    return real_ftruncate(fd, length);
}
```

- [ ] **Step 5: Add to CMake**

In `src/labios/CMakeLists.txt`, add to sources:
```
adapter/fd_table.cpp
adapter/posix_adapter.cpp
```

Create a new CMakeLists section for the shared library at the bottom of the root `CMakeLists.txt` or in `src/drivers/CMakeLists.txt`:

```cmake
# POSIX intercept shared library
add_library(labios_intercept SHARED src/drivers/posix_intercept.cpp)
target_link_libraries(labios_intercept PRIVATE labios ${CMAKE_DL_LIBS})
set_target_properties(labios_intercept PROPERTIES
    OUTPUT_NAME "labios_intercept"
    PREFIX "lib"
)
```

- [ ] **Step 6: Build**

```bash
cmake --build build/dev -j$(nproc)
```

Expected: Compiles the intercept shared library alongside everything else.

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "feat: POSIXAdapter and LD_PRELOAD intercept with full POSIX data path"
```

---

### Task 12: Docker + Intercept Integration Tests

**Files:**
- Modify: `Dockerfile`
- Create: `tests/integration/intercept_test.cpp`

- [ ] **Step 1: Update Dockerfile to include intercept library**

In the test stage of `Dockerfile`, add:

```dockerfile
COPY --from=builder /src/build/release/lib/liblabios_intercept.so /usr/local/lib/
COPY --from=builder /src/build/release/tests/labios-intercept-test /usr/local/bin/
```

- [ ] **Step 2: Write intercept_test.cpp**

This test uses the POSIX intercept via `LD_PRELOAD` by forking a child process:

```cpp
#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <numeric>
#include <string>
#include <vector>

// These tests run INSIDE the LD_PRELOAD context.
// The test binary itself is launched with LD_PRELOAD=liblabios_intercept.so.

TEST_CASE("Intercept write and read back", "[intercept]") {
    constexpr size_t data_size = 64 * 1024;  // 64KB
    std::vector<uint8_t> write_buf(data_size);
    std::iota(write_buf.begin(), write_buf.end(), static_cast<uint8_t>(0));

    int fd = ::open("/labios/intercept_test.bin", O_WRONLY | O_CREAT, 0644);
    REQUIRE(fd >= 0);

    ssize_t written = ::write(fd, write_buf.data(), data_size);
    REQUIRE(written == static_cast<ssize_t>(data_size));
    REQUIRE(::close(fd) == 0);

    // Read back
    fd = ::open("/labios/intercept_test.bin", O_RDONLY);
    REQUIRE(fd >= 0);

    std::vector<uint8_t> read_buf(data_size);
    ssize_t bytes_read = ::read(fd, read_buf.data(), data_size);
    REQUIRE(bytes_read == static_cast<ssize_t>(data_size));
    REQUIRE(::close(fd) == 0);

    REQUIRE(write_buf == read_buf);
}

TEST_CASE("Intercept lseek and pwrite/pread", "[intercept]") {
    int fd = ::open("/labios/seek_test.bin", O_RDWR | O_CREAT, 0644);
    REQUIRE(fd >= 0);

    // Write 1KB at offset 0
    std::vector<uint8_t> data(1024, 0xAA);
    REQUIRE(::write(fd, data.data(), 1024) == 1024);

    // pwrite 1KB at offset 1024 (no seek)
    std::vector<uint8_t> data2(1024, 0xBB);
    REQUIRE(::pwrite(fd, data2.data(), 1024, 1024) == 1024);

    // Verify offset didn't change (should be 1024 from the write)
    off_t pos = ::lseek(fd, 0, SEEK_CUR);
    REQUIRE(pos == 1024);

    // pread from offset 1024
    std::vector<uint8_t> read_buf(1024);
    REQUIRE(::pread(fd, read_buf.data(), 1024, 1024) == 1024);
    REQUIRE(read_buf == data2);

    REQUIRE(::close(fd) == 0);
}

TEST_CASE("Non-LABIOS paths pass through", "[intercept]") {
    // /tmp is not a LABIOS prefix, should use real filesystem
    int fd = ::open("/tmp/labios_passthrough_test.txt", O_WRONLY | O_CREAT, 0644);
    REQUIRE(fd >= 0);

    const char* msg = "hello";
    REQUIRE(::write(fd, msg, 5) == 5);
    REQUIRE(::close(fd) == 0);

    // Verify with real read
    fd = ::open("/tmp/labios_passthrough_test.txt", O_RDONLY);
    REQUIRE(fd >= 0);
    char buf[5];
    REQUIRE(::read(fd, buf, 5) == 5);
    REQUIRE(std::memcmp(buf, msg, 5) == 0);
    REQUIRE(::close(fd) == 0);

    ::unlink("/tmp/labios_passthrough_test.txt");
}
```

- [ ] **Step 3: Add to CMake**

In `tests/CMakeLists.txt`:

```cmake
add_executable(labios-intercept-test integration/intercept_test.cpp)
target_link_libraries(labios-intercept-test PRIVATE Catch2::Catch2WithMain)
# Note: no link to labios library - the intercept is loaded via LD_PRELOAD
```

- [ ] **Step 4: Docker rebuild and test**

```bash
cmake --preset release && cmake --build build/release -j$(nproc)
docker compose build
docker compose down -v && docker compose up -d

# Run intercept tests with LD_PRELOAD
docker compose run --rm --entrypoint sh test -c \
  "LD_PRELOAD=/usr/local/lib/liblabios_intercept.so labios-intercept-test"
```

Expected: Intercept tests pass. Writes go through LABIOS, reads come back verified. Passthrough test uses real filesystem.

- [ ] **Step 5: Run the M1 demo with LD_PRELOAD + dd**

```bash
docker compose run --rm --entrypoint sh test -c \
  "LD_PRELOAD=/usr/local/lib/liblabios_intercept.so \
   dd if=/dev/zero of=/labios/dd_test.dat bs=1M count=10 && \
   dd if=/labios/dd_test.dat of=/dev/null bs=1M"
```

Expected: 10MB written through LABIOS via dd. Read back succeeds.

- [ ] **Step 6: Run all existing tests to verify no regression**

```bash
docker compose run --rm --entrypoint labios-data-path-test test
docker compose run --rm --entrypoint labios-demo test
```

Expected: All pass.

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "feat: POSIX intercept integration tests, dd demo via LD_PRELOAD"
```

---

## Final Verification Checklist

After all tasks are complete:

- [ ] `cmake --build build/dev -j$(nproc)` compiles cleanly
- [ ] `ctest --test-dir build/dev -R "unit/" --output-on-failure` passes all unit tests
- [ ] `docker compose build` succeeds
- [ ] `docker compose up -d && docker compose logs test` shows smoke tests pass
- [ ] `docker compose run --rm --entrypoint labios-data-path-test test` passes (including 3MB split test)
- [ ] `docker compose run --rm --entrypoint labios-demo test` passes with data verification
- [ ] Intercept tests pass with LD_PRELOAD
- [ ] `dd` writes through LABIOS via LD_PRELOAD
- [ ] Dispatcher logs show `(locality)` for both read and write locality routing
- [ ] No `labios:file:*` keys in Redis (old workaround stays gone)
- [ ] `labios:location:*` keys present for all written files
- [ ] `labios:filemeta:*` keys present for files opened via intercept
