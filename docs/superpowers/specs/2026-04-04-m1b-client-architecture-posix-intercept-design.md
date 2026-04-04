# M1b: Client Architecture Rebuild + POSIX Intercept

**Date:** 2026-04-04
**Scope:** Decompose monolithic Client into three managers (LabelManager, ContentManager, CatalogManager). Implement label splitting (1-to-N) and small-I/O cache (N-to-1). Build a production-grade POSIX intercept layer using the adapter pattern. Full POSIX data path coverage for application transparency.

**Paper reference:** Section 3.2.3 (Components: Label Manager, Content Manager, Catalog Manager), Section 2.2 (Label Granularity Control), Figure 2(a) (Client internal design).

---

## 1. Motivation

The current `client.cpp` is a monolithic class that mixes Label Manager, Content Manager, and Catalog Manager concerns. It creates one label per write (no splitting), has no small-I/O cache, and provides no POSIX intercept. The paper specifies three distinct client components with configurable label granularity and transparent I/O interception. M1b builds the correct client architecture and layers the POSIX intercept on top.

## 2. Phased Delivery

**Phase 1 — Client Architecture Rebuild:** Decompose Client into three formal manager classes. Implement label splitting and small-I/O cache. Rebuild the native API to compose the managers. All existing tests pass against the new architecture.

**Phase 2 — POSIX Intercept:** Build `liblabios_intercept.so` using the adapter pattern. Virtual fd table with real kernel fds. Full POSIX data path. Path prefix filtering.

Each phase is independently testable. Phase 1 validates the manager architecture with existing tests before Phase 2 adds LD_PRELOAD complexity.

## 3. Core Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                      Application                             │
│  (dd, CM1, HACC, Montage, HDF5, PyTorch, MPI programs)     │
└──────┬──────────────────┬──────────────────┬────────────────┘
       │ POSIX calls      │ stdio calls      │ MPI-IO (future)
┌──────▼──────────────────▼──────────────────▼────────────────┐
│                     Interceptor                              │
│  LD_PRELOAD + dlsym(RTLD_NEXT) for symbol overrides          │
│  Routing: is_labios_path() / is_labios_fd() → adapter        │
└──────┬──────────────────┬──────────────────┬────────────────┘
       │                  │                  │
┌──────▼──────┐  ┌───────▼──────┐  ┌────────▼───────────────┐
│POSIXAdapter │  │STDIOAdapter  │  │MPIIOAdapter (future)    │
│ fd lifecycle│  │ delegates to │  │ collective → batched    │
│ offset mgmt │  │ POSIXAdapter │  │ labels                  │
│ sync/async  │  │              │  │                         │
└──────┬──────┘  └──────┬───────┘  └────────┬────────────────┘
       │                │                   │
┌──────▼────────────────▼───────────────────▼─────────────────┐
│                      FdTable                                 │
│  memfd_create() for real kernel fds                          │
│  shared_mutex on table, per-fd mutex on FileState            │
│  dup/dup2 via reference counting                             │
│  Lock-free bitset for signal-safe is_labios_fd()             │
└───────────────────────────┬─────────────────────────────────┘
                            │
┌───────────────────────────▼─────────────────────────────────┐
│                       Session                                │
│  Owns: RedisConnection, NatsConnection, Config, app_id       │
│  Creates: LabelManager, ContentManager, CatalogManager       │
└──────────────┬────────────────┬─────────────────────────────┘
               │                │
  ┌────────────▼───┐  ┌────────▼────────┐  ┌─────────────────┐
  │  LabelManager  │  │ ContentManager  │  │ CatalogManager  │
  │  split/publish │  │ warehouse/cache │  │ metadata/locate  │
  └────────────────┘  └─────────────────┘  └─────────────────┘
```

**Session** owns transport connections (Redis, NATS) and configuration. Creates the three managers, passing shared connection references. One Session per process. The POSIX intercept creates its Session lazily on first LABIOS open().

**Native Client API** remains as a convenience layer that composes the three managers. The POSIX intercept composes the same managers plus the adapter's fd/offset state. Both entry points share identical label creation and data staging logic.

## 4. LabelManager

Responsible for building, splitting, and publishing labels.

### Interface

```cpp
class LabelManager {
public:
    LabelManager(ContentManager& content, CatalogManager& catalog,
                 transport::NatsConnection& nats, const Config& cfg);

    // Split data into labels of at most max_label_size, stage each chunk
    // in the warehouse, create catalog entries, publish to NATS.
    // Returns async handles for completion tracking.
    std::vector<PendingLabel> publish_write(
        std::string_view filepath, uint64_t offset,
        std::span<const std::byte> data);

