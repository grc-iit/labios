# LABIOS 2.1

**Status:** Living design and planning authority
**Updated:** 2026-07-15
**Project:** LABIOS — Label-Based I/O System
**Provenance:** US Patent 11,630,834 B2 · NSF Award 2313154

This is the single source of truth for the direction of LABIOS 2.1. It
supersedes the previous constitutional, specification, engineering-plan, agent
integration, and analysis documents as planning authorities. Those documents
are historical inputs and will be archived after current concurrent work is
integrated.

This document contains both the engineering truth baseline and the forward
direction. Public documentation may summarize it, but no second planning file
overrides it.

## 1. Thesis

LABIOS's primary contribution is **Label I/O**: a declarative intermediate
representation for I/O programs.

Frontends translate I/O intent into labels. The LABIOS runtime resolves,
optimizes, schedules, and executes those labels. As a label flows through the
runtime, it accumulates the decisions and outcome of its execution.

The runtime is a reference implementation and research proof of concept for the
Label I/O abstraction. The runtime is not, by itself, the product thesis.

Label I/O is particularly valuable for agents because agent-generated I/O is
dynamic, asynchronous, intent-rich, mixed in granularity, and often shared with
other agents. Scientific and traditional applications remain equally valid
label producers.

## 2. Boundary

LABIOS is:

- An I/O intermediate representation.
- A runtime for interpreting and optimizing that representation.
- A way to decouple I/O production from execution and storage mechanics.
- A way to carry intent, constraints, dataflow, and execution history with I/O.
- A research platform for asynchronous, composable, and resource-aware I/O.

LABIOS is not:

- A storage system that owns users' external data services.
- A generic compute-task runtime.
- A generic external-effects runtime.
- A replacement for MCP, OpenAPI, or provider tool-call protocols.
- A universal shell or arbitrary tool-execution broker.
- A requirement that every agent tool become a label.

An agent tool maps into LABIOS when its I/O semantics can be expressed as Label
I/O. File reads, writes, artifact movement, database access, data transformation,
and backend routing fit. Arbitrary computation with unknown I/O does not become
Label I/O merely because an agent invoked it.

## 3. The Label I/O IR

A label is:

> **a sealed I/O program plus a mutable runtime residual**

The sealed program is what the producer declared. The residual is everything
the runtime writes onto the label while executing it: resolution, routing,
scheduling, timing, and result.

### 3.1 Sealed I/O program

The producer defines:

- I/O operation.
- Source and destination resources.
- Data or data reference.
- Intent and priority.
- Isolation and durability requirements.
- Completion behavior.
- Optional transformation pipeline and dependencies.

These fields preserve the producer's intent and are sealed after submission.

### 3.2 Mutable runtime residual

Runtime components may add:

- Namespace and location resolution.
- Dependency and hazard information.
- Aggregation decisions.
- Routing and worker/pool selection.
- Scheduling policy and score snapshots.
- Lifecycle timestamps and component hops.
- Execution result and error information.

A completed label is both the executed I/O program and the record of how the
runtime interpreted it.

### 3.3 Compiler/runtime model

```text
POSIX · SDKs · Python · scientific APIs · agent I/O APIs
                         |
                         v
                   Label I/O IR
                         |
       resolution · shuffling · pipeline planning
              scheduling · pool selection
                         |
                         v
              workers and backend adapters
```

In this model:

- Adapters are frontends.
- The label is the IR.
- The catalog and URI resolver act like a symbol-resolution layer.
- The shuffler and pipeline planner are optimization passes.
- The scheduler selects an execution target.
- Workers interpret the I/O program.
- Backend adapters are last-mile targets.
- A completed label is the execution record.

### 3.4 Typed resources

The canonical semantic resource model is an expanded typed `ResourceRef` union.
URI strings remain an ergonomic frontend and interchange syntax, but ingress
normalizes them into typed resources before optimization and scheduling. For
example, `file:///data/in.csv` and a `vector://` collection query both
normalize into typed resources, so one validation and optimization layer
reasons over them uniformly.

The 2.1 semantic families are file/range, memory, network, key-value,
relational, object, vector, graph, channel, and workspace resources. Each family
defines the identity, addressing, version, and operation properties needed for
validation and optimization. A namespaced extension resource remains available
for future backend families without converting the whole IR to untyped strings.

Legacy `Pointer`, `source_uri`, and `dest_uri` inputs are compatibility forms,
not permanently co-equal representations. The 2.1 implementation will normalize
them at ingress and migrate internal code incrementally.

`ir_version` is distinct from a resource/data `version`: the former versions IR
semantics; the latter participates in resource consistency and optimistic
coordination.

## 4. Architectural invariants

1. Clients do not bypass the dispatcher to reach workers.
2. Internal plumbing and external backends remain separate.
3. External backends remain authoritative for their data and namespaces.
4. Backends are thin last-mile adapters; runtime intelligence stays upstream.
5. Origin fields are sealed; runtime accumulation and state fields are mutable.
6. I/O production and execution may be asynchronous.
7. Optimizations must preserve the declared I/O semantics.
8. Component existence is not evidence of end-to-end behavior.
9. Correctness and reproducible evidence precede performance claims.
10. New surface area is added only when it tests an important Label I/O property.

## 5. Research properties carried forward

Historical technologies are inputs, not mandatory dependencies or checklists.
The left column names prior LABIOS-lab systems and mechanisms; a new reader
needs only the right column. The left column is provenance. The useful
intellectual properties are:

| Historical mechanism | Property retained in LABIOS 2.1 |
|---|---|
| POSIX, HDF5, netCDF, MPI-IO adapters | Different I/O dialects can compile into one IR while preserving semantics. |
| HCL shadow links | External namespaces can be resolved lazily without copying or owning all metadata. |
| Asynchronous labels | Submission, execution, and completion are separate, well-defined phases. |
| Ephemeral warehouse regions | Producers and consumers can exchange transient data without forcing it through final storage. |
| Luxio | Observed I/O behavior can guide resource and pool selection. |
| Worker scores | Labels can be matched to heterogeneous workers using requirements, resources, locality, and policy. |
| Elastic worker management | Execution pools can adapt to available allocations and workload demand. |
| Active-storage operations | Transformations can be declared with I/O and executed near data. |
| LabStor composition | The I/O execution stack can be assembled from composable operations and adapters. |

HCL, Luxio, and LabStor are not required implementation artifacts for 2.1. Their
ideas may be re-expressed using the current C++20, FlatBuffers, NATS, DragonflyDB,
and backend architecture.

## 6. Current engineering posture

LABIOS 2.0 produced a broad executable prototype. It contains most major
architectural components, but component breadth is ahead of end-to-end semantic
depth.

LABIOS 2.1 will prioritize:

- Truthful implementation status.
- Complete execution paths over additional adapters.
- Explicit Label I/O semantics.
- Cross-process behavior over process-local demonstrations.
- Real scheduling inputs over nominal policy selection.
- Correctness benchmarks before speedup claims.
- Small, reviewable implementation slices.

### 6.1 Verified baseline

Baseline audited at commit `d8e6a4d`; P09 implementation evidence updated 2026-07-14:

