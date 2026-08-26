# LABIOS 2.1 evidence map

This map records what each LABIOS 2.1 capability is evidenced by through the
2.1.0-rc.1 release rehearsal. It distinguishes implementation artifacts from
tests and experiments.
An implementation path is not, by itself, evidence that the path works end to
end.

Evidence classes used below:

- **Implementation** — the relevant production artifact exists.
- **Hermetic** — a test runs without NATS, Redis/DragonflyDB, Docker, persistent
  volumes, wall-clock coordination, or another process.
- **Live/failure** — a test uses the real services, process boundaries, durable
  state, or injected process loss.
- **Public API** — the behavior is reached through the supported client surface,
  rather than a component-internal call.
- **CI** — the current GitHub Actions workflow executes the evidence.
- **Performance** — completion-verified measurements against a semantically fair
  baseline. A blank or `unproven` entry means no result may be claimed.

## Current capability claims

| Capability | Implementation artifact | Hermetic evidence | Live or failure evidence | Public-API evidence | CI reachability | Performance evidence / remaining gap |
|---|---|---|---|---|---|---|
| Versioned Label I/O and deterministic admission | `schemas/label.fbs`, `src/labios/label.cpp`, `src/labios/client.cpp` | `Serialized labels carry the current IR version`; `FlatBuffers rejects malformed label and completion buffers`; P03 normalization/admission cases in `tests/unit/label_test.cpp` | Malformed durable recovery records are isolated in live scenario 3/6 and `durable_catalog_test.cpp` | `Client::publish` performs preflight admission | Unit CI | No performance claim; complete sealed-origin enforcement remains unproven |
| Typed resources and compatibility normalization | `ResourceRef` codec/normalizer in `label.cpp` | P03 URI, Pointer, conflict, resource-family, and round-trip tests | File, SQLite, and external-KV executable shapes are exercised by live/Compose tests | `write_to`, `read_from`, `execute_pipeline` | Unit plus Compose golden path for file/SQLite | Lossless executable support for every semantic ResourceRef family is unproven |
| Durable label ingress and recovery | JetStream transport plus catalog admission/recovery in `transport/nats.cpp`, `catalog_manager.cpp`, dispatcher | Durable callback and malformed-input unit cases | `tests/live/ws2_ws3_correctness.sh` scenario 3 kills the dispatcher after durable queued admission; historical retained runs `20260715T160503Z-16d4f13-pass1` and `20260715T160847Z-16d4f13-pass2`; triage rerun on 2026-07-30 passed but its scratch artifact was not retained | Submission uses the C++ client | Compose CI checks persistence restart, but does not inject dispatcher loss | Catalog-loss recovery, multi-node recovery, and exactly-once execution are unproven |
| Completion inspection, waiting, timeout, cancellation, and execution observations | Owning `Operation`, synchronized completion hints, catalog CAS transitions, append-only Completion fields, and C handle registry | Five operation lifetime/concurrency/reuse/read tests, C stale/double-release checks, documentation compile test, catalog-lifecycle projection test, and existing Completion observation derivation | Prompt 09's three live C lifetime cases cover client destruction, timeout reuse, and concurrent wait/cancel/release; prior live scenario 7 covers cancellation race outcomes | C++ `Operation::test/wait_for/wait_any/wait_all/cancel/read`; C `labios_test/labios_test_label/labios_wait_for/labios_wait_all/labios_wait_any/labios_cancel/labios_wait_read_alloc`; results expose catalog lifecycle, stable category, and executor observations | New hermetic cases are unit-CI reachable; live C lifetime and prior races are not CI-reachable | Latency/performance unproven |
| Worker delivery deduplication | Stable completion record and explicit idempotent reclaim gate in worker/catalog | Catalog claim/cancel test requires Redis and is smoke, not hermetic | Live scenario 2 kills the worker after claim and before a direct staged core-write effect, then checks completion and unchanged mtime after replay | Label is submitted and waited through the C++ client; replay injection is harness-internal | Not CI-reachable | Only the reproduced pre-effect direct staged core-write window is proven; arbitrary mid-effect or non-idempotent recovery is unproven |
| One-worker source → pipeline → destination | backend registry, SDS executor, Tier-1 worker path | SDS repository/executor unit tests | `Source URI pipeline writes to a different SQLite backend` and live scenario 1 verify exact output bytes | `execute_pipeline`, `wait`, and `read_from` | Compose golden path builds the path; CI directly runs the broader `[deployment]` data-path case, not the named pipeline case | No ETL or pipeline speedup claim |
| File and SQLite backends | POSIX and SQLite adapters plus registry | Adapter CRUD, path-containment, zero-length, and registry tests | Demo and `[deployment]` test perform 24 writes/read-backs over shared file and SQLite attachments | `write_to` and `read_from` | Compose golden path | Throughput unproven |
| Optional external user-Redis backend | `KVBackend`; worker attachment comes from the external-backend registry, not DragonflyDB plumbing | Registry construction tests; KV tests are live/smoke because they require Redis | `Configured external KV backend completes a label` targets the separate Redis fixture | C++ client `kv://` submission | Redis fixture is started in Compose CI, but the KV completion case is not selected there | Throughput and production deployment unproven |
| Batch scheduling and common feasibility | scheduling layer, dispatcher batch preparation, registry v2 | Section 9.7 scheduling cases, solver cases, exact 8 MiB paper trace, structured deterministic replay, and actual Completion observation derivation | Live scenario 6 covers one mixed batch, dependency deferral, Composite, unknown demand, and 16/24/32 KiB capacity constraints; Prompt 08's attempted run retained 1,260 verified measured completions but failed the independence and measurement contract | Work is submitted through the C++ client; the current benchmark reads actual service/queue observations through public completion results while decision inspection remains catalog-backed | Hermetic cases in unit CI; mixed live batch and experiment are not CI-reachable | Policy performance remains unproven; the source-complete 180-cell independent-replicate method is frozen but has not produced a valid run |
| Explainable scheduling residual | append-only scheduling decision history in `ScoreSnapshot` | Residual codec and replay cases in scheduling tests; Python typed placement-history surface | Live scenario 6 records decision snapshots; Python live pytest inspects it | C++/Python `Client::inspect_label` returns the catalog-owned label residual without exposing keys | Hermetic codec/replay and Python CI; live inspection in Compose CI | No performance result |
| Registry v2 and replacement-safe epochs | `worker_registry.fbs`, manager/worker protocol, generated Python bindings, shared `registry_v2.py` parser | Native, Python, and MCP malformed/truncated/LWR2/version/kind/union/empty/nonempty/duplicate cases | Live scenario 5 restarts the manager, observes reconstruction, restarts a worker, and rejects stale deregistration | MCP worker observations use public Observe; supplied snapshots use the shared verified parser only, with no CSV fallback | Native/Python/MCP hermetic cases run in CI; manager-loss injection is not CI-reachable | No performance result |
| Channels and workspaces | Owning `ChannelHandle`/`WorkspaceHandle` over shared process-local registries | Object-level targets remain smoke because they use Redis/NATS; public raw-pointer lifetime hazards are removed by construction | No independent-producer/consumer multi-process proof | C++ handles retain required session/object lifetime and fail predictably when empty/destroyed | Not selected by current CI | Registry identity, ACL, and sequence allocation remain process-local; cross-process semantics are unproven |
| Observability | dispatcher Observe handler and telemetry | MCP fake-client cases prove health/worker/config routing uses public Observe and label lifecycle/residual uses public inspection | Compose MCP live pytest checks health, worker count/scores, active policy/profile, versioned typed label, and placement history | `Client::observe`, `Client::inspect_label`, and owning `Operation::test` | MCP hermetic and live endpoint matrix run in CI | No performance result |
| Elasticity | decision engine, Docker client, per-tier orchestrator | Decision and mock-runtime tests in unit CI | No enabled-default, leader-election, or safe live provisioning experiment | Configuration surface exists; disabled by default | Unit CI only | Live elasticity behavior and benefit are unproven |
| POSIX intercept | `src/drivers/posix_intercept.cpp` and adapter | FD-table cases are hermetic | The intercept binary is built, but its integration executable is unlabeled and not run by CI | LD_PRELOAD surface, restricted to configured LABIOS prefixes | Build-only | Transparent arbitrary-repository POSIX compatibility and performance are unproven |
| Python SDK | pybind11 owning `Operation`, typed labels/resources, package and generated registry bindings | 10 hermetic pytest cases cover imports, enums, resources/labels, exceptions, invalid handles, and registry verification | 4 opt-in Compose pytest cases plus `examples/python/golden.py` cover completion, timeout reuse, cancellation race, Client destruction, repeatable reads, label publication, placement inspection, and exact pipeline bytes | All runtime work uses Client/Operation/Observe/inspect_label; no internal keys or subjects | Hermetic pytest and live Compose pytest/golden are CI-reachable; SDK is packaged in the test image | Python performance and channel/workspace parity are not claimed |
| MCP Label I/O frontend | `mcp/labios_mcp`, packaged Python SDK/FlatBuffers image, five stable tool names | 17 hermetic cases cover schemas/imports, typed lowering, sealed field preservation, stable errors, timeout/cancellation/parking, malformed requests, registry skew, unsupported transforms, and absence of direct adapters | 4 Compose cases plus `examples/mcp/golden.py` verify exact store/retrieve and pipeline destination bytes, timeout/cancellation projection, typed label/placement inspection, and public health/worker/config observations | Core tools use Python `Client`, typed `LabelParams`, owning `Operation`, Observe, and inspection only | Hermetic and live MCP pytest plus stdio golden and packaged import run in CI | Coding-agent benefit/performance unproven; workspace knowledge remains Prompt 14 |
| Single-host reference deployment | Dockerfile, `docker-compose.yml`, deployment docs | Compose model can be validated without starting services | Fresh topology health, 3-case smoke, 24-operation demo, persistence restart, and shared-visibility validation; 2026-07-30 Step-0 rerun succeeded | Demo and data-path use the public C++ client | `compose-golden-path` executes health, demo, shared visibility, worker distribution, and plumbing restart | This is not production, multi-node, secure, or a transparent reconnect guarantee |
| Agent frontend family | MCP core I/O compiles to versioned typed Label I/O | Hermetic MCP lowering and rejection tests | Compose exact-byte store/retrieve/pipeline and stdio MCP golden | Public MCP protocol over the Python SDK | Hermetic and live MCP paths are CI-reachable | Core I/O ingress proven; cross-process workspace knowledge remains Prompt 14 and cross-frontend equivalence remains Prompt 15 |
| Scientific frontend family | No 2.1 frontend implementation yet | None | None | None | None | Unproven; prompt 15 owns this release-critical gap |