    // Split read request if size > max_label_size.
    std::vector<PendingLabel> publish_read(
        std::string_view filepath, uint64_t offset, uint64_t size);

    // Block until all pending labels complete. Returns merged results.
    void wait(std::span<PendingLabel> pending);

    // Wait and collect read data from warehouse.
    std::vector<std::byte> wait_read(std::span<PendingLabel> pending);
};
```

### Splitting Logic

For a write of `data_size` bytes at `offset`:
1. Compute `num_labels = ceil(data_size / max_label_size)`
2. For each chunk `i` in `0..num_labels`:
   - `chunk_offset = offset + i * max_label_size`
   - `chunk_size = min(max_label_size, data_size - i * max_label_size)`
   - Build a Label with destination `FilePath{filepath, chunk_offset, chunk_size}`
   - Call `content_manager.stage(label_id, chunk_data)`
   - Call `catalog_manager.create(label_id, app_id, LabelType::Write)`
   - Publish serialized label to NATS
3. Return `PendingLabel` handles (one per chunk)

For a read of `size` bytes at `offset`:
1. Same splitting logic, but with `LabelType::Read` and source as `FilePath`
2. Each label requests `min(max_label_size, remaining)` bytes
3. After wait, retrieve each chunk from warehouse, concatenate

### Async Pipeline

`PendingLabel` holds:
- `label_id`: for warehouse retrieval
- `reply_data`: populated by NATS reply
- `completed`: atomic flag

For sync mode (O_SYNC), the write path calls `wait()` before returning. For async mode, pending labels accumulate in the FileState and are waited on at fsync/close.

## 5. ContentManager

Responsible for warehouse interaction and the small-I/O cache.

### Warehouse

Same as current `Warehouse` class, now a part of ContentManager:
- `stage(label_id, data)`: Store in Redis as `labios:data:<id>`
- `retrieve(label_id)`: Get from Redis
- `remove(label_id)`: Delete from Redis
- `exists(label_id)`: Check existence

### Small-I/O Cache

Per-fd in-memory cache for writes below `min_label_size`.

**Data structure:** Per-fd `std::map<uint64_t, std::vector<std::byte>>` mapping offset to data. Allows overlapping write detection and ordered iteration for flush.

**Operations:**

```cpp
struct FlushRegion {
    uint64_t offset;
    std::vector<std::byte> data;
};

class ContentManager {
public:
    // Cache a small write. Returns flush regions if threshold reached.
    std::vector<FlushRegion> cache_write(int fd, uint64_t offset,
                                          std::span<const std::byte> data);

    // Read from cache if read-through policy and data overlaps.
    // Returns nullopt if write-only policy or no overlap.
    std::optional<std::vector<std::byte>> cache_read(int fd, uint64_t offset,
                                                      uint64_t size);

    // Flush all cached data for an fd. Returns regions to publish.
    std::vector<FlushRegion> flush(int fd);

    // Flush all fds (for timer and shutdown).
    std::vector<std::pair<int, std::vector<FlushRegion>>> flush_all();

    // Remove cache state for a closed fd.
    void evict(int fd);
};
```

### Flush Triggers

1. **Size threshold:** When accumulated bytes for an fd reach `min_label_size`, `cache_write()` returns the flushed region immediately.
2. **close():** Orchestration layer calls `flush(fd)` and publishes resulting labels.
3. **fsync():** Same as close but without evicting cache state.
4. **Timer:** Background `std::jthread` calls `flush_all()` every `flush_interval_ms`. Acquires per-fd locks. Uses `stop_token` for shutdown.
5. **Read-triggered:** If read-through policy is active and a read overlaps cached data, the orchestration layer flushes the overlapping region before serving the read to ensure consistency.

### Read-Through vs Write-Only

Configurable per-file (defaulting to global config value). The native API allows setting the policy per `open()` call. The POSIX intercept uses the global default since POSIX `open()` has no mechanism to express this.

- **read-through:** `cache_read()` checks the cache first. If cached data overlaps the read range, returns it directly without creating READ labels. For partial overlap, returns cached portion; the caller issues READ labels for uncached ranges and merges.
- **write-only:** `cache_read()` always returns nullopt. Reads go through READ labels unconditionally. Simpler, faster reads at the cost of read-after-write consistency within the process.

## 6. CatalogManager

Extended from current implementation.

### Existing (unchanged)
- Label status lifecycle: Queued → Scheduled → Executing → Complete / Error
- Label-to-worker mapping: `set_worker(label_id, worker_id)`
- File-to-worker location: `set_location(filepath, worker_id)` / `get_location(filepath)`

### New: File Metadata for POSIX

Required for stat/fstat/lstat through the intercept.

```cpp
struct FileInfo {
    uint64_t size = 0;
    uint64_t mtime_ms = 0;  // Milliseconds since epoch
    bool exists = false;
};