| Area | Engineering truth |
|---|---|
| Label model | FlatBuffers labels carry origin, accumulation, lifecycle, routing, pipeline, continuation, and result fields. Labels now carry append-only IR versioning, typed ResourceRef addresses, staged-input bindings, declared dependency lowering, and Order/Barrier hazards; ingress normalizes URI/Pointer compatibility forms into IR 2.1 while preserving legacy mirrors. Full sealed-origin enforcement remains future work. |
| Transport | JetStream is now the required label transport: the runtime creates the LABIOS_LABELS stream, uses durable explicit-ack consumers, bounded redelivery, and publication acknowledgements. Core NATS remains for control-plane request/reply traffic. |
| Dispatcher | One complete post-shuffler batch now becomes typed scheduling units, reaches one policy invocation, receives one validated ordered decision per unit, and persists residual/lifecycle state before delivery. Direct-route, aggregate, Composite, independent, ready, and blocked units use the shared feasibility gate; parked decisions are re-enqueued through the P05 path. |
| Worker registry | Registry v2 uses verified FlatBuffers envelopes with `LWR2`, structured version-paired capabilities/attachments, absolute and normalized capacity, immutable generation snapshots, and epoch-safe deregistration. Workers derive file, SQLite, and optional external-KV attachments from their constructed `BackendRegistry`; Tier 0 omits pipeline operations, unwired Delete/Flush are not advertised, equal-epoch heartbeats cannot mutate static capability state, and periodic/on-demand full registration repairs manager state loss. Manager, worker, and dispatcher runtime subjects have no CSV/text fallback. |
| Placement evidence | The six legacy scalar slots remain unchanged while versioned append-only decision history records ordered attempts, candidates, exclusion reasons, residual capacity, locality, exact raw/normalized score components, typed per-policy evidence, and assigned/parked outcomes. The same Label wire format now carries typed replay context for the captured scheduling unit, worker snapshot, and effective policy profile; the legacy policy string is only a compatibility/debug mirror. History serializes even with all-zero legacy scalars. |
| Shuffler | Aggregation and RAW/WAW/WAR analysis work for batch-local `FilePath` ranges. URI resources and cross-batch hazards are not modeled equivalently. |
| Backends | File and SQLite adapters are implemented; user-Redis KV is optional. Internal DragonflyDB remains plumbing, not a user backend. |
| Pipelines | The repository and eleven byte-level builtins work as components. P04 completes the worker source URI → transform → destination URI path with declared-source versus bound-warehouse precedence; one worker executes the complete pipeline. |
| Worker tiers | Tier metadata, scoring, and pipeline rejection by Tier 0 exist. All default Compose workers are now Tier 1, so pipeline labels are admitted without a dispatcher batch-loop gate; Tier 2 still does not implement reasoning behavior. |
| Channels/workspaces | Public C++ calls now return owning `ChannelHandle`/`WorkspaceHandle` values rather than registry-owned raw pointers; handles share required session/object lifetime and fail predictably when empty or destroyed. Registries, identity, ACL state, and channel sequence state remain process-local, and no independent multi-process coordination is claimed. |
| Completion API | Prompt 09 replaces mutable public `PendingIO` internals with an owning, copyable `Operation` over immutable label IDs and shared session context. Operation observation/wait/cancel/read methods are concurrent-safe, survive originating `Client` destruction, preserve handles after typed nonterminal timeout, deterministically select wait-any, return pending views from timed-out wait-all, expose categorized errors and cancellation race outcomes, and retain read results for repeat retrieval. `PendingIO` remains only a compatibility spelling. The C API now uses registry-validated owning status records, stable error codes/categories, explicit result/buffer/error release functions, and stale/double-release protection. Catalog compare-and-set transitions continue to linearize cancellation, scheduling, and worker claim. |
| Elasticity | Decision engines and Docker orchestration abstractions exist. Elasticity is disabled by default; leader election is not implemented. |
| POSIX | The intercept is a useful `/labios` virtual-path prototype, not complete transparent POSIX behavior for arbitrary repositories. |
| Python | Prompt 10 binds the Prompt 09 owning `Operation`, typed labels/resources, label-level and URI submission, intent/priority, pipelines, completion/cancellation/lifecycle results, repeatable reads, placement history, and public Observe queries. Blocking native calls release the GIL. Versioned generated FlatBuffers bindings and one verified `LWR2` registry-v2 parser live in the Python package; Prompt 11 consumes this surface for MCP core Label I/O. |
| MCP | Prompt 11 makes the five-name server a public Label I/O frontend: store/retrieve/process lower typed resources and structured registered pipelines through the Python Client and owning Operation, dispatcher, workers, and external backends. Health, workers, active profile, lifecycle, and placement use public Observe/inspection APIs. Direct Redis/NATS/worker-volume core paths and CSV fallback are removed; workspace knowledge is an explicit Prompt-14 placeholder. |
| Configuration | `Client::set_config` changes local client configuration, not distributed runtime state. Public configuration docs list only implemented TOML keys and environment overrides. |
| Serialization safety | Label and Completion deserialization now verifies FlatBuffers buffers before access, rejects malformed/unsupported input with stable categories, and performs deterministic structural/admission checks in the client submission path. |
| Dispatcher ingress and parking | Durable callbacks use an explicit `DurableAck`; the precise acknowledgement boundary is a conditional atomic catalog transaction that writes the canonical label plus queued recovery metadata. In-memory insertion follows that durable transition, and a later lifecycle state wins over ingress redelivery. Catalog snapshots expose recovery labels plus park reason/attempt/next retry/last error. Batch failures release catalog-owned records for retry; malformed snapshots and dependency lookup failures are isolated. Composite parent snapshot, child IDs, packed ordered programs, and reply metadata are persisted before placement and reconstructed as one all-or-nothing scheduling unit. Parking uses bounded exponential backoff, capability-relevant wakeups, explicit TTL expiry, and bounded decision history. |

### 6.2 Test and benchmark evidence

- The native development build completes successfully.
- Prompt 05 completed the first claim-to-artifact audit on 2026-07-30 at
  commit `6a68b4f`; the detailed implementation/hermetic/live/failure/
  public-API/CI/performance map is `docs/evidence-map.md`. Every mapped
  capability either names exact evidence or is marked unproven. Fresh discovery
  remains 407 tests: 257 unit, 91 smoke, 3 integration, 15 kernel, and 41 bench.
  The unit lane passed 257/257. Current CI reaches the unit lane and its bounded
  Compose golden path, but not the full smoke/integration/kernel/benchmark
  lanes, failure injection, Python pytest, MCP pytest, POSIX-intercept
  execution, or live elasticity.
- P11/P12 fresh configure/build completed successfully. The acknowledgement
  point is the successful conditional atomic catalog transaction that commits
  `status=queued` and the canonical recovery snapshot; process-memory insertion
  is downstream of that boundary. The remaining ingress crash window is after
  the catalog commit and before the JetStream ACK reaches the server, where
  catalog recovery or transport redelivery is expected and later lifecycle
  state is not regressed. Scheduled delivery uses a 10-second recovery lease.
  Uncertain worker-side external effects remain governed by the existing
  idempotency boundary. Live evidence is limited to the pre-effect idempotent
  core-write worker-loss window listed below; arbitrary mid-effect recovery is
  not claimed.
- The 2026-07-15 live-validation tree reconfigured and built successfully.
  CTest discovers 407 tests; the hermetic unit lane contains 257 tests and all
  257 pass. The catalog cancellation/claim regression also passes against the
  live DragonflyDB fixture.
- P09's focused scheduling/solver/registry lane passes 18 tests (13 new
  scheduling-feasibility cases and the existing solver cases); the new tests
  cover the Section 9.7.9 scenarios and the exact 8 MiB paper trace.
- Redis/NATS-dependent backend and continuation cases are now classified as
  smoke tests instead of unit tests.
- The P06 client completion API and P07 durable transport integration compile in the clean native build; the hermetic unit lane contains 233 passing tests. The bounded live WS2 evidence below now supplements, but does not broaden, those hermetic claims.
- P08 was a documentation-only audit at commit `d8e6a4d`. P09 now supplies
  hermetic typed scheduling, feasibility, registry-v2, residual round-trip,
  and paper-trace evidence; it makes no performance claim.
- P11/P12 focused registry, capability-version, feasibility, parking-history,
  and malformed-input selection passes 10/10 tests. New live catalog tests
  cover loss of dispatcher memory after durable handoff, unknown dependencies,
  Composite reconstruction, and malformed recovery records; a public-client
  `kv://` case targets the configured user Redis fixture. Those live tests were
  were not validated in that earlier P11/P12 session. The bounded live run
  below now covers dispatcher recovery, manager reconstruction, and Composite
  execution on the stated topology; external-KV completion is not part of this
  validation claim.
- P10 adds trace-derived worker service, queue-depth, and per-scheme throughput
  features to telemetry and feeds them into trace-guided Constraint/MinMax
  decisions. The opt-in live harness is
  `tests/benchmark/bench_trace_guided_selection.cpp`; it uses the public client
  API, three deterministic workloads, completion verification, and separate
  submission/completion latency distributions. The hermetic trace-selection
  and attempt-accounting tests pass. Prompt 06 made the method reproducible:
  attempt identity is `(label_id, attempt)`, failed/cancelled/zero-byte and
  duplicate/out-of-order/expired attempts cannot contaminate successful EWMAs,
  all trace tuning is TOML-backed, and the predeclared arms are Round Robin,
  static-weight-matched MinMax ablation, and trace-informed MinMax. The reduced
  fresh-volume Compose dry run passed all three arms and the informed arm's
  public-observability calibration gate (at least two sampled workers and at
  least 20% service separation). CTest now discovers 413 tests; the hermetic
  unit lane passed 263/263. This is method-readiness evidence only: no
  speedup/negative-result claim exists and P10 remains pending Prompt 08's full
  experiment.
- Prompt 07 completed scheduling-contract fidelity on 2026-07-30. MinMax now
  calculates immutable batch target shares from captured initial profits,
  distributes known reservations by bytes or batches containing unknown demand
  by unit count, checks the proposed unit against remaining share, recomputes
  decision-time profit from residual capacity, and spills deterministically
  while preserving bounded cold-worker exploration. The common gate rejects a
  known positive reservation against zero available bytes, treats blocked units
  as explicitly infeasible/deferred, enforces exact attachment schemes, and
  applies soft locality only after feasibility. Append-only FlatBuffers tables
  and a typed policy union record Round Robin cursor context, Random seed/draw
  context, Constraint profile and exact contributions, MinMax shares/profits/
  consumption/spill/objective, trace samples/anchors, tie-breaks, ranks, and
  captured typed replay inputs. A hermetic replay test runs the production
  policy and dispatcher-to-label snapshot path, serializes/deserializes the
  labels, reconstructs the typed batch/worker/profile inputs solely from the
  structured residual, and reproduces assigned, hard-excluded, and blocked/
  parked outcomes. Fresh discovery is 420 tests; the unit lane is 270/270
  passing. Seven unit tests were added; none were removed or reclassified. This
  is hermetic correctness/replay evidence, not live placement or performance
  evidence.
