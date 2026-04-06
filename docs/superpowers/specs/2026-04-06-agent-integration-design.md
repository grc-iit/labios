# LABIOS Agent Integration Design

**Date:** 2026-04-06
**Status:** Approved for implementation
**Author:** Anthony Kougkas + Claude (brainstorm session)

## Problem

Coding agents (Claude Code, Codex CLI, and the broader class of application agents) perform intensive filesystem I/O that the raw filesystem cannot coordinate, optimize, or scale. The problem manifests across three dimensions:

1. **Backend fragmentation.** An agent connecting to files, SQLite, vector DBs, graph stores, and object storage today needs N MCP servers with N different APIs, N auth models, N error surfaces. No batching across backends. No unified scheduling.

2. **Scale collapse.** Agents choke on GB-level data (large logs, datasets, profiling output, monorepo scans). Everything flows through the agent's context window. No way to process data at the storage layer before the agent sees it.

3. **Coordination vacuum.** Agent swarms (10 coding agents on one codebase) have no I/O coordination layer. No conflict detection. No shared caching. No artifact dedup. No real-time discovery sharing. The filesystem offers last-writer-wins semantics. Silent data loss is the default.

## Solution

LABIOS becomes the universal I/O runtime for agents. One connection replaces the entire backend surface. Labels are the interface. The agent expresses WHAT it needs (intent, pipeline, destination, priority, isolation) and the runtime handles HOW (routing, scheduling, processing, coordination, scaling).

### Core Thesis

Labels are the most agent-friendly I/O interface because they are:
- **Self-describing**: carry all context needed for intelligent routing
- **Intent-aware**: the agent says what the I/O is for, not just where it goes
- **Programmable**: carry DAGs of operations, not just data addresses
- **Composable**: pipelines, continuations, dependencies chain naturally
- **Observable**: the agent queries system state through the same label interface
- **Configurable**: every knob is a dimension the agent can optimize along

Humans get overwhelmed by configuration complexity. Agents thrive on it. LABIOS's hyper-configurability is a feature when your users are AI agents that navigate parameter spaces, observe telemetry, and adapt.

## Integration Architecture

```
Coding Agents (Claude Code, Codex CLI, agent swarms)
         │
         │ MCP protocol (single connection)
         ▼
┌─────────────────────────────────────────────────────────┐
│              LABIOS MCP Server                           │
│                                                         │
│  Tool calls → Labels with intent inference              │
│  Session tracking → app_id → isolation boundary         │
│  Smart internals: promotion, dedup, scoring             │
└──────────────────────────┬──────────────────────────────┘
                           │ Labels
                           ▼
┌─────────────────────────────────────────────────────────┐
│              LABIOS Runtime                               │
│                                                         │
│  Shuffler → Scheduler → Workers                         │
│  URI routing: file:// s3:// vector:// graph:// kv://    │
│  SDS pipelines: process data at storage layer           │
│  Channels + Workspaces: coordination primitives         │
│  Elastic scaling: commission workers on demand          │
└─────────────────────────────────────────────────────────┘
```

Agents connect to one MCP server. That server is a LABIOS client. Every tool call becomes a label. The runtime routes, schedules, pipelines, and coordinates.

## Approach: Hybrid Gradient

Two tiers, same runtime:

**Tier 1 (Transparent).** `LD_PRELOAD` intercepts POSIX I/O from any agent process or its children (builds, tests, scripts). Agent benefits from shuffling, scheduling, caching automatically. Zero code changes.

**Tier 2 (SDK-aware).** MCP tools give the agent full expressiveness: intent tags, programmable pipelines, workspace memory, cross-backend routing, observability. The agent that speaks LABIOS natively gets dramatically more value.

The gradient validates the spec's thesis (S12.4): unaware agents get benefits for free, aware agents get full power.

## Use Case 1: Universal Backend Routing

A single label can cross multiple backends:

```
Label {
  source:      "kv://project/test-results"
  pipeline:    [filter_failures,
                enrich_from("graph://project/dep-graph"),
                correlate_with("file:///repo/.git/log")]
  destination: "kv://project/bug-priority-queue"
  intent:      ToolOutput
}
```

One label. Three backends (kv, graph, file). Pipeline correlates test failures with dependency graph and git history. Agent didn't touch raw data. Agent didn't manage three separate connections.

## Use Case 2: Large-Scale I/O Gateway

Agents can't handle GB-level data in context. LABIOS pipelines process data at the storage layer:

```
Label {
  source:      "file:///repo/src/**/*.py"    // 800 files
  pipeline:    [extract_signatures, build_call_graph,
                detect_patterns, summarize_architecture]
  destination: "kv://project/arch-analysis"
  intent:      ReasoningTrace
}
```

Workers scan 800 files in parallel. Build call graph. Detect patterns. Summarize to 5KB. Agent gets structured understanding without reading a single file.

## Use Case 3: Agentic Memory at Scale

Not one-liner memories. Bulk artifacts: full git diffs (50KB), dependency graphs (2MB), test results (200MB), architecture analyses (500KB). Accumulated across sessions, projects, users.

### Four-Tier Memory Hierarchy

| Tier | LABIOS Primitive | Lifetime | Visible To |
|------|-----------------|----------|------------|
| Session | Workspace `session/{id}` | Session end | This session |
| Project | Workspace `project/{repo_hash}` | Indefinite | All sessions in project |
| User | Workspace `user/{user_id}` | Indefinite | All projects for user |
| Team | Workspace `team/{id}` + grants | Indefinite | Granted users |

### Promotion Path

Session → Project → User → Team. Memories promote upward based on value. Continuations trigger promotion on session disconnect:

```
Label {
  source:      "memory://session/discoveries/*"
  pipeline:    [deduplicate, score_by_recall_frequency,
                filter("score > 0.7"), categorize]
  destination: "kv://user/knowledge-base"
  intent:      SharedState
  durability:  Durable
  continuation: None  // terminal
}
```

Triggered automatically by the MCP server on session disconnect.

## Use Case 4: Agent Swarm Coordination

10 agents, same codebase. Each with a sandbox (worktree or container).

**Resource claiming:** Agent A claims `src/auth/**`. Other agents see the claim. WAW on claimed resources triggers conflict resolution instead of silent overwrite.

**Artifact sharing:** Build output from Agent A published to channel `build-artifacts`. Agent B subscribes with a pipeline filter: receive only `.so` metadata, not the files themselves.

**Discovery propagation:** Agent A finds a bug pattern. Publishes to channel `discoveries`. All other agents receive it in real-time. Agent B avoids introducing the same bug.

**Shared object cache:** 10 agents building the same project. LABIOS deduplicates common object files via workspace. Total build I/O: 500MB instead of 5GB.

## MCP Server Tool Design

### Workspace and Sandbox Management

```
labios_sandbox(action, name?, config?)
  action: "create" | "snapshot" | "restore" | "destroy" | "list"
  Manages isolated agent working environments.
  Snapshot captures entire sandbox state as labeled checkpoint.
  Restore rebuilds sandbox from checkpoint.
```

### Bulk Data Storage and Retrieval

```
labios_store(key, data_or_path, scope?, intent?, ttl?)
  Store bulk data into workspace. data_or_path: inline or path to ingest.
  If path: workers read it (agent doesn't load into context).
  Scope: "sandbox" | "project" | "user" | "team"

labios_retrieve(key, pipeline?, scope?, format?)
  Retrieve stored data, optionally processed through pipeline first.
  Pipeline runs at storage layer. Agent gets processed result.
  200MB test output → pipeline → 2KB failure summary.
```

### Programmable I/O

Pipeline format: list of strings, each in the form `"operation"` or `"operation:args"`. Operations resolve via the SDS program repository (`builtin://` prefix for builtins, `repo://` for registered functions). Example: `["filter:ERROR", "tail:100", "format:json"]`.

```
labios_process(source, pipeline, dest?, intent?)
  Process data at storage layer. Source: file path, glob, or URI.
  Glob sources execute across workers in parallel.
  Pipeline: list of operation strings (see format above).

labios_scan(pattern, pipeline?, intent?)
  Parallel scan across files matching a glob pattern.
  Each file processed through pipeline independently.
  Results aggregated and returned.
```

### Multi-Agent Coordination

```
labios_coordinate(action, resource?, message?)
  action: "claim" | "release" | "status" | "broadcast"
  Advisory locks on files/modules/paths. Conflict detection.

labios_stream(channel, data?, subscribe?, pipeline?)
  Pub/sub channels with optional pipeline-on-receive.
  Used for artifact sharing and discovery propagation.
```

### Observability

