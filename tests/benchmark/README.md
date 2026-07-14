# Agent-oriented benchmark taxonomy

These benchmarks intentionally distinguish control-plane mechanics from evidence of a distributed Label I/O path. A benchmark title describes the path it actually executes.

## Categories

1. **Control-plane microbenchmark** — label creation, serialization/deserialization, ID generation, URI parsing, and backend-registry lookup. These measure mechanics and overhead; microbenchmark throughput is **not** distributed Label I/O throughput.
2. **Component benchmark** — one local class or registry, including a workspace object in one process. These measure a component only and are not multi-agent or end-to-end.
3. **Live end-to-end Label I/O benchmark** — uses the public client API, publishes real labels, traverses dispatcher and worker services, waits for completion, verifies final data/state, and has a semantically fair direct baseline. Such a benchmark must be explicitly opt-in and must not use mocks.
4. **Characterization test** — records a deterministic current limitation or unsupported semantic. It is not a performance success and must not make normal CI fail merely to announce the limitation.

## Current files

### `bench_coding_agent_io.cpp`

Provides control-plane label encoding roundtrips, ID uniqueness, and a deterministic coding-agent trace model. The trace includes many small reads/config-like operations, small writes, medium logs, larger artifacts, and read-after-write markers, but only constructs labels and encodes them. It proves neither backend execution nor distributed coding-agent I/O value. No live scenario is included because the benchmark does not claim a direct baseline without a configured service and comparable durability semantics.

### `bench_multi_agent_collab.cpp`

Provides a Redis-backed **workspace component** correctness test and component benchmarks for put/get/ACL operations. Logical app IDs 1–10 are arguments to one `Workspace` object in one process; this is not multi-agent collaboration. No live multi-process scenario is claimed.

The public `Client` workspace methods are in `include/labios/client.h:96-114`, but they return pointers to the process-local workspace registry. `WorkspaceRegistry::create/get` stores objects in a process-local map (`src/labios/workspace.cpp:207-230`), and workspace ACL state is likewise held by that object. Consequently, an independently started producer and consumer cannot establish the same authoritative workspace identity/ACL through the current API. Adding a subprocess that directly addresses Redis would bypass LABIOS and is intentionally not done. A runtime change would need persistent cross-process workspace metadata/identity and a public connection protocol for independently authenticated clients.

### `bench_cross_backend_etl.cpp`

Provides URI parsing, URI roundtrip, and file backend-registry lookup control-plane measurements. It does not read source data, transform it, write a destination, wait for a worker, or verify a destination result; therefore it makes no cross-backend ETL claim.

The advertised API now has a worker-executed source URI → pipeline → destination URI path. The worker resolves a declared source through the backend registry when no bound warehouse materialization applies, executes the SDS pipeline, and writes the result to the destination backend. The live correctness evidence is the `[data_path][pipeline]` integration test; this benchmark remains a control-plane measurement and makes no ETL performance or benchmark claim. It does not use direct backend calls, Redis, mocks, or benchmark-local ETL as a substitute.

## Commands

Default correctness execution needs no Redis, NATS, or Docker for coding-agent and URI benchmarks:

```sh
cmake --preset dev
cmake --build build/dev --target labios-bench_coding_agent_io labios-bench_multi_agent_collab labios-bench_cross_backend_etl -j"$(nproc)"
./build/dev/tests/benchmark/labios-bench_coding_agent_io "[bench]~[!benchmark]"
./build/dev/tests/benchmark/labios-bench_multi_agent_collab "[bench]~[!benchmark]"
./build/dev/tests/benchmark/labios-bench_cross_backend_etl "[bench]~[!benchmark]"
```

The workspace component test is opt-in with `LABIOS_BENCH_REDIS=1` (this prevents a missing Redis endpoint from blocking default CI) and then skips informatively when its external Redis service is unavailable. Run benchmark cases explicitly with Catch2's benchmark option, for example:

```sh
./build/dev/tests/benchmark/labios-bench_coding_agent_io [!benchmark]
LABIOS_BENCH_REDIS=1 ./build/dev/tests/labios-bench_multi_agent_collab [!benchmark]
./build/dev/tests/benchmark/labios-bench_cross_backend_etl [!benchmark]
```

There is currently no opt-in live benchmark in these three files. A future live command must document the dispatcher/worker and external-backend topology and use an explicit gate such as `LABIOS_BENCH_LIVE=1`.

## Methodology and interpretation

Correctness is established before timed work. Inputs and the trace seed are deterministic; outputs of measured operations are returned or counted. Catch2 controls repeated samples and warmup. Submission latency and completion latency must be reported separately for any future live test. No hard-coded performance thresholds are used.

A direct baseline is valid only when it uses the same underlying external storage/service, payloads, and materially comparable durability semantics. A local filesystem baseline cannot fairly represent a remote LABIOS deployment merely because both produce bytes. Historical paper numbers are not current results.

Known blockers are recorded above rather than hidden with a fake implementation. These benchmarks currently provide neutral control-plane/component evidence and a negative finding about the missing end-to-end ETL and cross-process workspace semantics.