- Prompt 08's 2026-07-30 three-arm attempt is invalid and does not close WS3's
  performance exit. Retained run
  `tests/benchmark/artifacts/p08-20260730T193949Z` contains 1,260/1,260
  completion-verified measured rows, passed the public policy/profile probes,
  and passed informed calibration, but it violates the exact experiment
  contract. Its 20 repetitions reused one runtime/volume per arm, accumulated
  catalog/cache/trace state, and exhibit strong serial dependence (lag-1
  autocorrelation approximately 0.77–0.85), invalidating Mann–Whitney and
  bootstrap inference that treats repetitions as independent. Untimed setup
  and read-back verification labels also entered the trace history; final
  samples total 882 per arm versus 441 warmup-plus-timed workload labels.
  Recorded service/queue fields are selected-worker scheduling EWMAs rather
  than actual per-label observations. Finally, the source identity hash omitted
  then-untracked runner/analyzer files. Ablation and informed made zero
  different worker choices across the 420 paired measured labels, but that
  diagnostic fact supports neither improvement nor a formal null result. Two
  earlier orchestration attempts are separately marked invalid (no-row Docker
  wait API failure; baseline-only stdin-consumption failure). Prompt 08 requires
  a newly frozen, source-complete method with genuinely independent replicates,
  measurement-external/deferred verification, and actual per-label
  service/queue observations. A bounded prerequisite slice now appends
  versioned worker execution observations to Completion and exposes actual
  queued-to-start and start-to-terminal durations through the public C++
  completion result. Its native build and 271-test hermetic lane pass, but the
  live Compose path is not yet validated because the Docker daemon was
  unavailable. Prompt 08 still requires a newly frozen independent-replicate
  runner and valid run. The replacement runner and analyzer are now frozen in
  committed source: 20 fresh-volume cells per profile/arm, a fixed 43-label
  training phase, post-measurement verification, actual Completion observations,
  equal advertised worker descriptors with identical cross-arm 0/20/60 ms hidden
  delays, seeded cell order, independent-replicate inference, and an observed-
  placement-treatment gate. No replacement result has yet been produced. The
  user explicitly lifted the prior stopping rule and opened Prompt 09; this does
  not change Prompt 08's blocked performance status.
- **Prompt 09 public C++/C API coherence (2026-07-31):** clean native build and
  fresh CTest discovery report 432 tests: 277 unit, 96 smoke, 3 integration,
  15 kernel, and 41 bench. The hermetic unit lane passed 277/277. Five new
  operation tests cover shared lifetime after producer destruction, concurrent
  wait/cancel, nonterminal timeout reuse, repeatable read retrieval, and invalid
  handles; the C link test covers stale and double release; and one test compiles
  every fenced native SDK snippet against the real headers. Three live public-C
  cases passed against the healthy reference topology for client destruction,
  timeout followed by reuse, and concurrent wait/cancel/status release. A sixth
  smoke case verifies Submitted/Queued/Shuffled/Scheduled/Executing/Completed/
  Unknown catalog lifecycle projection. The same six hermetic lifetime cases
  plus those three live C cases passed under AddressSanitizer and
  UndefinedBehaviorSanitizer with leak detection; two known cnats null/zero-byte
  nonnull-attribute reports were suppressed by source file via
  `tests/ubsan.supp`, with no LABIOS suppression. A TSan
  configure was attempted but the instrumented build-time `flatc` failed before
  project tests with `ThreadSanitizer: unexpected memory mapping`; no TSan claim
  is made. `labios-demo` completed 24/24 file/SQLite round trips, and the C++ and
  C golden examples both exited zero. No Python, MCP, backend, schema,
  cross-process-coordination, or distributed-configuration capability was added.
- The coding-agent benchmark is explicitly a Label I/O control-plane encoding
  and trace-construction microbenchmark.
- The former multi-agent benchmark is explicitly a one-process workspace
  component benchmark.
- The former cross-backend ETL benchmark is explicitly a URI and registry
  control-plane microbenchmark.
- The live `[data_path][pipeline]` integration test proves source data written
  through `file://`, deterministic SDS transformation, and result read back
  through `sqlite://` after completion. Compose workers are Tier 1 and mount one
  shared worker data volume, matching their `Shared` registry attachments, so
  the pipeline no longer retries to compensate for contradictory storage.
- **Bounded WS2/WS3 live correctness validation (2026-07-15):**
  `tests/live/ws2_ws3_correctness.sh` passed twice from fresh named volumes as
  runs `20260715T160503Z-16d4f13-pass1` and
  `20260715T160847Z-16d4f13-pass2`; command transcripts, submitted IDs,
  outcomes, image identities, topology, and byte checks are retained under
  `artifacts/ws2-ws3-live/` (both runtime runs used dirty-tree identity
  `460d00dfb90c318fa05ceb09dccc7847a82d4d67` over commit `16d4f13`). The
  topology was one single-host Compose project
  with persistent NATS JetStream and DragonflyDB, one manager, one dispatcher,
  three Tier-1 workers sharing one file/SQLite volume, and the external-Redis
  fixture. The exact live claims are limited to: source → pipeline → destination
  bytes; worker SIGKILL after `Executing` but before an idempotent core-write
  effect, followed by JetStream redelivery, durable completion, and a
  post-completion replay whose unchanged mtime proves dedupe; dispatcher
  SIGKILL after durable queued admission and before delivery; zero-worker
  direct and Composite parking across dispatcher restart; manager registry-v2
  reconstruction, replacement epoch, and stale deregistration rejection; one
  mixed ready/blocked/local-read/local-write/Composite/unknown-size/over-capacity
  scheduling batch with explicit decisions, 16/24/32 KiB capacity enforcement,
  legal parking, later dependency release, and checked result bytes; and public
  C++ completion outcomes for parked cancellation, a cancellation-winning
  Scheduled race, and an execution-winning `TooLate` race. The worker-loss
  fixture uses a deterministic post-claim delay solely to expose that window.
  No arbitrary mid-backend-effect recovery, catalog-loss recovery, multi-node
  behavior, exactly-once transaction, or performance result is claimed.
- No current benchmark proves live multi-process agent coordination. The
  cross-backend ETL benchmark remains a control-plane microbenchmark rather
  than an ETL claim.
- The smoke, kernel, and broader integration lanes were not rerun for P09.
  The bounded mixed-batch Compose evidence below now validates the listed WS3
  correctness cases only; it is not a performance result. Python hermetic
  pytest now covers the public SDK shape and registry-v2 parser; Prompt 10 also adds opt-in live Compose pytest and a public-API golden example.
  Generated Python FlatBuffers support closes the package-level P09 registry-v2
  gap, but MCP worker-registry/score/count behavior remains unchanged and
  unsupported until Prompt 11 consumes the shared parser.
- **Prompt 11 MCP core Label I/O ingress (2026-07-31):** 17 hermetic MCP
  pytest cases cover tool schemas/imports, typed lowering, source/destination/
  pipeline/intent/priority preservation, stable error categories, timeout,
  cancellation, parking, malformed requests, registry-v2 malformed/truncated/
  version-skew rejection, unsupported arbitrary transforms, and absence of
  direct Redis/NATS/volume adapters. Four opt-in Compose cases and the stdio MCP
  golden cover exact store/retrieve bytes, registered identity-pipeline
  destination bytes, timeout/cancellation race projection, versioned typed
  label and placement inspection, and public health/worker/profile observation.
  The MCP image builds the native Python SDK from the same repository sources
  and installs FlatBuffers 24.3.25. UBSan passes the combined 27 Python/MCP
  hermetic cases. ASan passes the 17 MCP boundary cases with leak detection
  disabled; leak-enabled execution reports CPython/pytest process-exit
  allocations, and the full Python exception-path lane hits an ASan
  `__cxa_throw` interceptor check before completion, so no leak-enabled or full
  exception-path ASan claim is made. Workspace-backed knowledge remains pending
  Prompt 14; no Prompt 12/13 coordination, new backend, arbitrary execution, or
  distributed pipeline-stage behavior is claimed.
- The HPDC'19 published results (up to 16x CM1 I/O improvement, 6x HACC, 17x
  Montage, 40-60% execution-time reduction) are prior-publication evidence.
  None of them has been reproduced by this repository, and no current document
  may imply otherwise.

### 6.3 Baseline conclusion

LABIOS 2.0 is a broad reference prototype with substantial component coverage.
LABIOS 2.1 should connect and validate those components around the Label I/O IR
instead of adding another layer of nominal features.

### 6.4 WS0 semantic contract

