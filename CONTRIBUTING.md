# Contributing to LABIOS

LABIOS is an active research project at Illinois Institute of Technology.
Contributions are welcome from team members and collaborators.

## Setup

```bash
git clone https://github.com/grc-iit/labios.git
cd labios
docker compose up -d --build --wait
```

See [docs/getting-started.md](docs/getting-started.md) for the full walkthrough.

## Planning and release status

Start with the [planning index](.planning/README.md). The single design and
engineering-truth authority is [.planning/LABIOS-2.1.md](.planning/LABIOS-2.1.md);
the active prompt files are bounded execution checklists, not competing plans.
The [2.1.0-rc.1 release notes](docs/releases/2.1.0-rc.1.md) and
[evidence map](docs/evidence-map.md) define the current verified claims and
limitations.

Do not add a backend, adapter, scheduler, or deployment target merely because it
appeared in an older plan. Current work prioritizes the active 2.1 release gates
and complete public paths over new surface area.

## Build and Test

```bash
# Docker (recommended)
docker compose run --rm --build test
docker compose run --rm --build test labios-data-path-test "[deployment]"

# Native hermetic lane
cmake --preset dev
cmake --build build/dev -j$(nproc)
ctest --test-dir build/dev -L unit --output-on-failure
```

Run every focused test affected by the change. Infrastructure-dependent and
characterization lanes must use the scoped commands in the evidence map; an
indiscriminate `ctest` is not a hermetic release gate. Record failures rather
than weakening tests or converting benchmark results into correctness claims.

## Workflow

1. Create a branch from `labios-2.0` (not `master`).
2. Read the planning authority and the active prompt for the selected release gate.
3. Implement one bounded slice and add focused tests.
4. Run the hermetic lane plus every affected live/failure path.
5. Update the authority, evidence map, and public docs when a claim changes.
6. Open a pull request against `labios-2.0` with commands and results recorded.

## Coding Conventions

**Language**: C++20. Use coroutines, jthread, concepts, ranges where they simplify code.

**Threading**: `std::jthread` + `std::stop_token` for cooperative shutdown. No kill booleans.
No spin-polling or busy-wait loops.

**Memory**: Smart pointers only. No raw `new`/`delete`. No `std::system()` or `popen()`.
Use `std::filesystem` for all path operations.

**Testing**: Every component has Catch2 unit tests. No exceptions.

**Commits**: Use [Conventional Commits](https://www.conventionalcommits.org/):
```
feat(component): short description
fix(component): short description
perf(component): short description
refactor(component): short description
test(component): short description
docs(component): short description
```

## Architecture

Read [docs/architecture.md](docs/architecture.md) before making changes.

Key invariants:
- Clients never talk to workers. The dispatcher is the only bridge.
- Internal plumbing (DragonflyDB warehouse, NATS label queue) is separate from external backends (user's Redis, user's filesystem).
- All intelligence lives in the runtime. Backends are thin last-mile adapters.

## Current 2.1 priorities

Check the planning index and GitHub issues before taking ownership. The serialized
path from the release candidate to final 2.1.0 is:

1. Catalog-authoritative cross-process channels and workspaces (Prompt 13).
2. MCP workspace knowledge over the public coordination API (Prompt 14).
3. A scientific frontend with machine-checked Label I/O equivalence (Prompt 15).
4. The frozen independent trace-guided experiment, with positive or negative
   results reported without spin (Prompt 08).
5. Final clean-tree evidence, documentation coherence, and release review
   (Prompt 16).

Additional adapters, FUSE, Kubernetes packaging, new scheduling policies, and
`io_uring` are deferred unless the planning authority is deliberately changed.

## Tests

| Category | Command | Infrastructure |
|----------|---------|----------------|
| Unit | `ctest -L unit` | None |
| Smoke | `ctest -L smoke` | NATS + DragonflyDB |
| Kernel | `ctest -L kernel` | NATS + DragonflyDB |
| Benchmark | `ctest -L bench` | None |
| Integration | `ctest -L integration` | Full stack |
| MCP hermetic | `PYTHONPATH="$PWD/build/dev/python:$PWD/mcp" python3 -m pytest mcp/tests -m "not live" -v` | Built Python SDK + MCP dependencies |
| MCP live | `docker compose exec -T -e LABIOS_MCP_LIVE=1 mcp python -m pytest tests -m live -v` | Docker stack |

Unit and hermetic MCP tests run without infrastructure and must always pass.
Smoke, live MCP, and integration tests require `docker compose up -d --wait`.

## Questions

Open a GitHub issue or contact the team directly.
