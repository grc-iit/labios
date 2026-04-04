# LABIOS Paper Audit

## Scope

Audit target: current `codex/paper-audit-m1-optimization` worktree.

Reference documents:
- HPDC'19 paper markdown: `../labios/.planning/reference/original-paper/labios.md`
- `LABIOS-2.0.md`

Audited source sets:
- `include/`
- `src/`
- `tests/`
- `schemas/`

This audit treats the HPDC'19 paper as the architectural specification and `LABIOS-2.0.md` as the modernization roadmap. Findings below distinguish between M1-acceptable forward-compatibility choices and true correctness gaps.

## What Matches The Paper

- The codebase has the paper's three client-side roles: `LabelManager`, `ContentManager`, and `CatalogManager`.
- The instruction/data/metadata split is structurally present:
  - instructions flow through NATS subjects
  - staged payload data flow through Redis warehouse keys
  - catalog metadata flow through Redis hash entries
- The label model supports the paper's flexible pointer concept through a typed union:
  - memory pointer
  - file path + offset/length
  - network endpoint
- The worker executes labels by reading from the warehouse for WRITE, reading local storage for READ, and updating catalog state around execution.
- The FlatBuffers schema is complete for the current M1 path and preserves the major label fields and pointer variants needed by the implementation.
- The dispatcher and worker are separated services, and the solver abstraction exists with a round-robin implementation.
- The POSIX intercept path reflects the paper's compatibility goal: legacy I/O calls are intercepted and translated into labels.

## What Deviates From The Paper

- The label is not paper-pure. It carries 2.0/M7-era extension fields such as `intent`, `ttl_seconds`, `isolation`, plus the transport-specific `reply_to` field. These are not part of the HPDC'19 label abstraction.
- `operation` is represented as a string, not as a function pointer or shared-program reference with executable SDS semantics. This is an M5 gap.
- The dispatcher does not implement the paper's two explicit phases:
  - no label shuffling
  - no aggregation
  - no dependency detection
  - no supertask creation
- Only round-robin scheduling is implemented. Random, constraint-based, and MinMax are absent.
- The worker-manager portion of the paper architecture is largely stubbed:
  - `labios-manager` is only a readiness service
  - no worker registry
  - no worker scores
  - no bucket sorting
  - no commission/decommission
- The current M1 transport path still uses NATS request/reply from client to dispatcher instead of a pure fire-and-forget queue plus independent completion path. Architecturally, labels still traverse the dispatcher, but this is tighter coupling than Figure 1(b).
- The catalog only partially models the paper's inventory responsibilities. It tracks status, worker assignment, some file metadata, and filepath location mapping, but it does not yet model richer label attributes, distribution views, or location mappings for generalized internal object layouts.
- The software-defined storage path is not present.
- The four deployment models and storage malleability features are not present.
- Test registration is incomplete for audit-sensitive code:
  - `tests/unit/catalog_manager_test.cpp`
  - `tests/unit/content_manager_test.cpp`
  - `tests/unit/label_manager_test.cpp`
  are currently registered under `smoke/` prefixes in `tests/CMakeLists.txt`, so the requested `ctest -R unit/` slice does not exercise them.

## What Was Fixed

### 1. Label/catalog metadata tightened

Files:
- `include/labios/catalog_manager.h`
- `src/labios/catalog_manager.cpp`
- `src/labios/label_manager.cpp`
- `tests/unit/catalog_manager_test.cpp`

Changes:
- `LabelManager` now stamps primitive operations explicitly:
  - WRITE labels use `operation = "write"`
  - READ labels use `operation = "read"`
- Labels are now created with `Queued` flags instead of leaving the label-state bitfield unused.
- READ labels now populate a destination pointer shape (`MemoryPtr{0, size}`) instead of leaving the destination conceptually absent.
- `CatalogManager` now persists additional label attributes needed by the paper's inventory model:
  - flags
  - priority
  - operation
- Added catalog APIs for reading/updating flags and storing worker error text.

### 2. Client-side read-after-write correctness fixed

Files:
- `src/labios/adapter/posix_adapter.cpp`
- `src/drivers/posix_intercept.cpp`
- `src/labios/catalog_manager.cpp`
- `tests/unit/catalog_manager_test.cpp`

Changes:
- If a read cannot be fully satisfied from the small-I/O cache, the adapter now flushes cached writes first so READ labels observe the latest data instead of stale storage contents.
- Intercept shutdown flushes now also update catalog file metadata, preventing persisted small writes from being invisible to the catalog on process teardown.
- `track_open(..., O_TRUNC)` now resets file size immediately.
- `track_truncate()` now marks the file as existing and refreshes mtime, which better matches POSIX expectations.

### 3. Dispatcher/worker ownership corrected

Files:
- `src/services/labios-dispatcher.cpp`
- `src/services/labios-worker.cpp`

Changes:
- The dispatcher no longer writes persistent file-location mappings at assignment time. That mapping is now left to the worker after a successful WRITE, which better matches the paper's worker-driven completion/update flow.
- The dispatcher now advances label flags to `Scheduled`.
- The worker now advances label flags to `Pending` on execution.
- Worker failures now publish an error completion reply when possible, instead of silently relying on a later client-side request timeout.
- Worker failures now store error text in the catalog.

## Remaining Gaps For M2+

- Implement the full dispatcher split:
  - batching window
  - shuffler
  - aggregation
  - dependency detection
  - supertasks
  - direct read-local routing as an explicit shuffler output
- Restore the paper's missing scheduling policies:
  - random
  - constraint-based
  - MinMax DP
- Implement worker scoring and a real worker manager:
  - availability
  - capacity
  - load
  - speed
  - energy
  - configurable Table 2 weight profiles
  - bucket-sorted worker list
- Separate request publication from completion tracking more cleanly so the instruction path behaves like the paper's distributed queue instead of a synchronous request/reply shortcut.
- Extend catalog modeling beyond filepath-level mappings to the paper's fuller inventory semantics.
- Add delete/flush path completeness through workers, not just WRITE/READ.
- Restore SDS semantics with executable operation references from a shared program repository.
- Add commission/decommission and idle suspension logic.
- Add deployment-model configurability and storage-bridging backends.