P01 established the normative Label I/O IR version-1 semantic contract in
docs/label-ir.md. The contract partitions all 68 current FlatBuffers fields and
all 34 LabelData members into sealed program, runtime residual, or execution
state; defines component mutation authority; specifies version-0 normalization,
typed resources, operation descriptors, staging and pipeline semantics, legal
shuffler/scheduler decisions, deterministic validation, and stable rejection
categories.

The contract is specification, not a claim that WS0 runtime support is already
implemented. It records the current contradictions that later WS0 prompts must
resolve: no ir_version or typed ResourceRef fields, no FlatBuffers verifier,
Pointer-based dispatcher routing versus URI-based worker execution, unbound
warehouse input overriding a declared pipeline source, operation strings
without descriptor enforcement, unsafe aggregation/hazard analysis, unwired
Delete and Flush worker paths, child-ID reuse for Composites, and silent batch
abandonment when placement is unavailable. Current clients also stage data and
may create catalog state before dispatcher admission, so deterministic
rejection requires preflight or cleanup that is not implemented yet.

Existing serialization fixtures remain evidence about the codec. The contract
classifies which fixtures normalize into executable version 1 and which are
codec-only or intentionally incompatible; that classification is input to
later test planning and does not itself require P02 to rewrite those tests.

## 7. Candidate design areas

These are discussion areas, not approved implementation commitments.

### 7.1 Label IR semantics

Define the stable operations, resources, constraints, pipeline representation,
completion behavior, validation rules, and legal optimization boundaries of the
IR.

### 7.2 Composable I/O programs

Make the existing source URI → transformation pipeline → destination URI path
correct end to end. Begin with a single-worker pipeline before considering
distributed stage placement.

### 7.3 Asynchronous completion

Define submission, test, wait, wait-any, wait-all, cancellation, failure, and
completion-recovery semantics as part of Label I/O rather than incidental
transport behavior.

### 7.4 Resource-aware worker pools

Match label requirements to worker capability and resource vectors. Separate
feasibility from policy optimization. Use label traces and telemetry to improve
pool selection before considering automatic provisioning.

### 7.5 Shadow namespaces

Explore logical resource aliases and lazy shadow metadata that resolve external
URIs without copying entire external namespaces. External systems remain
authoritative.

### 7.6 Transient I/O resources

Clarify the roles of channels, workspaces, and optional regions. Any primitive
claimed to coordinate producers and consumers must work across processes.

### 7.7 Frontends

Choose new frontends based on the Label I/O semantic property they demonstrate,
not because an old plan named a library. Agent I/O is the primary modern use
case; a scientific frontend can demonstrate generality.

### 7.8 Evidence

Use targeted agent, scientific, and synthetic workloads. Keep prior published
results separate from results reproduced by the current runtime.

## 8. Annual-report research alignment

The NSF project (Award 2313154) ends July 31, 2027 following a no-cost
extension. The upcoming annual report should describe intellectual progress,
publications, artifacts, and current implementation evidence; it should not
present historical technology choices as a literal software checklist.

The proposal's four Key Objectives, as named in the submitted narrative, map
naturally to Label I/O:

| Proposal objective | LABIOS 2.1 expression |
|---|---|
| I/O Integration | Multiple scientific and agent I/O frontends compile into a common Label I/O IR while preserving semantics. |
| I/O Asynchronicity | Submission, execution, data staging, dependency, and completion are separated and explicitly defined. |
| Storage Dynamic Deployment | Label requirements and observed workloads guide selection among heterogeneous workers, pools, and available allocations. |
| Storage Programmability | Labels carry composable source, transformation, and destination programs that execute near data. |

HCL, Luxio, and LabStor are historical mechanisms and research artifacts. For
2.1, their reusable ideas are lazy namespace resolution, profile-guided pool
selection, and composable I/O stacks. Prior publications and external artifacts
must be credited separately from behavior reproduced by the current repository.

### 8.1 Reporting-period delta table

The report should be auditable at a glance: for each objective, one column of
prior published evidence and one column of evidence produced by this repository
during the reporting period. WS6 populates the current-evidence column; nothing
may appear there without a test, trace, artifact, or experiment identifier.

| Objective | Prior published evidence | Current repository evidence | Planned 2.1 advance |
|---|---|---|---|
| I/O Integration | HPDC'19 LABIOS design and adapters; later publications (needs user input) | POSIX-intercept prototype, Python/C SDK surfaces, honest capability matrix (§6.1) | WS0 versioned IR; WS5 two frontend families |
| I/O Asynchronicity | HPDC'19 async-label results (CM1: up to 16x I/O, 40% runtime reduction) | Async client API exists; completion semantics not yet deterministic (§6.1) | WS2 asynchronous Label I/O semantics |
| Storage Dynamic Deployment | HPDC'19 worker scoring and bucket sort; Luxio publications (needs user input) | Four solvers and scoring exist; solvers receive no real batch input (§6.1) | WS3 resource-aware selection with real inputs |
| Storage Programmability | HPDC'19 Montage result (65% shorter execution, 17x I/O); LabStor SC'22 | Eleven pipeline builtins plus live P04 source → pipeline → destination evidence (§6.1) | WS1 executable composable I/O |

The HPDC'19 numbers above are prior publication results, not results of this
repository.

### 8.2 Evidence required from the project team

These inputs cannot be derived from this repository and must be supplied by the
team (all marked "needs user input"):

- Publication list attributable to Award 2313154 since 2023, including student
  theses and journal follow-ups.
- Software release artifacts: version tags, DOIs, and the LoC/feature/test
  metrics the proposal promised to track.
- Education and broader-impact outcomes: whether the proposed CS590 storage
  course ran, REU/IPRO participation, mentoring and outreach counts.
- Testbed results beyond HPDC'19 (Chameleon, institutional cluster, DoE or TACC
  systems), if any exist for the reporting period.
- Confirmation of the July 31, 2027 no-cost-extension end date from the award
  record.
- Reconciliation of the HPDC'19 recognition: the proposal summary states LABIOS
  received the Karsten Schwan Best Paper Award, while current repository
  documents say "Best Paper Nominee." Resolve from the award record before the
  report cites either.
- Attribution boundaries: Hermes (OAC-1835764) and ChronoLog (CSSI-2104013)
  results belong to other awards and must not be reported as LABIOS outcomes.

The evidence set also includes, from this repository: the corrected engineering
truth baseline and test evidence, concrete Label I/O advances during the
reporting period, honest limitations and changes in implementation mechanism,
and a focused plan through July 2027 tied to the four objectives.

## 9. Working Set roadmap

Each Working Set is delivered through the small bounded prompts directly under
`.planning/prompts/`. Execute those numbered files in lexical order. No session
owns the entire roadmap, and no prompt is parallelized unless the user explicitly
changes the sequence. This section carries the intellectual structure.

Dependency structure, validated against the code at `d9bdfa8`:

```text
WS0 ──► WS1 ──► WS5 ──► WS6
 │       │               ▲
 ├──► WS2 ──► WS3 ───────┤
 └──► WS4 ───────────────┘
```

- WS1 needs only WS0's resource-shape and field-partition decisions, not the
  full WS0 implementation.
- WS2 and WS3 implementation sessions both modify the dispatcher batch loop
  (`src/services/labios-dispatcher.cpp`) and must be serialized, WS2 first.
- WS4 shares no dispatcher or schema hot spots and may proceed in parallel with
  WS2 or WS3.

### WS0 — Versioned Label I/O IR

**Research question:** What field partition, resource typing, and validation
rules make a label a well-defined I/O program, and which runtime optimizations
are legal against its declared semantics?

**Engineering outcome:** an explicit `ir_version` on the wire, FlatBuffers
verification of all externally received buffers, deterministic validation,
typed-resource normalization at ingress, and a written semantic contract in
`docs/label-ir.md`. Specify field partitions, IR versioning, typed resources,
core and namespaced operations, compatibility normalization, pipeline
semantics, and legal optimization boundaries.

**Depends on:** nothing; first Working Set.

**Non-goals:** a textual DSL; nested wire tables; removing legacy pointer/URI
fields within this Working Set.

**Exit:** invalid or corrupted labels fail deterministically, current valid
labels normalize into IR 2.1, and the semantic contract is explicit enough for
later passes.

### WS1 — Executable composable I/O

**Research question:** Can a declared I/O program (source resource,
transformation pipeline, destination resource) execute end to end while
preserving its declared semantics?

**Engineering outcome:** a real source → pipeline → destination path through
the client, dispatcher, worker, program repository, and backend registry. The
worker loads the declared source through the backend registry, and the default
deployment can execute pipelines. Start with one worker and deterministic
builtins.

**Depends on:** WS0's semantic decisions (resource shape, pipeline and staging
precedence rules); not on the WS0 implementation slices.

**Non-goals:** distributing individual pipeline stages; new builtins;
performance claims.

**Exit:** an end-to-end test reads actual source data through one backend,
transforms it, writes through a different destination backend, waits for
completion, and verifies the result.

**P04 status (Complete, 2026-07-14):** the worker loads an unbound declared
source through `BackendRegistry`, preserves bound staged-input execution, maps
worker/source/pipeline/destination failures to categorized completion errors,
and the live Compose data-path test passed with all default workers at Tier 1.

