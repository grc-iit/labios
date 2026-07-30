# Invalid partial run

The baseline arm completed with 420 verified rows, but the experiment is not a
result. The synchronous Compose command inherited the shell loop's arm-order
file on standard input and consumed the remaining `ablation` and `informed`
lines. Analysis rejected the incomplete bundle because both required raw files
were absent. The runner was amended only to connect the test container's stdin
to `/dev/null`; workload, repetitions, timing boundaries, statistical method,
and gates were unchanged before restarting every arm from fresh volumes.