## Test-label and CI audit

The 2.1.0-rc.1 release-audit build discovers 433 CTest tests:

| CTest label | Count | Classification |
|---|---:|---|
| `unit` | 278 | Hermetic lane. Release rehearsal passed 278/278, including staged-write binding/order, five operation lifetime cases, C stale/double-release checks, and exact compilation of both native SDK snippets. |
| `smoke` | 96 | Infrastructure-dependent. Three new public-C cases cover client destruction, timeout reuse, and concurrent wait/cancel/release; two additional cases verify catalog lifecycle projection and public parked reason/retry metadata. |
| `integration` | 3 | Cross-service behavior; requires a live topology. |
| `kernel` | 15 | Kernel replay/characterization cases. These are not reproductions of HPDC'19 results. |
| `bench` | 41 | Mixed microbenchmark/component/live-benchmark registrations. A CTest label does not establish a fair baseline or a performance claim. |

The six hermetic C++/C handle cases and three live C lifetime cases also pass an
ASan+UBSan build with leak detection. UBSan suppressions are limited to two
known cnats null/zero-byte nonnull-attribute reports in
`glib_dispatch_pool.c` and `msg.c`, recorded in `tests/ubsan.supp`; no LABIOS
source is suppressed. TSan did not
run because its instrumented build-time `flatc` failed with an unexpected memory
mapping, so no thread-sanitizer claim is made. For Prompt 11, UBSan passes the
combined 27 Python/MCP hermetic cases. ASan passes all 17 MCP boundary cases
with leak detection disabled. Leak-enabled pytest reports CPython process-exit
allocations, and the full Python exception-path lane hits an ASan
`__cxa_throw` interceptor check; neither is promoted to a LABIOS memory defect,
but no leak-enabled/full-exception ASan claim is made.

