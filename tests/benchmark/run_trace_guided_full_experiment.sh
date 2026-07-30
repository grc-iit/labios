#!/usr/bin/env bash
set -euo pipefail

if [[ "${LABIOS_ALLOW_INVALID_P08_DIAGNOSTIC:-0}" != "1" ]]; then
    echo "refusing invalid Prompt 08 method: repetitions are not independent and actual per-label service/queue observations are unavailable" >&2
    echo "set LABIOS_ALLOW_INVALID_P08_DIAGNOSTIC=1 only to reproduce the rejected diagnostic bundle" >&2
    exit 2
fi

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$repo_root"

run_id="${LABIOS_BENCH_RUN_ID:-p08-$(date -u +%Y%m%dT%H%M%SZ)}"
artifact_root="${LABIOS_BENCH_ARTIFACT_ROOT:-$repo_root/tests/benchmark/artifacts/$run_id}"
mkdir -p "$artifact_root"

cleanup() {
    docker compose --profile test down -v --remove-orphans >/dev/null 2>&1 || true
}
trap cleanup EXIT

docker version >"$artifact_root/docker-version.txt"
docker compose version >"$artifact_root/compose-version.txt"
docker compose config --quiet
git rev-parse HEAD >"$artifact_root/base-commit.txt"
git status --short >"$artifact_root/tracked-status.txt"
git diff --binary | sha256sum >"$artifact_root/tracked-diff.sha256"
uname -a >"$artifact_root/uname.txt"
{ command -v lscpu >/dev/null && lscpu; } >"$artifact_root/cpu.txt" || true
{ command -v free >/dev/null && free -h; } >"$artifact_root/memory.txt" || true
cp conf/profiles/trace_guided.toml conf/profiles/trace_ablation.toml "$artifact_root/"

python3 - "$artifact_root/arm-order.txt" <<'PY'
import random, sys
arms = ["baseline", "ablation", "informed"]
random.Random(2101).shuffle(arms)
open(sys.argv[1], "w").write("\n".join(arms) + "\n")
PY

while IFS= read -r arm; do
    arm_dir="$artifact_root/$arm"
    mkdir -p "$arm_dir"
    cleanup
    case "$arm" in
        baseline)
            policy="round-robin"
            profile=""
            expected_profile="none"
            ;;
        ablation)
            policy="minmax"
            profile="/etc/labios/profiles/trace_ablation.toml"
            expected_profile="trace_ablation"
            ;;
        informed)
            policy="minmax"
            profile="/etc/labios/profiles/trace_guided.toml"
            expected_profile="trace_guided"
            ;;
    esac
    export LABIOS_SCHEDULER_POLICY="$policy"
    export LABIOS_SCHEDULER_PROFILE="$profile"
    export LABIOS_WORKER_1_TEST_DELAY_MS=0
    export LABIOS_WORKER_2_TEST_DELAY_MS=20
    export LABIOS_WORKER_3_TEST_DELAY_MS=60
    docker compose --profile test config >"$arm_dir/compose-config.yaml"
    docker compose --profile test up -d --build --wait \
        nats redis redis-kv manager dispatcher worker-1 worker-2 worker-3
    docker compose --profile test images --format json >"$arm_dir/images.json"
    docker compose --profile test ps >"$arm_dir/compose-ps.txt"

    container_name="labios-p08-${run_id}-${arm}"
    if docker compose --profile test run --build -T --no-deps \
            --name "$container_name" \
            -e LABIOS_BENCH_LIVE=1 \
            -e LABIOS_BENCH_ARM="$arm" \
            -e LABIOS_BENCH_RUN_ID="$run_id" \
            -e LABIOS_BENCH_REPETITIONS=20 \
            -e LABIOS_BENCH_WARMUP=1 \
            -e LABIOS_BENCH_ACTIVE_POLICY="$policy" \
            -e LABIOS_BENCH_ACTIVE_PROFILE="$expected_profile" \
            -e LABIOS_BENCH_OUTPUT=/tmp/raw.csv \
            -e LABIOS_BENCH_CALIBRATION_OUTPUT=/tmp/calibration.json \
            test labios-bench_trace_guided_selection "[!benchmark]" \
            </dev/null >"$arm_dir/benchmark.log" 2>&1; then
        exit_code=0
    else
        exit_code=$?
    fi
    docker cp "$container_name:/tmp/raw.csv" "$arm_dir/raw.csv"
    docker cp "$container_name:/tmp/calibration.json" "$arm_dir/calibration.json"
    docker rm "$container_name" >/dev/null
    docker compose logs --no-color >"$arm_dir/services.log"
    test "$exit_code" = "0"

    python3 - "$arm_dir/raw.csv" "$arm" <<'PY'
import csv, sys
path, arm = sys.argv[1:]
rows = list(csv.DictReader(open(path, newline="")))
assert len(rows) == 420, (arm, len(rows))
assert all(r["arm"] == arm for r in rows)
assert all(r["verified"] == "1" and r["terminal_state"] == "complete"
           and not r["failure"] and int(r["worker_id"]) > 0 for r in rows)
counts = {}
for row in rows:
    counts[row["profile"]] = counts.get(row["profile"], 0) + 1
assert counts == {
    "small-hot-metadata": 320,
    "large-sequential": 80,
    "mixed-pipeline": 20,
}, counts
PY
done <"$artifact_root/arm-order.txt"

python3 tests/benchmark/analyze_trace_guided.py \
    --invalid-diagnostic "$artifact_root"
(cd "$artifact_root" && sha256sum */raw.csv >raw.sha256)
cleanup
trap - EXIT
printf '%s\n' "$artifact_root"