// Track file metadata updates after writes.
void track_write(std::string_view filepath, uint64_t offset, uint64_t size);

// Mark file as existing (on open with O_CREAT).
void track_open(std::string_view filepath, int flags);

// Mark file as deleted.
void track_unlink(std::string_view filepath);

// Update file size (for ftruncate).
void track_truncate(std::string_view filepath, uint64_t new_size);

// Query file metadata.
std::optional<FileInfo> get_file_info(std::string_view filepath);
```

Stored in Redis as `labios:filemeta:<filepath>` hash with fields: `size`, `mtime`, `exists`.

`track_write()` updates size to `max(current_size, offset + write_size)` and sets `mtime` to now.

## 7. POSIX Intercept: Adapter and FdTable

### FdTable

Thread-safe mapping of fd numbers to FileState.

```cpp
struct FileState {
    std::string filepath;
    uint64_t    offset = 0;
    int         open_flags;
    bool        sync_mode;       // Derived from O_SYNC | O_DSYNC
    ReadPolicy  read_policy;     // From config, overridable per-file
    std::vector<PendingLabel> pending;
    std::atomic<int> ref_count{1};
    std::mutex  mu;
};
```

**Fd allocation via memfd_create():**
- `memfd_create("labios", MFD_CLOEXEC)` returns a fully real kernel fd
- Passes all validation: fcntl, dup, select, poll, epoll
- If `memfd_create` is unavailable, fallback to `open("/dev/null", O_RDWR)`
- The fd is tracked in FdTable; all intercepted operations are handled before reaching the kernel

**Thread safety:**
- `std::shared_mutex` on the table (read-heavy: checking `is_labios_fd`)
- Per-fd `std::mutex` on FileState (serializes operations on same fd)
- Lock-free bitset for O(1) signal-safe `is_labios_fd()` check

**dup/dup2 support:**
- `dup(old_fd)`: Create new kernel fd via `real_dup(old_fd)`, add to table pointing to same FileState, increment ref_count
- `dup2(old_fd, new_fd)`: If new_fd is LABIOS, close it first. Then dup and track.
- `close(fd)`: Decrement ref_count. Only the last close triggers flush + wait.

### POSIXAdapter

Translates POSIX semantics into LABIOS operations via the three managers.

| POSIX Call | POSIXAdapter Implementation |
|---|---|
| `open(path, flags, mode)` | If LABIOS path: allocate fd via memfd_create, create FileState, `catalog.track_open(path, flags)`. Return fd. |
| `close(fd)` | Decrement ref_count. On last close: flush cache (`content.flush(fd)`), publish flush labels (`label_mgr.publish_write()`), wait all pending labels (`label_mgr.wait()`), evict cache state (`content.evict(fd)`), remove from FdTable. |
| `write(fd, buf, count)` | If count < min_label_size: `content.cache_write()`, publish if flush returned. Else: `label_mgr.publish_write()`. Update offset. If O_SYNC: `label_mgr.wait()`. Return count. |
| `pwrite(fd, buf, count, off)` | Same as write but uses explicit offset. Does not update FileState.offset. |
| `read(fd, buf, count)` | If read-through: check `content.cache_read()` first. For uncached ranges: `label_mgr.publish_read()` + `label_mgr.wait_read()`. Update offset. |
| `pread(fd, buf, count, off)` | Same as read but uses explicit offset. Does not update FileState.offset. |
| `lseek(fd, offset, whence)` | Update FileState.offset. For SEEK_END: query `catalog.get_file_info()` for file size. |
| `fsync(fd)` / `fdatasync(fd)` | Flush cache, publish flush labels, wait all pending labels. |
| `stat(path, buf)` / `lstat` | `catalog.get_file_info(path)` → populate struct stat. |
| `fstat(fd, buf)` | Get filepath from FileState → `catalog.get_file_info()`. |
| `unlink(path)` | `catalog.track_unlink(path)`. Future: create DELETE label. |
| `mkdir(path, mode)` | Catalog directory tracking (metadata only). |
| `rmdir(path)` | Catalog directory removal. |
| `rename(old, new)` | Update catalog: remap file metadata and location from old path to new path. |
| `ftruncate(fd, length)` | `catalog.track_truncate(filepath, length)`. |
| `access(path, mode)` | `catalog.get_file_info(path)` → check existence. |

Plus 64-bit variants: `open64`, `stat64`, `fstat64`, `lstat64`, `lseek64`, `ftruncate64`.

### Interceptor (LD_PRELOAD)

```cpp
// posix_intercept.cpp
// Each function: check if LABIOS → route to adapter or real libc