### WS2 — Asynchronous Label I/O semantics

**Research question:** What completion guarantees does asynchronous Label I/O
give producers, and what delivery machinery is required to honor them?

**Engineering outcome:** lifecycle inspection, nonblocking test, wait-one,
wait-any/all, cancellation boundaries, errors, and recoverable completion
state. This Working Set explicitly owns the transition from fire-and-forget
core NATS to durable, acknowledged delivery, and the fix for the dispatcher
dropping batches when no workers are registered. Transport changes are selected
to support the semantics, not treated as the semantics themselves.

**Depends on:** WS0; live validation benefits from WS1.

**Non-goals:** exactly-once distributed transactions; scheduling policy
changes.

**Exit:** completion behavior is deterministic under concurrency and selected
restart/failure cases, and admitted labels are never silently lost in the
tested failure cases.

### WS3 — Resource-aware selection and pools

**Research question:** Given real label batches and worker capability vectors,
does resource-aware selection produce explainably better placement than a fixed
policy?

**Engineering outcome:** solvers receive real batches with label
sizes/requirements, typed resource locality, pipeline requirements, and worker
capability/resource vectors. Feasibility filtering is separated from policy
optimization. Correctness (real solver inputs, explainable choices) is a
separate slice from performance (trace-guided selection against an explicit
baseline objective).

**Depends on:** WS0; implementation serialized after WS2 (shared dispatcher
batch loop); the performance slice additionally needs WS1 and WS2 for real
execution traces.

**Non-goals:** automatic provisioning; leader election; new solver algorithms
before real inputs exist.

**Exit:** different valid workload profiles produce explainable, tested worker
or pool choices (correctness), and improve on an explicit baseline objective in
a reproducible experiment (performance).

### WS4 — Federated namespaces and shared transient I/O

**Research question:** Can independent processes resolve the same logical
resources and coordinate through named transient primitives while external
systems remain authoritative?

**Engineering outcome:** channel/workspace identity, ACL, and sequence state move
from process-local memory into the catalog so independently started processes
reconstruct the same primitive. Lazy shadow metadata is deferred beyond 2.1
unless the evidence map identifies a named release workflow that requires it.

**Depends on:** WS0 (channel/workspace/typed-resource families) and the validated
public client path. The active prompt queue serializes this work after MCP core
ingress so its real consumer and API requirements are known.

**Non-goals:** a separate region primitive (D14) until a demonstrated bulk-data
use case cannot be represented by the two existing primitives; copying or
owning external namespaces (D13).

**Exit:** independent processes resolve the same logical resource and coordinate
through a named transient primitive without direct DragonflyDB access.

### WS5 — Agent and scientific frontends

**Research question:** Do two distinct frontend families compile equivalent I/O
intent into the same versioned IR and receive equivalent semantics?

**Engineering outcome:** MCP ingress compiles tool I/O into labels that
traverse the dispatcher/worker path (no direct warehouse or volume access for
Label I/O), and one scientific frontend chosen for semantic breadth. MCP
remains an ingress protocol, not the internal abstraction. The POSIX intercept
remains a prototype; extending it is in scope only as far as the chosen
scientific workflow requires, and full transparent POSIX coverage is not a 2.1
commitment.

**Depends on:** WS1 (executable path); WS2 (wait semantics for synchronous tool
responses); the workspace-backed MCP tool needs WS4's cross-process state.

**Non-goals:** a generic tool broker; mapping tools without I/O semantics into
labels.

**Exit:** two distinct frontend families compile equivalent I/O intent into the
same versioned IR and receive equivalent semantics.

### WS6 — Evidence, annual report, and release

**Research question:** Which Label I/O claims are supported by reproducible
current evidence, and how do they compose with prior publications into the
annual report?

**Engineering outcome:** opt-in live end-to-end benchmarks (agent-oriented
workload and a Montage-style producer/transform/consumer workflow with
semantically fair baselines), a populated §8.1 delta table, and a release
coherence pass aligning public documents with the capability matrix. Never mix
historical performance numbers with results reproduced by 2.1.

**Depends on:** WS1, WS2, WS4, WS5 for live demonstrations; the evidence-map
planning work may begin earlier with user-supplied inputs (§8.2).

**Non-goals:** performance claims without semantically fair baselines;
presenting microbenchmarks as end-to-end results.

**Exit:** every public and annual-report claim points to a publication,
artifact, test, trace, or reproducible experiment.

## 10. Decision and implementation protocol

Only one design area is decided or implemented at a time. The active numbered
prompt files are serialized; concurrency requires an explicit user decision.

For each area, the working session must present:

1. The intellectual question.
2. The relevant current implementation.
3. Two or three bounded options.
4. One recommendation with tradeoffs.
5. The smallest implementation slice.
6. The evidence that would validate it.
7. One explicit user decision.

No implementation begins until that decision is made. After implementation, the
session reports the focused diff and verification result before proposing the
next area.

## 11. Decisions

### Accepted

- **D1 — Label I/O IR:** Label I/O is the central abstraction. Frontends compile
  I/O intent into labels; the runtime resolves, optimizes, schedules, and executes
  them.
- **D2 — Reference runtime:** The LABIOS runtime is the proof of concept and
  reference implementation of the abstraction, not the sole value of the work.
- **D3 — Agent focus within the I/O boundary:** LABIOS will be optimized for
  agent-generated I/O without expanding into arbitrary tool or compute execution.
- **D4 — Current technology stack:** Historical mechanisms may be re-expressed
  using the current stack; HCL, Luxio, and LabStor are not mandatory dependencies.
- **D5 — Depth before breadth:** Complete and evaluate existing paths before
  adding more backends, adapters, or named capabilities.
- **D6 — Versioned semantic IR:** Before the first 2.1 runtime slice, define a
  versioned Label I/O semantic specification covering origin versus residual
  fields, operation and resource semantics, pipeline rules, completion rules,
  validation, and legal optimization boundaries. Do not add a textual DSL,
  parser, or separate end-user language at this stage.
- **D7 — Flat versioned wire format:** Keep the current flat FlatBuffers label
  representation for 2.1. The semantic specification will classify every field
  as sealed program, runtime residual, or execution state; introduce explicit IR
  versioning; and enforce legal mutation through validation and APIs. Do not
  restructure labels into nested wire tables or maintain parallel wire formats.
- **D8 — Core operation plus namespaced refinement:** Retain a small semantic
  core (`Read`, `Write`, `Delete`, `Flush`, `Observe`, and `Composite`) and use a
  versioned namespaced operation descriptor such as `file.rename` or
  `vector.search` to refine backend-specific I/O. Extensions must declare I/O
  inputs, outputs, and resource semantics; they do not turn labels into generic
  compute tasks.
- **D9 — First runtime slice:** After WS0 establishes the semantic foundation,
  the first runtime implementation slice is the complete source → pipeline →
  destination path. This is the shortest route from component breadth to an
  executable Label I/O program.
- **D10 — Annual-report narrative:** Use research continuity with modernization.
  Organize progress around I/O integration, asynchronicity, dynamic resource
  selection, and programmability; present Label I/O IR as the unifying advance
  and agent I/O as the modern workload context.
- **D11 — Typed canonical resources:** Use an expanded typed resource union as
  the internal semantic representation. URIs remain ingress/interchange syntax
  and legacy pointers remain compatibility inputs during migration.
- **D12 — Separate semantic versioning:** Add an explicit `ir_version`; do not
  overload the existing resource/data `version` field.
- **D13 — Lazy shadow metadata:** Pursue logical aliases plus selected,
  refreshable external metadata and versions. Do not copy or own complete
  external namespaces, and defer full federated overlays until evidence demands
  them.
- **D14 — No new region primitive yet:** Deepen channels and workspaces and make
  them cross-process before adding a third transient-data abstraction.
- **D15 — Demonstration pair:** Use an agent-oriented I/O workflow as the primary
  modern demonstration and a Montage-style scientific
  producer/transform/consumer workflow as the continuity demonstration.
- **D16 — Working Set execution:** Future Codex sessions operate from generated,
  bounded prompts organized by WS0–WS6. Each session verifies its slice and
  updates this document before a later Working Set proceeds.

### Implementation-level decisions

Exact field layouts, compatibility windows, completion race semantics,
scheduling objectives, and frontend selection are delegated to their Working
Set prompts. They must remain inside D1–D16 and return for user input only when a
choice changes those strategic boundaries.

#### P01 / WS0 — Label I/O IR semantic contract

Approved on 2026-07-14 as one decision bundle:

- **P01-A2 — Legacy version normalization:** ir_version 1 is the initial
  normative semantic version. A missing field is version 0 and must be
  deterministically normalized; unknown nonzero versions are rejected. The
  existing Label.version remains a separate resource/data version.
- **P01-B3 — Typed canonical addressing:** ResourceRef is the sole canonical
  internal address. Pointer and URI fields are compatibility inputs and
  rolling-upgrade projections. Independently normalized equivalent forms
  coalesce; genuine disagreement is ADDRESS_CONFLICT.
