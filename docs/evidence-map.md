# LABIOS 2.1 evidence map

This map records what each LABIOS 2.1 capability is evidenced by as of commit
`6a68b4f`. It distinguishes implementation artifacts from tests and experiments.
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
| Completion inspection, waiting, timeout, and cancellation | `include/labios/label_manager.h`, `src/labios/label_manager.cpp`, catalog CAS transitions | Client completion ordering, timeout, and state tests; `Cancellation and worker claim linearize at the catalog status` is correctly labeled smoke because it needs Redis | Live scenario 7 covers parked cancellation, a cancellation-winning Scheduled race, and an execution-winning TooLate race | `test`, `wait_for`, `wait_any`, `wait_all`, and `cancel` | Hermetic completion cases in unit CI; races are not CI-reachable | Latency/performance unproven |
| Worker delivery deduplication | Stable completion record and explicit idempotent reclaim gate in worker/catalog | Catalog claim/cancel test requires Redis and is smoke, not hermetic | Live scenario 2 kills the worker after claim and before a direct staged core-write effect, then checks completion and unchanged mtime after replay | Label is submitted and waited through the C++ client; replay injection is harness-internal | Not CI-reachable | Only the reproduced pre-effect direct staged core-write window is proven; arbitrary mid-effect or non-idempotent recovery is unproven |
| One-worker source → pipeline → destination | backend registry, SDS executor, Tier-1 worker path | SDS repository/executor unit tests | `Source URI pipeline writes to a different SQLite backend` and live scenario 1 verify exact output bytes | `execute_pipeline`, `wait`, and `read_from` | Compose golden path builds the path; CI directly runs the broader `[deployment]` data-path case, not the named pipeline case | No ETL or pipeline speedup claim |
| File and SQLite backends | POSIX and SQLite adapters plus registry | Adapter CRUD, path-containment, zero-length, and registry tests | Demo and `[deployment]` test perform 24 writes/read-backs over shared file and SQLite attachments | `write_to` and `read_from` | Compose golden path | Throughput unproven |
| Optional external user-Redis backend | `KVBackend`; worker attachment comes from the external-backend registry, not DragonflyDB plumbing | Registry construction tests; KV tests are live/smoke because they require Redis | `Configured external KV backend completes a label` targets the separate Redis fixture | C++ client `kv://` submission | Redis fixture is started in Compose CI, but the KV completion case is not selected there | Throughput and production deployment unproven |
| Batch scheduling and common feasibility | scheduling layer, dispatcher batch preparation, registry v2 | 13 Section 9.7 scheduling cases, solver cases, exact 8 MiB paper trace | Live scenario 6 covers one mixed batch, dependency deferral, Composite, unknown demand, and 16/24/32 KiB capacity constraints | Work is submitted through the C++ client; decision inspection is harness-internal | Hermetic cases in unit CI; mixed live batch is not CI-reachable | Policy performance remains unproven pending prompts 06–08 |
| Explainable scheduling residual | append-only scheduling decision history in `ScoreSnapshot` | Residual codec and replay cases in scheduling tests | Live scenario 6 records decision snapshots | Public completion does not yet expose the full explanation; catalog inspection does | Hermetic codec/replay only | No performance result |
| Registry v2 and replacement-safe epochs | `worker_registry.fbs`, manager/worker protocol | Malformed/version/capability/epoch cases | Live scenario 5 restarts the manager, observes reconstruction, restarts a worker, and rejects stale deregistration | Manager request path is runtime-internal; C++ test driver decodes the public control reply | Hermetic codec/state cases in unit CI; manager-loss injection is not CI-reachable | No performance result; Python/MCP registry-v2 support is unproven until prompt 10 |
| Channels and workspaces | `channel.cpp`, `workspace.cpp` | Some object-level logic exists, but the CTest targets are labeled smoke and use Redis/NATS | No independent-producer/consumer multi-process proof | C++ client methods exist but resolve process-local registries | Not selected by current CI | Cross-process identity, ACL, and channel sequence semantics are unproven; prompts 12–13 own this gap |
| Observability | dispatcher Observe handler and telemetry | Query formatting/component cases are not hermetic in current classification | Compose smoke demonstrates system health; other endpoints have component/live fixtures | `Client::observe` | Demo/health indirectly in Compose CI; endpoint matrix not directly selected | No performance result |
| Elasticity | decision engine, Docker client, per-tier orchestrator | Decision and mock-runtime tests in unit CI | No enabled-default, leader-election, or safe live provisioning experiment | Configuration surface exists; disabled by default | Unit CI only | Live elasticity behavior and benefit are unproven |
| POSIX intercept | `src/drivers/posix_intercept.cpp` and adapter | FD-table cases are hermetic | The intercept binary is built, but its integration executable is unlabeled and not run by CI | LD_PRELOAD surface, restricted to configured LABIOS prefixes | Build-only | Transparent arbitrary-repository POSIX compatibility and performance are unproven |
| Python SDK | `src/python/labios_module.cpp` | `tests/python/test_labios.py` is separate pytest interface evidence, not part of CTest | No live parity run is retained | Python bindings expose a subset | Current CI installs no Python/pybind11 dependency and does not run pytest | Full C++ parity, FlatBuffers support, registry-v2 bindings, and performance are unproven; prompt 10 owns the gap |
| MCP tool server | `mcp/labios_mcp` five-tool prototype | `mcp/tests/test_server.py` is mock/interface evidence | No label-ingress live proof; current server directly accesses DragonflyDB and a worker volume | MCP protocol surface exists | Container health only; pytest is not run by CI | Dispatcher/worker traversal and coding-agent benefit are unproven; prompts 11 and 14 own the gaps |
| Single-host reference deployment | Dockerfile, `docker-compose.yml`, deployment docs | Compose model can be validated without starting services | Fresh topology health, 3-case smoke, 24-operation demo, persistence restart, and shared-visibility validation; 2026-07-30 Step-0 rerun succeeded | Demo and data-path use the public C++ client | `compose-golden-path` executes health, demo, shared visibility, worker distribution, and plumbing restart | This is not production, multi-node, secure, or a transparent reconnect guarantee |
| Agent frontend family | MCP prototype only | Mock/interface MCP tests | No label-ingress equivalence evidence | MCP surface exists | Health only | Unproven until prompts 10–11 and 14 |
| Scientific frontend family | No 2.1 frontend implementation yet | None | None | None | None | Unproven; prompt 15 owns this release-critical gap |