extern "C" int open(const char* path, int flags, ...) {
    ensure_initialized();
    va_list ap;
    va_start(ap, flags);
    mode_t mode = (flags & O_CREAT) ? va_arg(ap, int) : 0;
    va_end(ap);

    if (is_labios_path(path)) {
        return adapter().open(path, flags, mode);
    }
    return real_open(path, flags, mode);
}

extern "C" ssize_t write(int fd, const void* buf, size_t count) {
    if (is_labios_fd(fd)) {
        return adapter().write(fd, buf, count);
    }
    return real_write(fd, buf, count);
}
// ... same pattern for all intercepted calls
```

**Initialization lifecycle:**
1. `__attribute__((constructor))`: Read config (TOML + env vars). Load prefix list. Resolve real libc symbols via `dlsym(RTLD_NEXT, ...)`. No connections yet.
2. First LABIOS `open()`: Create Session (connect Redis + NATS), create three managers, create POSIXAdapter. Start cache flush timer thread.
3. `__attribute__((destructor))`: Flush all caches, wait all pending labels, close connections, stop timer thread.

**Path filtering:**
- Configurable prefix list from TOML config + `LABIOS_INTERCEPT_PREFIXES` env var
- `is_labios_path(path)` checks `std::string_view(path).starts_with(prefix)` for each prefix
- Default prefix: `/labios`

## 8. Write Path Orchestration

The write path is orchestrated by the Client (native API) or POSIXAdapter (intercept), composing the three managers:

```
write(fd_or_filepath, data, offset):

  1. If data.size < min_label_size:
       flush_regions = content_manager.cache_write(fd, offset, data)
       for each region in flush_regions:
           pending += label_manager.publish_write(filepath, region.offset, region.data)
           catalog_manager.track_write(filepath, region.offset, region.data.size)
     Else:
       pending = label_manager.publish_write(filepath, offset, data)
       catalog_manager.track_write(filepath, offset, data.size)

  2. Track pending labels for this fd (async mode) or wait immediately (sync mode).

  3. Return bytes written (data.size, not pending label count).
```

The read path:

```
read(fd_or_filepath, offset, size):

  1. If read-through policy:
       cached = content_manager.cache_read(fd, offset, size)
       If cached covers full range: return cached data
       If partial: compute uncached ranges, issue READ labels for those

  2. pending = label_manager.publish_read(filepath, offset, size)
     result = label_manager.wait_read(pending)

  3. Merge cached + label results if partial cache hit.
     Return merged data.
```

## 9. Configuration

### Extended TOML Schema

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

[label]
min_size = "64KB"                    # N-to-1 aggregation threshold
max_size = "1MB"                     # 1-to-N splitting threshold

[cache]
flush_interval_ms = 500              # Timer flush (0 = disabled)
default_read_policy = "read-through" # "read-through" or "write-only"

[intercept]
prefixes = ["/labios"]               # Paths routed through LABIOS
```

### Environment Variable Overrides

| Variable | Override |
|---|---|
| `LABIOS_LABEL_MIN_SIZE` | `[label] min_size` |
| `LABIOS_LABEL_MAX_SIZE` | `[label] max_size` |
| `LABIOS_CACHE_FLUSH_MS` | `[cache] flush_interval_ms` |
| `LABIOS_CACHE_READ_POLICY` | `[cache] default_read_policy` |
| `LABIOS_INTERCEPT_PREFIXES` | `[intercept] prefixes` (comma-separated) |

## 10. Source Layout

