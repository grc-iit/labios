# 16 — LABIOS 2.1 release evidence and coherence

**Queue status:** Final gate; run after Prompts 13–15 and the Prompt-08 experiment.

Run last, after 15 and the Prompt-08 experiment. Read `AGENTS.md`,
`.planning/LABIOS-2.1.md`, the entire public documentation surface, workflows,
tests, benchmark procedures/results, and the
first-pass evidence map. Start from a clean clone or clean disposable worktree and
fresh Compose volumes.

## Objective

Produce an honest, reproducible LABIOS 2.1 checkpoint whose public claims, APIs,
deployment, demonstrations, CI, and evidence agree.

## Ownership

Release tests/benchmarks, CI, packaging, public docs, evidence-map refresh, version/
release metadata, and minimal fixes for defects reproduced during rehearsal. Do not
add features, backends, shadow metadata, or broaden deployment claims.

## Work

1. Re-run fresh native configure/build, all honest test lanes, sanitizers, Python,
   MCP, Compose build/config/health, and every public example command.
2. Re-run the five-minute C++ and Python golden paths, P04 pipeline, WS2 failure
   suite, WS3 mixed-batch suite, three-arm trace experiment or its frozen result,
   MCP core and knowledge workflows, cross-process coordination, and scientific
   frontend with data verification after every path.
3. Exercise dispatcher, worker, manager, NATS, and Dragonfly restarts; lost ACK,
   stale epoch, no-worker parking, over-capacity, blocked dependency, external
   backend failure, per-worker visibility, and malformed/version-skewed inputs.
4. Refresh the claim-to-artifact map and NSF delta table. Incorporate team-supplied
   inputs; leave unavailable inputs marked, never inferred. Keep historical results
   separate from current measurements.
5. Reconcile API signatures, configuration, feature status, installation, topology,
   security limitations, teardown/recovery, and benchmark reproduction across all
   public docs. Remove every future feature from current-support language.
6. If rehearsal exposes a runtime defect, add a minimal reproducer, make the
   smallest scoped fix, run focused regression, then repeat the affected full path.
   Stop only if the fix changes a contract or feature scope.

## Release gates

Every admitted label in failure tests is accounted for; all examples run from docs;
all current claims link to a reproducible artifact; no blocker/high defect remains;
CI exercises wire compatibility, Python/MCP, and Compose; raw data/configs/digests
ship with performance results; limitations call the deployment a single-host
reference unless stronger evidence now exists.

## Exit

Update `.planning/LABIOS-2.1.md` to the final measured truth and deliver one release
checklist with pass/fail evidence. A negative performance result is releaseable; an
unverified or confounded claim is not.
