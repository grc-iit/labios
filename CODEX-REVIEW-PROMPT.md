# Deep Iterative Review and Hardening

You are reviewing LABIOS 2.0, the first agent I/O runtime. US Patent 11,630,834 B2, NSF Award #2331480, HPDC'19 Best Paper Nominee.

## Read These Files First (In Order)

1. `CLAUDE.md` — project conventions, architectural boundaries, current status
2. `LABIOS-SPEC.md` — the definitive specification (15 sections, the design authority)
3. `LABIOS-2.0.md` — constitutional document (established the rewrite, lists what was lost from the paper)
4. `docs/superpowers/specs/2026-04-06-agent-integration-design.md` — agent integration design (MCP server, memory hierarchy, multi-backend routing)
5. `docs/architecture.md` — implementation reference

## Critical Architectural Boundary

LABIOS internal plumbing (DragonflyDB warehouse for staging, NATS for label routing) is completely separate from external backends (user's storage systems). Backends are thin adapters. All intelligence lives in the runtime. See the "Architectural Boundaries" section in CLAUDE.md.

## Your Mission

Perform an iterative deep review cycle. Each iteration:

### Iteration 1: Code Quality and Correctness
- Read every header in `include/labios/` and every implementation in `src/labios/`
- Check for: undefined behavior, data races, resource leaks, incorrect error handling
- Check for: concepts satisfied correctly, move semantics used properly, RAII everywhere
- Check for: thread safety (all shared mutable state protected by mutex or atomic)
- Fix everything you find. Build and test after each fix.

### Iteration 2: Spec Compliance
- Read LABIOS-SPEC.md section by section
- For each spec requirement, verify the implementation matches semantics
- Check: does the shuffler actually write aggregation/supertask_id/dependencies onto labels?
- Check: does the scheduler write routing decisions onto labels?
- Check: do workers write hops/timestamps/status onto labels?
- Check: do continuations fire after every completion path?
- Fix any deviations. Build and test.

### Iteration 3: Test Coverage
- Read every test in `tests/unit/`
- Identify code paths with no test coverage
- Add tests for: edge cases (empty labels, zero-length data, destroyed channels, revoked workspace access)
- Add tests for: error paths (backend failures, NATS disconnects, Redis timeouts)
- Add tests for: the new backends (KVBackend, SQLiteBackend) 
- Build and test.

### Iteration 4: Performance
- Read the serialization benchmarks in `tests/benchmark/`
- Check for unnecessary allocations in hot paths (label serialization, shuffler batch processing, solver assignment)
- Check for unnecessary string copies where string_view would work
- Check for lock contention (worker manager mutex, channel registry, workspace registry)
- Fix and benchmark.

### Iteration 5: Integration Coherence
- Read the agent integration design spec
- Verify the Python SDK bindings match the C++ client API
- Verify the POSIX intercept covers all documented entry points
- Verify docker-compose.yml is consistent with the architecture (separate user KV backend from internal warehouse)
- Verify CMakeLists.txt builds cleanly on a fresh checkout

## Build and Test Commands

```bash
cmake --preset dev
cmake --build build/dev -j$(nproc)
ctest --test-dir build/dev --output-on-failure
```

## Rules

- You are on branch `review/codex-deep-review` in a git worktree at `/home/akougkas/projects/labios-codex-review`
- Conventional commits: fix(), perf(), refactor(), test()
- All tests must pass after every change
- Commit after each meaningful fix or group of related fixes
- Do NOT change the spec or design docs. Only fix implementation code and tests.
- If you find something that looks like a design decision rather than a bug, leave a comment in the code but don't change the behavior