The 2026-08-26 release rehearsal also passed all 96 smoke cases using scoped
execution: runtime-facing cases ran with the healthy topology, while the four
catalog recovery cases ran with the dispatcher stopped so it could not consume
their deliberately queued fixtures. The rehearsal found and fixed an unsafe
legacy aggregation path for staged split writes; the exact 3 MiB split
write/read case now verifies all child completions and reconstructed bytes.
The three integration cases pass. Optional characterization is not release-
clean: the 1000-label CM1 kernel case and 1000-file legacy benchmark timed out,
and the host-sensitive 100 MiB benchmark missed its fixed 15 MiB/s thresholds
(approximately 2.16 MiB/s write and 1.85 MiB/s read). These results support no
performance conclusion and remain reported failures, not suppressed evidence.

Additional executables (`labios-intercept-test`,
`labios-elastic-flood-test`, and `labios-live-correctness-driver`) are not
registered as ordinary CTest cases. The live driver is intentionally controlled
by the failure-injection shell harness. The other unlabeled executables are
build-only unless invoked explicitly.

Current CI reaches:

1. the 278-test native hermetic lane, 10 hermetic Python pytest cases, and 17 hermetic MCP pytest cases;
2. Compose model validation and clean image build;
3. health of NATS, DragonflyDB, the separate external Redis fixture, manager,
   dispatcher, three workers, and MCP container;