- **P01-C3 — Declared-source authority and bound staging:** a declared source
  defines pipeline input. Warehouse data may substitute only with a sealed
  binding to that exact source identity, scope, and version. Version-0 direct
  producer and MemoryPtr staging patterns normalize explicitly; remaining
  ambiguity is AMBIGUOUS_INPUT.
- **P01-D3 — Contextual validation:** verification, version-0 normalization,
  producer admission, runtime transition, recovery snapshot, and completion
  validation are distinct contexts. Passing the codec does not make a label an
  executable program. Failures use stable categories and never crash a
  receiver.
- **P01-E2 — Registered refinements:** the six LabelType operations remain the
  semantic core. A refinement is a registered, versioned namespace.verb
  identifier with declared I/O roles, resource effects, ordering, atomicity,
  retry, cancellation, result, and capability semantics. It is not a textual
  DSL or generic compute/task escape hatch.

P01 is bounded by these implementation-level guardrails:

- The version-1 descriptor registry contains only core.read, core.write,
  core.delete, core.flush, core.observe, and core.composite at descriptor
  version 1. The only version-0 operation aliases are empty-by-type, read for
  Read, and write for Write. Any vector.search discussion is non-normative and
  the descriptor is not registered.
- ir_version, operation_version, typed ResourceRef fields, and descriptor
  enforcement are normative additions, not current capability. Delete and
  Flush remain unwired through the current worker path.
- Version-0 normalization attempts typed/URI/Pointer equivalence, the explicit
  alias table, and recognized staged-input bindings before ADDRESS_CONFLICT or
  AMBIGUOUS_INPUT is returned.
- Unit-fixture classification is documentation and later test-plan input, not
  an instruction that P02 rewrite the fixtures.
- Legality rules cover only aggregation, dependency/hazard analysis,
  reordering, Composite/supertask grouping, scheduler deferral, and
  cancellation decisions faced by the existing shuffler and scheduler. They
  do not commit unplanned optimization passes.

The contract also fixes these implementation-level WS0 details:

- New Label fields are append-only after the current Label.result FlatBuffers
  slot. Version 1 adds ir_version, operation_version, source_resource,
  destination_resource, declared_dependencies, and a serialized
  StagedInputBinding/input_binding; existing field order and enum values are
  preserved. HazardType appends Order and Barrier.
- StagedInputBinding carries a stable content ID, provenance, length, optional
  digest, and observed source version. Its physical warehouse/cache location
  is authenticated ContentManager residual state, not a sealed address or
  user resource.
- Version-1 producers declare predecessor IDs in sealed
  declared_dependencies. Ingress lowers them into the existing append-only
  runtime dependencies residual; the hazard analyzer may append but never
  remove or retarget edges. Ordered frontends must emit these edges instead of
  relying on IDs or transport order.
- A version-0 null MemoryPtr Read destination normalizes to completion
  retrieval, while a staged MemoryPtr Write source becomes one synthesized
  MemoryResource plus its MaterializedSource binding.
- FilePath, bare file paths, and authority-less file URIs use one configured
  default file backend and namespace. File/object core Delete is whole-resource
  only. Durable completion waits for the registered backend durability barrier;
  unsupported durability is not silently weakened.
- Invalid labels use a fixed validation/category precedence. An all-default
  LabelResult is semantically absent before terminal state and a valid
  zero-byte result when status is Complete.
- Rolling deployment is capability-gated. A selected legacy consumer requires
  mandatory, equivalent, lossless Pointer/URI projections; typed-only labels
  never flow to version-0 consumers, and semantics are never weakened for
  compatibility.
- TTL is a checked latest-start deadline from created_us; priority is a
  per-label scheduling preference, not an ordering edge. Fail-fast Composite
  execution terminalizes and publishes every skipped child as
  DEPENDENCY_FAILED or COMPOSITE_ABORTED before publishing the Composite's
  terminal completion.

### P02 / WS0 — Versioned wire safety and deterministic validation

**Complete (2026-07-14).** Added append-only `ir_version`, FlatBuffers verification
for labels and completions, version-0 normalization, stable categorized decode
errors, core descriptor admission checks, and rejection handling at service
receive boundaries.

### P03 / WS0 — Typed resources and ingress normalization

**Complete (2026-07-14).** Added the append-only ResourceRef union, staged-input
binding, declared dependency wire fields, and Order/Barrier hazards. URI and
legacy Pointer inputs normalize into typed resources at client and label-manager
ingress; equivalent forms coalesce, conflicts reject deterministically, and
legacy mirrors remain populated for existing dispatcher/worker consumers.
Version-0 staged writes receive DirectProducer or MaterializedSource bindings,
and declared dependencies lower into the append-only residual dependency list.

**WS0 exit evidence:** unit validation and serialization tests pass; invalid
resource/address combinations fail with stable categories, and valid URI,
Pointer, staged-input, and dependency forms normalize into IR 2.1.

### P04 / WS1 — Executable source → pipeline → destination

**Complete (2026-07-14).** The worker now honors declared-source authority,
loads source bytes through the registered backend, executes existing SDS
builtins, writes to a distinct destination backend, and reports categorized
execution failures. The default Compose workers advertise Tier 1 capability.
The live `Source URI pipeline writes to a different SQLite backend` test passed
against rebuilt Compose services.

### P05 / WS2 — Asynchronous completion contract

**Complete (2026-07-14).** Defined the normative asynchronous Label I/O
completion contract in `docs/label-ir.md` Section 13. The contract retains the
P01 lifecycle and ownership model, adds observable Parked and semantic
Cancelled states without a breaking status-enum change, specifies test/wait/
wait_any/wait_all and timeout results, fixes the pre-execution cancellation
boundary, classifies P01 failure categories by retryability, and makes an
empty worker pool a durable parked/requeued outcome rather than a dropped
batch.

Implementation-level decisions:

- **P05-A — Lifecycle representation and ownership:** `Submitted` is a
  pre-admission client state; admitted labels follow
  `Admitted -> Queued/Parked -> Shuffled -> Scheduled -> Executing` and then
  `Completed` or `Failed`. `Parked` is a Queued substate. `Cancelled` is a
  semantic terminal state projected to current `Failed`/`CANCELED` wire values,
  with the catalog retaining the semantic lifecycle. Admission coordinator,
  shuffler, scheduler, worker executor/completion coordinator, cancellation/
  expiry coordinator, and recovery coordinator own the transitions explicitly
  listed in Section 13.1.
- **P05-B — Client result semantics:** `test` is nonblocking; `wait` is
  wait-one; `wait_any` returns the first deterministically selected terminal
  member; and `wait_all` returns one result per member. Complete, Failed,
  Cancelled, and timeout are distinct typed outcomes. Timeout never cancels or
  invalidates a handle. Existing chunked convenience waits are wait-all.
- **P05-C — Cancellation boundary:** cancellation is guaranteed only when
  its compare-and-set wins before `Executing`, best-effort during the
  Scheduled-to-Executing race, and impossible after `Executing` is recorded in
  IR version 1. Cancellation is idempotent and never claims rollback.
- **P05-D — No-worker admission policy:** an empty/unavailable worker pool or
  current solver placement parks an admitted label in the catalog with bounded
  backoff, observability fields, and registration/recovery wakeups. There is
  no attempt-count drop limit; TTL expiration or proved permanent infeasibility
  are explicit terminal outcomes.
- **P05-E — Delivery and restart scope:** delivery and terminal notifications
  are at least once. Workers deduplicate by stable label ID and use a declared
  idempotency mechanism or refuse blind replay of uncertain non-idempotent
  effects. Dragonfly catalog records persist canonical label snapshots,
  leases, dedupe state, completion records, and result bindings through
  dispatcher, worker, and client process restarts. Catalog loss is outside the
  guarantee, and retention expiry is observable rather than a fabricated label
  failure.

P06 owns client API implementation over these records. **P06 status (Complete,
2026-07-14):** the client now exposes nonblocking lifecycle inspection,
timeout-aware wait-one/any/all results, idempotent pre-execution cancellation,
and catalog completion snapshots.

**P07 status (Complete for bounded live windows, 2026-07-15):**
The runtime uses the NATS JetStream `LABIOS_LABELS` stream with durable explicit-ack
consumers, five-attempt redelivery by default, and a ten-second ack deadline.
Workers persist completion records before acknowledgement and suppress replayed
label IDs using those records. The dispatcher parks and re-enqueues no-worker
batches with bounded backoff. Native build and 233-test hermetic unit evidence
pass. The two fresh-volume runs named in Section 6.2 establish the WS2 exit only
for the recorded pre-effect worker SIGKILL, queued dispatcher SIGKILL,
zero-worker/restart, and cancellation race windows on that single-host
three-worker topology; broader crash points and uncertain non-idempotent effects
remain unclaimed.

### P08 / WS3 — Solver input contract

