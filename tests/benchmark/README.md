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

### `bench_trace_guided_selection.cpp` (P10)

This is the opt-in category-3 experiment. It uses only the public `Client` API
for three deterministic profiles: 16 small hot metadata writes, four 1 MiB
sequential writes, and a 256 KiB source → `builtin://identity` → SQLite
pipeline. Every label is completion-waited and the destination is read back for
verification. The CSV records submission latency separately from completion
latency for every repetition.

The predeclared arms use the same three-worker Compose topology:

1. **baseline** — Round Robin, with no weight profile;
2. **ablation** — MinMax with `conf/profiles/trace_ablation.toml`, whose static
   weights match the informed profile but whose trace weights are zero; and
3. **informed** — MinMax with `conf/profiles/trace_guided.toml`.

The dispatcher enriches each scheduling snapshot with completed-label trace
features: dispatch-to-completion service proxy, in-flight queue-depth EWMA, and
per-scheme throughput EWMA. Failed, cancelled, zero-byte, duplicate,
out-of-order, and expired attempts cannot enter successful service or
throughput EWMAs. Trace use is controlled by the profile's trace weights, not
its filename. Alpha, cold-start value, queue anchor, size normalization,
minimum samples, and attempt TTL are all TOML parameters. MinMax deterministically
explores candidates below the minimum-sample threshold before using informed
scores. No worker is provisioned or decommissioned.

Run the reduced method-readiness check with:

```sh
tests/benchmark/run_trace_guided_dry_run.sh
```

The script rebuilds the profile-specific images and recreates volumes for every
arm. The benchmark verifies the active policy/profile through public
observability before running. The informed arm also requires nonzero trace
samples on at least two workers and a maximum observed service proxy at least
20% above the minimum; failure of either calibration gate invalidates the run.
Passing this reduced one-repetition check establishes method readiness only.

The rejected Prompt 08 attempt used
`run_trace_guided_full_experiment.sh`. It performs one untimed warmup and 20
timed repetitions per profile and arm. Each arm begins after `compose down -v`
and is rebuilt from the same tracked source. The three workers use fixed
execution-delay fixtures of 0, 20, and 60 ms; these are calibration controls,
not production-performance settings. Arm order is shuffled once with seed
2101. Within every arm, all six profile orders are rotated deterministically
across repetitions. Resources are unique per repetition. State is fresh at arm
start and intentionally accumulates within an arm so completion-derived trace
state can mature; there is no hidden cache flush between repetitions.

The measured interval for each label begins immediately after its public
asynchronous submission returns and ends when `Client::wait_any` observes its
terminal completion. Public submission call latency is recorded separately.
Read-back verification happens after the measured completion interval. Every
raw row contains label and resource identity, bytes, selected worker, trace
inputs captured at scheduling, park retries, terminal state, failure text, and
expected/observed digests. Any missing, failed, or unverified row invalidates
the arm. The informed arm additionally requires successful trace samples from
at least two workers and a maximum/minimum service-proxy separation of at least
1.20.

The current benchmark source additionally requires Completion observation
version 1 and records `actual_queue_delay_us` (queued admission to worker start),
`actual_service_time_us` (worker start to terminal completion), executor, and
attempt from the public `CompletionResult`. The scheduling-time EWMA columns
remain explicitly named `trace_*_input` because they are policy inputs, not
observed outcomes. This updated surface has hermetic codec/derivation evidence;
a live Compose validation was attempted after implementation but could not run
because the Docker daemon was unavailable.

Raw per-label median, nearest-rank p95, and mean distributions are descriptive.
The attempted analysis incorrectly treated the median completion latency within
each of the 20 stateful repetitions as an independent inferential unit. The
predeclared primary comparison was informed
versus static-weight-matched ablation for each profile: a two-sided
Mann–Whitney U test with tie and continuity corrections, Holm correction over
the three profiles at alpha 0.05, Cliff's delta, and a deterministic
10,000-resample bootstrap 95% interval for the median difference and ratio
(seed 2101). Baseline is contextual. A profile is favorable only when the
Holm-adjusted test is significant, the observed ratio is below one, and the
ratio interval is wholly below one; the symmetric condition is adverse.
Everything else would have been null or inconclusive. Those calculations are
invalid for this run because the repetition-independence assumption failed.

The rejected harness is gated against accidental use. It can be reproduced for
diagnostic inspection only with:

```sh
LABIOS_ALLOW_INVALID_P08_DIAGNOSTIC=1 \
  tests/benchmark/run_trace_guided_full_experiment.sh
```

The script records the base commit, tracked-diff hash, host and container
versions, rendered topology, profile files, raw CSVs, calibration snapshots,
logs, checksums, machine-readable analysis, and a concise result table beneath
`tests/benchmark/artifacts/`. A valid run's compact evidence bundle is reviewed
and tracked with Prompt 08; transient container layers remain untracked.

The attempted `p08-20260730T193949Z` run produced 1,260/1,260 verified measured
completions and passed the informed calibration gate, but it is **invalid as a
performance experiment**. Repetitions accumulated state within one arm and
showed strong serial dependence; untimed setup/read-back labels trained trace
state; the service/queue columns captured scheduling EWMAs rather than actual
per-label observations; and source provenance omitted then-untracked method
files. Those historical raw rows predate the Completion observation fields;
the current source does not retroactively repair them. `INVALID.md` records the
audit. The generated numerical analysis is
retained only to diagnose the rejected method and must not be cited as a null,
negative, or favorable performance result. Prompt 08 and WS3's performance exit
remain open. The reduced dry run remains method-readiness evidence only.

## Methodology and interpretation

Correctness is established before timed work. Inputs and the trace seed are deterministic; outputs of measured operations are returned or counted. Catch2 controls repeated samples and warmup. Submission latency and completion latency must be reported separately for any future live test. No hard-coded performance thresholds are used.

A direct baseline is valid only when it uses the same underlying external storage/service, payloads, and materially comparable durability semantics. A local filesystem baseline cannot fairly represent a remote LABIOS deployment merely because both produce bytes. Historical paper numbers are not current results.

Known blockers are recorded above rather than hidden with a fake implementation. These benchmarks currently provide neutral control-plane/component evidence and a negative finding about the missing end-to-end ETL and cross-process workspace semantics.
