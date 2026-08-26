# 08 — Full trace-guided experiment and WS3 close

**Queue status:** Blocked by Prompt 15; run fourth in the RC-to-final queue.

The method was frozen after 07. For the current RC-to-final queue, run this after
Prompt 15 and before Prompt 16 on a working Docker host so the experiment measures
the feature-complete candidate. Read `AGENTS.md`, `.planning/LABIOS-2.1.md`, the
benchmark README, trace profile, benchmark source, and the evidence map. This
session measures; it does not redesign the method after seeing results.

## Objective

Produce a reproducible positive or negative answer to whether trace information
improves placement over the same-policy ablation on the documented topology.

## Protocol

1. Freeze commit/image digests, topology, resource controls, worker calibration,
   run ID, workload seeds, warmup, cache policy, timed boundary, and verification.
2. Run baseline, ablation, and informed arms from `docker compose down -v` and a
   rebuild. Use identical workloads and randomized or counterbalanced arm order.
3. Use at least 20 independent repetitions per profile/arm, or a predeclared larger
   count from a power analysis. Record per-label submission, completion, service,
   queue, worker, scheme, retries, failures, and verified result data.
4. Compare informed primarily with ablation. Baseline is contextual only. Report
   distributions, effect sizes, uncertainty, and the predeclared statistical test;
   do not use p95-of-five or successful completions only.
5. Check cache/locality/volume/order/retry contamination and trace carry-over. A
   failed equivalence, policy probe, calibration, or completion check invalidates
   the arm and requires a clean rerun.

## Exit

Store raw machine-readable rows, exact commands/configs/digests, analysis output,
and a concise interpretation. Update `.planning/LABIOS-2.1.md` with the measured
result, including a negative result without euphemism. WS3 performance closes only
when all arms are completion-verified and the confound checklist is clean.
