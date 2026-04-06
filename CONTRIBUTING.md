# Contributing to LABIOS

LABIOS is an active research project at Illinois Institute of Technology.
Contributions are welcome from team members and collaborators.

## Setup

```bash
git clone https://github.com/grc-iit/labios.git
cd labios
docker compose up -d        # Full stack in under 5 minutes
```

See [docs/getting-started.md](docs/getting-started.md) for the full walkthrough.

## Build and Test

```bash
# Docker (recommended)
docker compose exec test bash
./run_tests.sh

# Native
cmake --preset dev
cmake --build build/dev -j$(nproc)
ctest --test-dir build/dev
```

All tests must pass before submitting a pull request.

## Workflow

1. Create a branch from `labios-2.0` (not `master`)
2. Write code, write tests
3. Run the full test suite
4. Open a pull request against `labios-2.0`

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

## What Needs Work

Check the GitHub issues for tagged tasks. Priority areas:

- Additional backend adapters (`vector://`, `graph://`, `s3://`, `pfs://`)
- FUSE filesystem mount
- Kubernetes deployment (Helm charts)
- Additional scheduling policies
- io_uring integration for async I/O

## Tests

| Category | Command | Infrastructure |
|----------|---------|----------------|
| Unit | `ctest -L unit` | None |
| Smoke | `ctest -L smoke` | NATS + DragonflyDB |
| Kernel | `ctest -L kernel` | NATS + DragonflyDB |
| Benchmark | `ctest -L bench` | None |
| Integration | `ctest -L integration` | Full stack |
| MCP | `docker compose exec mcp python -m pytest tests/ -v` | Docker stack |

Unit tests run without infrastructure and must always pass. Smoke and integration
tests require `docker compose up -d`.

## Questions

Open a GitHub issue or contact the team directly.
