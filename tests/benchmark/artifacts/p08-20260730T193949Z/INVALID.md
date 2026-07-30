# Invalid Prompt 08 run

This bundle is retained as a rejected-method diagnostic, not as performance
evidence. Its 1,260 measured labels are completion-verified, but the run fails
the Prompt 08 contract:

1. The 20 repetitions reused one runtime and volume per arm. Catalog, capacity,
   cache, and trace state accumulated, and per-repetition medians exhibit strong
   serial trend (lag-1 autocorrelation approximately 0.77–0.85). Mann–Whitney
   and bootstrap calculations treating them as independent units are invalid.
2. Public setup and read-back verification labels executed between measured
   workloads and entered completion-derived trace history without raw rows.
   The informed policy therefore learned from untimed traffic.
3. `trace_service_input_us` and `trace_queue_input` are selected-worker EWMAs
   captured at scheduling, not actual per-label service and queue observations
   as required by the protocol.
4. `base-commit.txt` identifies `d109ed4`, but `tracked-diff.sha256` covers only
   `git diff` and omitted the then-untracked runner and analyzer. The exact
   executable method is therefore not reconstructible solely from the recorded
   provenance.

The public policy/profile probes, 420 rows per arm, digest checks, absence of
failures/retries, and informed calibration all passed. Ablation and informed
also made exactly the same 340/80/0 measured worker assignments. Those facts do
not cure the method failures and support no speedup, regression, or formal null
claim. `analysis.json` and `RESULT.md` are retained only to make the rejected
calculation inspectable.
