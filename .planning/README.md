# LABIOS planning index

This directory separates the current LABIOS 2.1 engineering plan from historical
or session-local material.

## Authority

[`LABIOS-2.1.md`](LABIOS-2.1.md) is the single planning, design, and engineering-
truth authority. Public documentation may summarize it, but another planning file
does not override it. The numbered prompt files are bounded execution checklists;
they do not create independent product requirements.

## Current checkpoint

LABIOS is at the `2.1.0-rc.1` source-candidate checkpoint. The candidate verifies
the bounded single-host reference topology and public golden paths documented in
the [release notes](../docs/releases/2.1.0-rc.1.md) and
[evidence map](../docs/evidence-map.md). It is not final 2.1.0 and carries no new
performance, production, secure multi-tenant, or verified multi-node claim.

## Active path to 2.1.0

Work is serialized in this order unless the project lead explicitly changes it:

| Order | Work item | Exit in one line |
|---:|---|---|
| 1 | [Prompt 13 — cross-process coordination](prompts/13-cross-process-coordination-runtime.md) | Independent processes share catalog-authoritative channels/workspaces with tested ACL, sequence, CAS, restart, and expiry behavior. |
| 2 | [Prompt 14 — MCP workspace knowledge](prompts/14-mcp-workspace-knowledge.md) | The MCP knowledge tool is a thin public-workspace frontend with no direct plumbing access. |
| 3 | [Prompt 15 — scientific frontend](prompts/15-scientific-label-frontend.md) | A scientific and direct-SDK path produce machine-checked equivalent Label I/O semantics and verified bytes. |
| 4 | [Prompt 08 — trace-guided experiment](prompts/08-trace-guided-full-experiment.md) | The frozen method produces a valid positive or negative result; otherwise performance remains explicitly unclaimed. |
| 5 | [Prompt 16 — final evidence and coherence](prompts/16-release-evidence-and-coherence.md) | A clean rehearsal accounts for every claim and yields the final 2.1.0 release checklist. |

Prompt 08 is intentionally run after the remaining feature work so its frozen
experiment measures the final candidate rather than an earlier runtime. Prompt 16
runs last. Each work item must update the authority and evidence map before the
next begins.

## Team inputs needed in parallel

The implementation queue does not block collection of the project records listed
in [`LABIOS-2.1.md` §8.2](LABIOS-2.1.md#82-evidence-required-from-the-project-team):
publications, release/DOI records, education and outreach outcomes, external
testbed results, award dates and recognition, and attribution boundaries.
These records must be supplied by the project team and must not be inferred from
source code.

## Repository policy

Git tracks this index, the authority, and the active prompt queue. `.gitignore`
continues to exclude archived plans, raw reporting drafts, references, future
ideas, and session-local notes. Ignored material is context only and cannot
silently become release scope. Promote a planning file by adding a deliberate
`.gitignore` exception and reviewing it in the same commit.
