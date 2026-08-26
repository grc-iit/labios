# Changelog

All notable changes to LABIOS are documented here. The project uses Semantic
Versioning for release tags; Label I/O IR and worker-registry protocol versions
are independent wire-level versions.

## [2.1.0-rc.1] - 2026-08-26

First release candidate for the LABIOS 2.1 generation.

### Added

- Versioned Label I/O IR v1 with FlatBuffers verification, deterministic
  admission, typed resources, staged-input bindings, and declared dependencies.
- Durable NATS JetStream label delivery with explicit acknowledgements,
  redelivery, catalog-backed recovery, parking, and bounded retry metadata.
- End-to-end source → registered pipeline → destination execution across file,
  SQLite, and optional external Redis KV adapters.
- Typed scheduling batches, policy-independent feasibility checks, registry-v2
  worker capabilities, and replayable placement decision history.
- Owning C++ `Operation` and C status APIs with typed timeout, cancellation,
  repeatable read retrieval, lifecycle inspection, and explicit C resource
  release functions.
- Python Label I/O SDK with generated registry-v2 FlatBuffers bindings.
- MCP store, retrieve, process, and observe tools that use the public Python
  client and normal dispatcher/worker path.
- Single-host Docker Compose reference topology with three Tier-1 workers,
  persistent JetStream/catalog state, shared file/SQLite storage, and a separate
  user-Redis backend fixture.

### Changed

- Label transport now uses JetStream; core NATS remains control-plane plumbing.
- Default Compose workers are Tier 1 so registered pipelines execute in the
  verified reference topology.
- `PendingIO` is now a compatibility spelling for the owning `Operation`; its
  old mutable public representation was removed.
- Channel and workspace creation returns owning value handles instead of
  registry-owned raw pointers.
- MCP requests require external backend URIs and structured registered pipeline
  stages; old direct workspace, Redis, NATS, and worker-volume paths were
  removed.
- Registry v2 requires verified `LWR2` FlatBuffers messages; CSV/text fallback
  was removed.
- Unknown resource schemes fail deterministic admission instead of reaching a
  worker as malformed work.

### Fixed

- Staged split writes retain each content binding and producer-declared order;
  the shuffler no longer merges independently staged objects into an invalid
  aggregate.
- Strict project warnings no longer turn diagnostics from fetched third-party
  dependencies into LABIOS build failures.
- The release-build C API link test now executes checks even when `NDEBUG` is
  defined.

### Compatibility and limitations

- This is a source release candidate, not the final 2.1.0 release.
- The supported deployment is an unauthenticated, single-host reference
  topology for development and evaluation—not production or verified
  multi-node operation.
- Channel/workspace registry identity, ACLs, and channel sequence allocation
  remain process-local; cross-process coordination is not claimed.
- `labios_knowledge` returns
  `WORKSPACE_KNOWLEDGE_UNAVAILABLE`; no direct-store fallback exists.
- Tier 2 has metadata/scoring but no distinct reasoning behavior. Elasticity is
  disabled by default and has no live release claim.
- The frozen trace-guided experiment has not produced a valid independent run,
  so this repository makes no new scheduling-performance claim.
- A scientific frontend and cross-frontend equivalence evidence remain pending
  before final 2.1.0.

[2.1.0-rc.1]: https://github.com/grc-iit/labios/releases/tag/v2.1.0-rc.1