4. the public-client demo;
5. the `[deployment]` shared file/SQLite test;
6. round-robin evidence in every worker log; and
7. NATS/Dragonfly persistence across explicit restart followed by daemon
   restart and another demo;
8. four live Python SDK pytest cases in the packaged test image;
9. the documented Python golden example with exact pipeline-byte verification;
10. four live MCP pytest cases in the self-contained MCP image; and
11. packaged SDK/FlatBuffers imports plus the stdio MCP golden with exact store/retrieve and pipeline bytes.

Current CI does **not** reach the full 96-test smoke lane, the three integration
tests, kernel cases, benchmarks, the failure-injection harness,
POSIX-intercept execution, external-KV completion, live elasticity, or
multi-process coordination. Those remain local/manual evidence or unproven.

An indiscriminate `ctest` run is not a hermetic gate. Configuration tests are
intentionally sensitive to globally exported runtime endpoint variables, and
legacy benchmark cases contain host-sensitive throughput thresholds. Release
verification must invoke documented lanes with scoped environments and must not
reinterpret a benchmark threshold as semantic correctness.

## Four NSF objectives without evidence mixing

| NSF objective | Prior publication evidence | Evidence produced by this repository | Missing 2.1 evidence |
|---|---|---|---|
| I/O Integration | HPDC'19 architecture and its scientific/POSIX adapters | Versioned Label I/O, typed-resource normalization, C++/C/Python surfaces, file/SQLite/KV adapters, and MCP core Label I/O ingress | A scientific frontend and explicit cross-frontend equivalence; MCP workspace knowledge remains Prompt 14 |
| I/O Asynchronicity | HPDC'19 reports up to 16x CM1 I/O improvement and 40% execution-time reduction | Durable admission/recovery, catalog-backed waits, timeout semantics, cancellation races, and the bounded worker/dispatcher loss evidence listed above | Broader crash windows, uncertain non-idempotent outcomes, and multi-node behavior |
| Storage Dynamic Deployment | HPDC'19 worker scoring and bucket sorting; Luxio-related evidence requires team-supplied bibliography | Registry v2, common feasibility, four policy families, replayable decisions, paper trace, and one live mixed batch | A valid Prompt 08 completion/service/queue distribution with independent repetitions; live elasticity remains unproven |
| Storage Programmability | HPDC'19 reports 17x Montage I/O improvement and 40–60% execution-time reduction; LabStor continuity requires precise external citation | Eleven byte transforms and live one-worker file → pipeline → SQLite correctness | Scientific frontend/equivalence and any current-repository performance result |

The historical values above are prior-publication results. This repository has
not reproduced the 16x CM1, 6x HACC, 17x Montage, or 40–60% figures.

## Release-critical gaps and active order

Prompts 01–12 are complete at the bounded evidence recorded above, except that
Prompt 08 still lacks a valid experiment result. The authority in
`.planning/LABIOS-2.1.md` defines this serialized RC-to-final order:

1. **13** implements the Prompt-12 cross-process coordination contract.
2. **14** builds MCP workspace knowledge only on that proven public runtime.
3. **15** supplies the scientific frontend and machine-checked cross-frontend
   equivalence evidence.
4. **08** runs the already frozen independent-replicate experiment against the
   resulting feature-complete candidate. The retained earlier attempt remains
   invalid evidence; no method redesign is permitted after observing results.
5. **16** audits every public claim and produces final release evidence from a
   clean tree and fresh volumes.

This section ranks evidence gaps; it does not override the planning authority.

## Team inputs still required

The following list is ready to send verbatim and is not derivable from this
repository:

- **needs user input:** Publication list attributable to NSF Award 2313154
  since 2023, including student theses and journal follow-ups.
- **needs user input:** Software release artifacts: version tags, DOIs, and the
  LoC/feature/test metrics the proposal promised to track.
- **needs user input:** Education and broader-impact outcomes: whether the
  proposed CS590 storage course ran, REU/IPRO participation, mentoring, and
  outreach counts.
- **needs user input:** Testbed results beyond HPDC'19, including Chameleon,
  institutional-cluster, DoE, or TACC systems, if any exist for the reporting
  period.
- **needs user input:** Confirmation from the award record that the NSF
  no-cost-extension end date is July 31, 2027.
- **needs user input:** Primary-source confirmation that LABIOS received the
  HPDC'19 Karsten Schwan Best Paper Award. Repository wording has been corrected
  from “Nominee,” but the award record is still required for the annual report.
- **needs user input:** Attribution boundaries for related work: Hermes
  (OAC-1835764) and ChronoLog (CSSI-2104013) results must not be reported as
  LABIOS/Award-2313154 outcomes.