```
labios_observe(query)
  Query LABIOS system state via observe:// labels.
  "workers" | "queue" | "health" | "channels" | "data/location"

labios_knowledge(query?)
  "What do I know about X?" across all accessible memory tiers.
  Returns relevant memories ranked by scope and recency.
```

## Smart Server Internals

The MCP server is not a dumb proxy. It has internal logic that uses LABIOS primitives:

**On session connect:** Create session workspace. Load project/user workspaces. Publish session-start to `labios.sessions` channel.

**On session disconnect:** Evaluate session memories via continuation label. Promote memories recalled 2+ times or marked durable to project tier. Destroy session workspace.

**Background pipelines:** Periodic deduplication of project workspace. Summarize large values. Expire stale memories by TTL.

**Recall scoring:** Search across tiers in priority order (session → project → user → team). Score by recency, scope weight, intent relevance, recall frequency.

## Incremental Implementation Plan

### Phase 0: Foundation (minimal viable connection)
- LABIOS MCP server skeleton (TypeScript or Python, MCP protocol)
- Connect Claude Code to LABIOS via MCP config
- LD_PRELOAD intercept on spawned processes (already implemented)
- `labios_observe` tool (wraps existing observe:// labels)
- Validate: Claude Code session with LABIOS running, basic I/O flowing

### Phase 1: Memory
- `labios_store` and `labios_retrieve` with pipeline support
- Four-tier workspace naming convention
- Session connect/disconnect lifecycle
- `labios_knowledge` query tool
- Benchmark: cross-session recall vs vanilla Claude Code (re-read everything)

### Phase 2: Large I/O
- `labios_process` with glob source and parallel execution
- `labios_scan` for multi-file operations
- Pipeline builtins for code analysis (extract_signatures, build_graph)
- Benchmark: 100K-line codebase analysis via pipeline vs sequential reads

### Phase 3: Swarm Coordination
- `labios_coordinate` for resource claiming and conflict detection
- `labios_stream` for artifact sharing and discovery channels
- `labios_sandbox` for sandbox lifecycle management
- Benchmark: 10-agent concurrent build/test scenario

### Phase 4: Universal Backend Routing
- Additional BackendStore implementations (kv://, vector://, graph://)
- Cross-backend pipeline execution
- Benchmark: cross-backend ETL in a single label vs 3 MCP servers

## Benchmark Design

Every benchmark compares LABIOS-enhanced agents against vanilla agents on tasks where vanilla fails or degrades:

| Test | Vanilla Behavior | LABIOS Behavior | Metric |
|------|-----------------|----------------|--------|
| Cross-session recall | Re-reads all files (20 min) | Recalls from workspace (30 sec) | Time to productive |
| 4GB log analysis | Context overflow / OOM | Pipeline: filter → summarize → return 2KB | Success/fail |
| 10-agent concurrent build | 5GB artifacts, disk thrash, conflicts | 500MB shared cache, WAW detection | I/O volume, conflicts |
| 800-file codebase scan | Sequential reads (30 sec) | Parallel pipeline (2 sec) | Latency |
| Cross-backend correlation | 3 MCP calls, manual join | 1 label, pipeline joins at storage | Tool calls, latency |
| Knowledge across 50 sessions | Zero (no persistence) | Workspace recall with dedup | Knowledge retained |

## Architectural Decisions

### BackendStore concept: path-based vs label-based

Current implementation: `put(path, offset, data)`. Spec says: `put(label, data)`.

**Decision:** Keep path-based for Phase 0-2. The current interface is simpler and sufficient for file:// and kv:// backends. When vector:// and graph:// backends arrive (Phase 4), they need label metadata (intent, isolation) to make intelligent storage decisions. At that point, extend the concept to accept an optional label reference. Backward compatible via overload.

### MCP server language

**Decision:** Python. The pybind11 SDK (just built) provides native access to the LABIOS client. Python MCP server libraries are mature. Claude Code's MCP integration is well-tested with Python servers.

### Memory promotion heuristic

**Decision:** Recall frequency as primary signal. Memories recalled 2+ times in a session promote to project on disconnect. Memories with `SharedState` intent promote regardless of recall count. Memories with `Ephemeral` durability never promote.

## References

- LABIOS-SPEC.md: Sections 2 (Labels), 4 (URI Routing), 5 (SDS Pipelines), 8 (Coordination), 10 (Observability), 12 (Integration)
- US Patent 11,630,834 B2
- NSF Award #2331480
- claude-mem (https://github.com/thedotmack/claude-mem) as comparison baseline
