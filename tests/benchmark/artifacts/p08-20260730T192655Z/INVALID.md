# Invalid orchestration attempt

No benchmark rows or performance observations were produced. Docker Compose
created the test container, but Docker Desktop returned an API `404` when the
runner invoked `docker wait` on the detached Compose-run result. The workload,
repetition count, statistical method, and success gates were not changed. The
runner was amended to execute the named test container synchronously and retain
it long enough to copy artifacts before a clean restart from fresh volumes.