```
include/labios/
  session.h              # Session: connection ownership, manager creation
  label_manager.h        # LabelManager: split, publish, wait
  content_manager.h      # ContentManager: warehouse + small-I/O cache
  catalog_manager.h      # CatalogManager: metadata + location + file info
  client.h               # Client: convenience API (rewritten)
  adapter/
    adapter.h            # IOAdapter concept
    posix_adapter.h      # POSIXAdapter
    fd_table.h           # FdTable with memfd_create
  label.h                # Unchanged
  config.h               # Extended

src/labios/
  session.cpp
  label_manager.cpp
  content_manager.cpp
  catalog_manager.cpp
  client.cpp             # Rewritten to compose managers
  adapter/
    posix_adapter.cpp
    fd_table.cpp

src/drivers/
  posix_intercept.cpp    # LD_PRELOAD symbol overrides

tests/
  unit/
    label_manager_test.cpp
    content_manager_test.cpp
    catalog_manager_test.cpp
    fd_table_test.cpp
    posix_adapter_test.cpp
  integration/
    manager_pipeline_test.cpp
    intercept_dd_test.cpp
    intercept_mixed_io_test.cpp
    intercept_cache_test.cpp
    intercept_split_test.cpp
```

### CMake Additions

- `liblabios.a`: Now includes session, label_manager, content_manager, catalog_manager, posix_adapter, fd_table
- `liblabios_intercept.so`: Shared library (SHARED) linking posix_intercept.cpp + liblabios.a. Installed to lib/.
- Docker: test image includes liblabios_intercept.so for LD_PRELOAD tests

## 11. Testing Strategy

### Phase 1: Manager Rebuild

| Test | Validates |
|---|---|
| `label_manager_test`: split 10MB / 1MB max → 10 labels | 1-to-N splitting |
| `label_manager_test`: split 1MB / 1MB max → 1 label | Exact boundary |
| `label_manager_test`: split 500KB / 1MB max → 1 label | Below max passthrough |
| `label_manager_test`: each label has correct offset + length | Offset arithmetic |
| `content_manager_test`: 64 × 1KB writes → flush at 64th (64KB = min_label_size) | Size-triggered flush |
| `content_manager_test`: explicit flush returns all cached data | Close/fsync flush |
| `content_manager_test`: read-through returns cached data | Read-after-write |
| `content_manager_test`: write-only cache_read returns nullopt | Policy enforcement |
| `catalog_manager_test`: track_write updates file size | File metadata |
| `catalog_manager_test`: get_file_info after unlink returns not-exists | Deletion tracking |
| `manager_pipeline_test`: write 100 labels of varying sizes, read back, verify | Full pipeline regression |
| Existing smoke + data_path tests pass unchanged | Architecture regression |

### Phase 2: POSIX Intercept

| Test | Validates |
|---|---|
| `fd_table_test`: allocate 1000 fds, no collisions | Fd allocation |
| `fd_table_test`: dup increments ref_count, close decrements | Reference counting |
| `fd_table_test`: 8 threads concurrent open/close/write | Thread safety |
| `intercept_dd_test`: `dd if=/dev/zero of=/labios/test.dat bs=1M count=100` | M1 demo |
| `intercept_dd_test`: `dd if=/labios/test.dat of=/dev/null bs=1M` + verify | Read-back |
| `intercept_mixed_io_test`: open, write, pwrite, read, pread, lseek, stat, fstat, fsync, unlink | Full call coverage |
| `intercept_cache_test`: 1000 × 1KB writes, verify aggregation in logs | Small-I/O cache |
| `intercept_split_test`: 10MB write, verify 10 labels in logs | Label splitting |
| Cross-test: write via intercept, read via native Client API | Shared architecture |

## 12. Decisions Log

| Decision | Choice | Rationale |
|---|---|---|
| Client decomposition | Three formal managers | Paper Figure 2(a), testability, independent evolution |
| Label granularity timing | Implement in M1b | Paper says it belongs in Label Manager, not dispatcher |
| Small-I/O cache | Implement in M1b | Paper says it belongs in Content Manager |
| Write semantics | O_SYNC selects sync; default async with sync-on-close | POSIX-correct, gives apps explicit control |
| Cache flush triggers | Size + close + fsync + timer + read-triggered | Maximum correctness, bounded staleness |
| Read-after-write consistency | Configurable: read-through or write-only per-file | Different workloads have different needs |
| POSIX call surface | Full data path (open through rename, +64-bit) | HDF5, MPI-IO, real application transparency |
| Path filtering | Configurable prefix list | Simple, fast, TOML + env var |
| Fd allocation | memfd_create() for real kernel fds | Passes all validation (fcntl, dup, select) |
| Thread safety | shared_mutex on table, per-fd mutex, lock-free bitset | Multi-threaded HPC apps |
| Intercept pattern | Adapter (POSIXAdapter, future MPI-IO, HDF5) | Paper's "Wrappers", extensible, testable |