## Test-label and CI audit

Fresh CTest discovery at `6a68b4f` contains 407 tests:

| CTest label | Count | Classification |
|---|---:|---|
| `unit` | 257 | Hermetic lane. `ctest --test-dir build/dev -L unit` passed 257/257 on 2026-07-30 and is run by CI. |
| `smoke` | 91 | Infrastructure-dependent. Includes Redis/NATS/catalog/content/data-path/channel/workspace/observability cases. It must not be described as hermetic. |
| `integration` | 3 | Cross-service behavior; requires a live topology. |
| `kernel` | 15 | Kernel replay/characterization cases. These are not reproductions of HPDC'19 results. |
| `bench` | 41 | Mixed microbenchmark/component/live-benchmark registrations. A CTest label does not establish a fair baseline or a performance claim. |

Additional executables (`labios-intercept-test`,
`labios-elastic-flood-test`, and `labios-live-correctness-driver`) are not
registered as ordinary CTest cases. The live driver is intentionally controlled
by the failure-injection shell harness. The other unlabeled executables are
build-only unless invoked explicitly.

Current CI reaches:

1. the 257-test hermetic lane;
2. Compose model validation and clean image build;
3. health of NATS, DragonflyDB, the separate external Redis fixture, manager,
   dispatcher, three workers, and MCP container;
4. the public-client demo;
5. the `[deployment]` shared file/SQLite test;
6. round-robin evidence in every worker log; and
7. NATS/Dragonfly persistence across explicit restart followed by daemon
   restart and another demo.

Current CI does **not** reach the full 91-test smoke lane, the three integration
tests, kernel cases, benchmarks, the failure-injection harness, Python pytest,
MCP pytest, POSIX-intercept execution, external-KV completion, live elasticity,
or multi-process coordination. Those remain local/manual evidence or unproven.

An indiscriminate `ctest` run is not a hermetic gate. Configuration tests are
intentionally sensitive to globally exported runtime endpoint variables, and
legacy benchmark cases contain host-sensitive throughput thresholds. Release
verification must invoke documented lanes with scoped environments and must not
reinterpret a benchmark threshold as semantic correctness.

## Four NSF objectives without evidence mixing

| NSF objective | Prior publication evidence | Evidence produced by this repository | Missing 2.1 evidence |
|---|---|---|---|
| I/O Integration | HPDC'19 architecture and its scientific/POSIX adapters | Versioned Label I/O, typed-resource normalization, C++/C surfaces, POSIX-prefix prototype, file/SQLite/KV adapters, and the current capability matrix | Two semantically equivalent frontend families; Python parity; MCP label ingress |
| I/O Asynchronicity | HPDC'19 reports up to 16x CM1 I/O improvement and 40% execution-time reduction | Durable admission/recovery, catalog-backed waits, timeout semantics, cancellation races, and the bounded worker/dispatcher loss evidence listed above | Broader crash windows, uncertain non-idempotent outcomes, and multi-node behavior |
| Storage Dynamic Deployment | HPDC'19 worker scoring and bucket sorting; Luxio-related evidence requires team-supplied bibliography | Registry v2, common feasibility, four policy families, replayable decisions, paper trace, and one live mixed batch | Prompt 08's completion-verified baseline/informed distributions; live elasticity remains unproven |
| Storage Programmability | HPDC'19 reports 17x Montage I/O improvement and 40–60% execution-time reduction; LabStor continuity requires precise external citation | Eleven byte transforms and live one-worker file → pipeline → SQLite correctness | Scientific frontend/equivalence and any current-repository performance result |

The historical values above are prior-publication results. This repository has
not reproduced the 16x CM1, 6x HACC, 17x Montage, or 40–60% figures.

## Release-critical gaps and prompt order

The lexical prompt order remains authoritative. The evidence gaps explain its
release criticality; this is not a second roadmap:

1. **06** freezes trace accounting and method readiness.
2. **07** closes MinMax determinism and policy correctness.
3. **08** is the pending WS3 live performance exit. A negative or null result is
   acceptable; a missing distribution is not.
4. **09–10** make documented C++/C examples and Python/registry-v2 surfaces
   compile and cohere.
5. **11** closes the MCP runtime-bypass claim.
6. **12–13** specify, then implement, cross-process coordination.
7. **14** builds MCP workspace/knowledge behavior on that runtime.
8. **15** supplies the second frontend family and equivalence evidence.
9. **16** audits every public claim and produces release evidence from a clean
   tree and fresh volumes.

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