**Complete (2026-07-14).** Recorded the normative resource-aware scheduling
contract in `docs/label-ir.md` Section 9.7 after a read-only audit of commit
`d8e6a4d`. P08 changes documentation only; it does not claim that the current
runtime implements the contract.

Approved as one decision bundle:

- **P08-A — Typed jobs and one real batch:** derive typed `JobDescriptor`
  values entirely from existing Label/P03 fields, registered operation
  descriptors, and authenticated runtime catalog state. Add no new sealed
  producer field. Capture one P01-stable `SchedulingBatch` containing every
  direct-route, aggregate, Composite/supertask, independent, ready, and blocked
  unit, and invoke the selected policy once for the complete prepared batch.
- **P08-B — Legality before placement:** consume the canonical residual
  dependency graph, compute or validate a P01 topological ready frontier, use
  priority only among legally interchangeable ready units, and retain stable
  runtime ordinals only as tie-breaks. Solvers choose workers; they never
  reorder programs, split Composites, rewrite labels, or mutate dependencies.
- **P08-C — Versioned worker truth:** replace the three worker-registry CSV
  messages and the plain-text deregistration control on their existing NATS
  subjects with verified FlatBuffers v2 `WorkerRegistration`,
  `WorkerResourceUpdate`, `WorkerDeregistration`, and
  `WorkerRegistrySnapshot` payloads. Worker descriptors carry registration
  epochs, immutable registry generations, absolute and normalized resources,
  execution/IR/pipeline capability, exact backend attachments, and locality
  domains. Deregistration removes only the matching live epoch. Manager,
  workers, and dispatcher cut over together with no text/CSV fallback.
- **P08-D — Feasibility before policy:** apply one policy-independent
  unit/worker feasibility matrix covering availability, IR/operation version,
  tier, pipeline operations, source and destination attachments, hard
  locality, and known absolute byte demand. Validate checked cumulative
  reservations against each worker's captured per-batch byte budget before
  accepting any policy plan.
- **P08-E — Explicit partial outcomes:** require exactly one ordered placement
  decision per unit. Independent feasible ready units may schedule while
  unrelated units defer; a Composite is all-or-nothing. Zero workers, no
  available worker, current infeasibility, invalid plan output, and blocked
  successors retain admitted labels through the P05 queued/parked path rather
  than being omitted or dropped. Only the finite P01 proof set makes
  infeasibility permanent.
- **P08-F — Preserve four policies:** Round Robin, Random, Constraint, and
  MinMax retain their policy identities but consume only common feasible
  candidates and residual batch budgets. Their cursor, random draw,
  profile/contributions, or profit/share evidence is deterministic and
  replayable.
- **P08-G — Append-only decision history:** preserve the order and meaning of
  the six existing `ScoreSnapshot` scalar slots and append a versioned decision
  history. Record every assigned, failed, parked, and legal rescheduling
  attempt, including all worker candidates, exclusion reasons, capacity
  before/after, policy objective, and tie-break evidence. The latest successful
  worker mirrors into the legacy scalars; parked/all-zero histories still
  serialize the table.
- **P08-H — Deliberate MCP compatibility gap:** the former Python MCP worker
  parser is incompatible with registry v2. Prompt 10 now owns Python
  FlatBuffers support, generated bindings, and the shared parser (superseding
  the original P13 assignment); Prompt 11 owns any MCP behavior change.
  Worker-registry, worker-score, or worker-count MCP behavior remains
  unclaimed until then. The gap is explicit, not transparent compatibility.

Implementation boundaries and gate:

- Lossless typed-locator decoding for all declared ResourceRef families remains
  a P03 follow-up. P09 may advertise only families whose identities and
  locators survive the queue round trip; File, SQLite, and optional external KV
  are the initial executable set. Internal DragonflyDB remains plumbing and is
  never a user-backend attachment.
- The only approved Label schema work is append-only runtime-residual
  explainability. `batch_id`, registry generation, ordinal, readiness, and
  catalog residence remain runtime scheduling context rather than sealed
  producer input.
- Automatic provisioning, elasticity triggers, leader election, new solver
  algorithms, and distributed pipeline-stage placement remain outside P08/P09.
- **P08 status:** Go and complete (2026-07-14).
- **P09 ownership gate lifted (2026-07-14):** the executable P09 prompt now
  explicitly owns `schemas/label.fbs`, a new `schemas/worker_registry.fbs`,
  FlatBuffers generation, `include/labios/label.h`, `src/labios/label.cpp`, the
  four-payload registry codec (including epoch-safe deregistration),
  dispatcher/manager/worker integration, the scheduling and solver
  implementation, P05 parking integration, and all related unit and scheduling
  tests.
- **P09 status:** Complete for bounded correctness evidence (2026-07-15). The
  runtime uses typed scheduling batches, common feasibility/capacity filtering,
  explainable residuals, registry-v2 cutover, and coordinated
  manager/worker/dispatcher paths. Native build, 244-test unit lane, and focused
  hermetic scheduling / solver / registry tests pass. The two Section 6.2 live
  runs add only the recorded one-batch mixed correctness and manager-restart
  evidence on the single-host three-worker topology. The MCP registry-v2
  behavior gap remains explicit; Prompt 10 owns the Python parser and generated bindings, while Prompt 11 alone may change MCP behavior.
- **P10 status:** Hermetic implementation and Prompt 06 method-readiness are
  complete; Prompt 08's performance exit remains blocked. The 2026-07-30
  attempted full run retained correct rows and passed calibration but failed
  independent-replicate, actual per-label service/queue, trace-contamination,
  and complete-provenance requirements. A subsequent prerequisite slice adds
  append-only Completion observation version 1: worker ID, attempt, lifecycle
  timestamps, actual queued-to-start delay, and actual start-to-terminal service
  time are verified on decode and exposed by public C++ completion results. The
  benchmark requires these observations and keeps scheduling EWMAs separately
  named as policy inputs. Native build and 271/271 hermetic tests pass. The
  source-complete replacement method is frozen around 180 independently
  recreated runtime cells, fixed declared training, deferred verification, and
  independent-replicate analysis; it has not yet been run. The invalid historical
  statistics remain diagnostics, not a negative/null result, and no trace-guided
  performance claim is supported.
- **Worker registry recovery slice (2026-07-14):** registry construction now
  derives attachments from the constructed external `BackendRegistry`, omits
  unwired Delete/Flush and unsupported Tier 0 pipelines, and records operation
  and pipeline versions. Empty generation-zero snapshots are verified v2
  messages; manager snapshots capture rows and generation under one lock.
  Workers periodically republish full registration as an idempotent heartbeat,
  allowing manager state loss recovery; epoch-matched deregistration remains
  replacement-safe. Native build and focused worker-registry, worker-manager,
  and scheduling tests pass. No live Redis/Compose KV completion claim is made.
- **Prompt 03 reference deployment slice (2026-07-14):** the default is
  classified only as a single-host reference deployment. NATS JetStream,
  DragonflyDB, the external Redis fixture, and worker data use persistent named
  volumes; all workers share the one attachment their registry advertises as
  `Shared`; daemons have restart/readiness policy; and unsafe unauthenticated
  localhost ports are explicit. `docker compose config`, a clean image build,
  all-daemon health, the 24-operation file/SQLite golden demo, the focused
  24-operation shared-visibility test, and NATS/Dragonfly restart persistence
  passed on the implementation host. Every worker executed both reads and
  writes. Dependent daemons were restarted after plumbing because transparent
  live connection recovery is not claimed. Synchronous C++ waits now expose a
  typed timeout that explicitly leaves the label active.
- **Prompt 05 evidence-map status (2026-07-30):** Complete. The tracked
  `docs/evidence-map.md` separates implementation, hermetic, live/failure,
  public-API, CI, and performance evidence for every current 2.1 capability;
  maps historical and repository evidence separately to the four NSF
  objectives; audits CTest labels and CI reachability; ranks prompts 06–16 only
  by release-critical evidence gaps while preserving lexical order; and records
  the Section 8.2 team inputs verbatim as `needs user input`. No runtime code or
  public capability claim changed in this evidence-only session.
- **Prompt 06 trace-method status (2026-07-30):** Complete. Completion-derived
  features are attempt-scoped, bounded by a configured TTL, success-only, and
  replay-safe; wildcard NATS subscriptions now dispatch to matching callbacks.
  Profile loading fails closed and all trace parameters are explicit. The
  reduced baseline/ablation/informed Compose run passed from fresh volumes,
  including runtime policy/profile probes and the calibration gate. This
  decision freezes the Prompt 08 method; it does not claim a performance result.
- **Prompt 07 scheduling-fidelity status (2026-07-30):** Complete. Opaque
  policy strings are retained only as mirrors; typed append-only policy and
  replay evidence is authoritative. The common gate now makes readiness and
  zero-byte-capacity legality explicit, Constraint and MinMax apply soft
  locality only as a tie-break, Random excludes unavailable workers, and
  Round Robin records a stable worker-ID cursor. MinMax implements one
  proportional complete-batch distribution with byte targets when every
  reservation is known and unit-count targets otherwise; it accounts for the
  proposed unsplittable unit before crossing a target and uses deterministic
  deficit/profit/locality/worker-ID spill when no target can contain that unit.
  Its Prompt 06 cold-worker rule is bounded to one exploration selection per
  cold worker per prepared batch, after which proportional selection resumes.
  The native build and 270-test hermetic lane pass; the genuine structured
  replay covers assignments, hard exclusions, residual capacities, ranks,
  shares/profits, deterministic tie-breaks, and parked outcomes. No live or
  performance claim is made.

#### Prompt 09 — Public C++ and C API coherence

**Complete (2026-07-31).** Approved Option A: one owning `Operation` stores an
immutable label-ID set and shared operation context. Copies may call indexed
`test`, wait-for/any/all, cancel, and read concurrently. Originating `Client`
destruction releases only its ownership; retained operations keep the session
alive and do not imply cancellation. Timeout is a typed nonterminal observation,
unknown IDs are lookup failures, wait-any uses terminal time then label ID,
wait-all preserves pending/parked views in input order, cancellation exposes
Cancelled/TooLate/Terminal/Unknown, and completed reads remain repeatable during
retention.

Compatibility decisions:

- `PendingIO` remains a deprecated compatibility spelling for opaque uses of
  `Operation`; its former public mutable `pending`/`PendingLabel` representation
  is removed.
- Existing Client wait wrappers and boolean `cancel(uint64_t)` remain; new code
  uses `Operation` and typed `cancel_label`/`CancellationResult` outcomes.
- `Client::create_channel/get_channel` and workspace counterparts now return
  owning value handles. Source explicitly declaring raw pointers breaks. The
  handles do not upgrade process-local registry, ACL, or sequence state into a
  cross-process guarantee.
- Existing C error numeric values 0 through -4 remain unchanged. New timeout,
  cancellation, too-late, lookup, released-handle, protocol, and buffer-size
  categories are append-only. C statuses own operation lifetime, remain valid
  after client release, and are registry-validated before dereference. Explicit
  C member inspection and ordered-all waiting are available as
  `labios_test_label` and `labios_wait_all`; the older first-member `labios_test`
  and ordered-all `labios_wait_for` spellings remain.
- C result strings, arrays, allocated read buffers, and copied last-error data
  are caller-owned and have explicit idempotent release functions. Token
  tombstones are retained until process exit solely to prevent address reuse and
  reject copied stale handles; releasing a handle immediately releases its
  Client/Operation payload.
- URI conveniences, typed-label publication, intent/priority, and pipeline calls
  use the same normalize/validate/publish path. `Client::set_config` remains
  process-local and is not a distributed control API.

The compile-tested golden paths are `examples/native/cpp_golden.cpp` and
`examples/native/c_golden.c`; `tests/compile_doc_snippets.py` prevents SDK
signature drift. `CompletionResult::lifecycle` and its C projection expose the
catalog-authoritative Submitted through terminal/Unknown phase separately from
wait outcome. Prompt 09 adds no schema field and does not begin Prompt 10.

#### Prompt 10 — Python Label I/O SDK and registry-v2 repair

**Complete (2026-07-31).** Python now binds the owning Prompt 09 `Operation`
rather than reconstructing asynchronous state. The package exposes typed
resources and labels, URI and label-level publication, intent/priority,
source → pipeline → destination programs, completion/lifecycle/cancellation
results, deterministic waits, timeout reuse, repeatable reads, public placement
history, and active scheduler configuration. Blocking native operations release
the GIL, and retained operations keep their native session safe after the
originating Client is destroyed.

Generated FlatBuffers 24.3.25 Python sources for `worker_registry.fbs` are
versioned in the package. `parse_worker_registry_message` requires `LWR2`,
protocol version 2, matching payload kind/union, valid descriptor values, and
supports empty/nonempty snapshots with no text fallback. This slice removes the
obsolete C++ offline CSV codec as an intentional compatibility break. It does
not alter MCP behavior.

The one binding-blocker C++ addition is `Client::inspect_label(label_id)`, a
catalog-owned public API returning the admitted label and placement residual;
Python does not address DragonflyDB keys or NATS subjects. The Compose test image
and CI now carry and execute the SDK, its live pytest, and
`examples/python/golden.py`.

#### Prompt 11 — MCP core tools through Label I/O

**Complete (2026-07-31).** MCP is a public Label I/O frontend. `labios_store`,
`labios_retrieve`, and `labios_process` lower typed external resources, sealed
intent/priority/TTL, and structured registered pipelines through the packaged
Python `Client`; synchronous MCP responses project the owning `Operation`
without rebuilding asynchronous state. Timeout leaves the label active unless
`cancel_on_timeout` explicitly requests cancellation. Stable payloads distinguish
malformed request, admission failure, parked, timeout, cancelled/too-late,
execution failure, and unknown/expired completion.

Health, queue, worker score/count, active scheduler profile, lifecycle, and
placement use public Observe, Operation, and `inspect_label` APIs. Normal paths
contain no direct Redis/NATS/worker-volume adapter. The shared Prompt-10 parser
accepts only verified `LWR2` protocol-v2 snapshots when a snapshot is supplied;
worker observations normally use Observe and there is no CSV/text fallback.
The MCP image is built from the repository root, compiles the matching Python
SDK, and installs FlatBuffers 24.3.25.

Compatibility breaks are intentional: workspace key/scope store/retrieve,
workspace-tier search/version metadata, line-oriented process strings/globs,
in-process process output, direct workspace scans, and legacy worker CSV are
removed with no hidden fallback. Store/retrieve now require external backend
URIs; process requires source, destination, and structured registered stages.
`labios_knowledge` remains discoverable but returns
`PENDING_PROMPT_14`. Prompt 11 adds no backend, arbitrary tool execution,
coordination semantics, or distributed pipeline stages. The only C++ source
change exposes already-public `LabelData.ir_version` and `operation_version` in
the Python binding for public inspection; no runtime behavior changed.

#### Prompt 12 — Cross-process coordination semantic contract

**Complete (2026-07-31).** `docs/label-ir.md` Section 14 is the normative WS4
contract for named channels and workspaces. Canonical identity is the versioned
namespace/name/kind tuple; each incarnation has a monotonic epoch, and owning
handles retain a reconnect-capable session and principal independently of the
creating `Client`. Create is an atomic, idempotent create-or-open operation;
kind/options conflict deterministically, open never creates, and stale handles
cannot cross recreation.

The selected implementation model is one versioned catalog key family per
object with atomic scripts for identity, metadata, ACLs, channel sequence and
retention, durable subscriber cursors/leases/acks, workspace head/history
versions, optimistic CAS, idempotency records, TTL transitions, and bounded
cleanup. NATS channel messages become wake-up hints only; catalog data and
cursors are authoritative. Channel delivery is ordered per subscriber and at
least once, duplicates use `(epoch, sequence)`, slow consumers receive explicit
gaps, and no exactly-once claim is made. Workspace put/delete/expiry create
strictly increasing entry revisions and tombstones; Exact/MustExist/
MustNotExist conflicts are linearizable and no arbitrary multi-entry transaction
is added.

Owner, Admin, Writer, and Reader ACLs are checked atomically on every operation.
A production principal must come from transport-authenticated identity. The
single-host reference deployment can enforce only spoofable logical principal
comparisons because clients hold unauthenticated plumbing access; this is not a
production tenant-security claim. Existing PID app IDs remain compatibility
identifiers only.

Direct catalog coordination is allowed solely behind public channel/workspace
handles as the specified transient primitive. A Label carrying
`ChannelResource` or `WorkspaceResource` uses the same identity, epoch/version,
ACL, sequence/CAS, and error semantics but still traverses ordinary Label I/O;
other backend I/O cannot use the coordination catalog as a bypass. Legacy
process-local keys are not silently imported. Section 14 fixes stable errors,
public C++/Python API shape, state machines, rolling compatibility, observability,
recovery bounds, invariants, and the independent-process acceptance matrix that
Prompt 13 must implement. Prompt 12 changes documentation only.

## 12. Immediate integration steps

1. Collect the project team's publication and artifact list for the annual
   report evidence map (§8.2).
2. Execute the numbered files directly under `.planning/prompts/` in lexical
   order, starting with `01-durable-ingress-and-parking.md`.
3. Each completed prompt updates the evidence (§6) and decision (§11) state in
   this document before a later prompt proceeds.
4. Implement, verify, and review one slice before selecting the next.

## 13. Archive policy

The following documents were moved to `.planning/_archived/pre-2.1/` and are
retained only as history:

- `LABIOS-2.0.md`
- `LABIOS-SPEC.md`
- `LABIOS_2.0_ENGINEERING_PLAN.md`
- `2026-04-06-agent-integration-design.md`
- `CODEX-ANALYSIS.md`
- `direction.md`
- `implementation-status.md`

Their durable ideas have been incorporated here. Historical details remain
available for provenance, but they do not override LABIOS 2.1 decisions.
