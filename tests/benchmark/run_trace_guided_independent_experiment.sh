#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$repo_root"

if [[ -n "$(git status --porcelain --untracked-files=all)" ]]; then
    echo "refusing Prompt 08 experiment from a dirty or source-incomplete tree" >&2
    exit 2
fi
if ! docker version >/dev/null 2>&1; then
    echo "Prompt 08 requires a working Docker daemon" >&2
    exit 2
fi

run_id="${LABIOS_BENCH_RUN_ID:-p08-independent-$(date -u +%Y%m%dT%H%M%SZ)}"
artifact_root="${LABIOS_BENCH_ARTIFACT_ROOT:-$repo_root/tests/benchmark/artifacts/$run_id}"
experiment_project="${LABIOS_BENCH_COMPOSE_PROJECT:-p08independent}"
mkdir -p "$artifact_root"
current_project=""
cleanup() {
    if [[ -n "$current_project" ]]; then
        COMPOSE_PROJECT_NAME="$current_project" docker compose --profile test \
            down -v --remove-orphans >/dev/null 2>&1 || true
    fi
}
trap cleanup EXIT

# Freeze source and host provenance before any image is built.
git rev-parse HEAD >"$artifact_root/source-commit.txt"
git ls-files -s | sha256sum >"$artifact_root/source-index.sha256"
git status --porcelain --untracked-files=all >"$artifact_root/source-status.txt"
docker version >"$artifact_root/docker-version.txt"
docker compose version >"$artifact_root/compose-version.txt"
uname -a >"$artifact_root/uname.txt"
{ command -v lscpu >/dev/null && lscpu; } >"$artifact_root/cpu.txt" || true
{ command -v free >/dev/null && free -h; } >"$artifact_root/memory.txt" || true
cp conf/profiles/trace_guided.toml conf/profiles/trace_ablation.toml \
    "$artifact_root/"
printf '%s\n' \
    'seed=2101' \
    'replicates_per_profile_arm=20' \
    'training=two fixed cycles of small/large/pipeline after one pipeline-source write' \
    'verification=public reads after all measured decisions in each fresh-state replicate' \
    'primary_unit=per-replicate median completion_us' \
    'primary_comparison=informed versus static-weight-matched ablation' \
    'topology=three equal advertised workers; fixed hidden execution delays 0/20/60 ms' \
    >"$artifact_root/method.txt"

# Equal advertised descriptors isolate the contribution of completion-derived
# trace information. The delay fixture is identical in every arm and replicate.
export LABIOS_WORKER_1_SPEED=3 LABIOS_WORKER_2_SPEED=3 LABIOS_WORKER_3_SPEED=3
export LABIOS_WORKER_1_ENERGY=3 LABIOS_WORKER_2_ENERGY=3 LABIOS_WORKER_3_ENERGY=3
export LABIOS_WORKER_1_CAPACITY=50GB LABIOS_WORKER_2_CAPACITY=50GB LABIOS_WORKER_3_CAPACITY=50GB
export LABIOS_WORKER_1_TEST_DELAY_MS=0 LABIOS_WORKER_2_TEST_DELAY_MS=20 LABIOS_WORKER_3_TEST_DELAY_MS=60

COMPOSE_PROJECT_NAME="$experiment_project" docker compose --profile test config --quiet
COMPOSE_PROJECT_NAME="$experiment_project" docker compose --profile test build \
    manager dispatcher worker-1 worker-2 worker-3 test
COMPOSE_PROJECT_NAME="$experiment_project" docker compose --profile test images \
    --format json >"$artifact_root/images.json"
COMPOSE_PROJECT_NAME="$experiment_project" docker compose --profile test config \
    >"$artifact_root/compose-config.yaml"

python3 - "$artifact_root/order.csv" <<'PY'
import csv, itertools, random, sys
cells = list(itertools.product(
    ("baseline", "ablation", "informed"),
    ("small-hot-metadata", "large-sequential", "mixed-pipeline"),
    range(20),
))
random.Random(2101).shuffle(cells)
with open(sys.argv[1], "w", newline="") as target:
    writer = csv.writer(target, lineterminator="\n")
    writer.writerow(("sequence", "arm", "profile", "repetition"))
    for sequence, cell in enumerate(cells):
        writer.writerow((sequence, *cell))
PY
printf 'sequence,arm,profile,repetition,replicate_id,compose_project,status\n' \
    >"$artifact_root/cells.csv"

mapfile -t cells < <(tail -n +2 "$artifact_root/order.csv")
for cell in "${cells[@]}"; do
    IFS=, read -r sequence arm profile repetition <<<"$cell"
    replicate_id="${run_id}-${sequence}-${arm}-${profile}-${repetition}"
    current_project="$experiment_project"
    cell_dir="$artifact_root/cells/$replicate_id"
    mkdir -p "$cell_dir" "$artifact_root/$arm/$profile/calibration"

    case "$arm" in
        baseline)
            policy="round-robin"
            scheduler_profile=""
            expected_profile="none"
            ;;
        ablation)
            policy="minmax"
            scheduler_profile="/etc/labios/profiles/trace_ablation.toml"
            expected_profile="trace_ablation"
            ;;
        informed)
            policy="minmax"
            scheduler_profile="/etc/labios/profiles/trace_guided.toml"
            expected_profile="trace_guided"
            ;;
        *) echo "unknown arm $arm" >&2; exit 2 ;;
    esac
    export LABIOS_SCHEDULER_POLICY="$policy"
    export LABIOS_SCHEDULER_PROFILE="$scheduler_profile"

    cleanup
    COMPOSE_PROJECT_NAME="$current_project" docker compose --profile test \
        up -d --no-build --wait nats redis redis-kv manager dispatcher \
        worker-1 worker-2 worker-3 </dev/null
    COMPOSE_PROJECT_NAME="$current_project" docker compose --profile test ps \
        >"$cell_dir/compose-ps.txt"

    container_name="${current_project}-benchmark"
    set +e
    COMPOSE_PROJECT_NAME="$current_project" docker compose --profile test run \
        -T --no-deps --name "$container_name" \
        -e LABIOS_BENCH_LIVE=1 \
        -e LABIOS_BENCH_INDEPENDENT=1 \
        -e LABIOS_BENCH_ARM="$arm" \
        -e LABIOS_BENCH_PROFILE="$profile" \
        -e LABIOS_BENCH_REPETITION="$repetition" \
        -e LABIOS_BENCH_RUN_ID="$run_id" \
        -e LABIOS_BENCH_ACTIVE_POLICY="$policy" \
        -e LABIOS_BENCH_ACTIVE_PROFILE="$expected_profile" \
        -e LABIOS_BENCH_OUTPUT=/tmp/raw.csv \
        -e LABIOS_BENCH_CALIBRATION_OUTPUT=/tmp/calibration.json \
        test labios-bench_trace_guided_selection "[!benchmark]" \
        </dev/null >"$cell_dir/benchmark.log" 2>&1
    exit_code=$?
    set -e
    if [[ "$exit_code" -eq 0 ]]; then
        docker cp "$container_name:/tmp/raw.csv" "$cell_dir/raw.csv"
        docker cp "$container_name:/tmp/calibration.json" \
            "$artifact_root/$arm/$profile/calibration/$repetition.json"
    fi
    docker rm "$container_name" >/dev/null 2>&1 || true

    if [[ "$exit_code" -ne 0 ]]; then
        COMPOSE_PROJECT_NAME="$current_project" docker compose logs --no-color \
            >"$cell_dir/services.log" 2>&1 || true
        printf '%s,%s,%s,%s,%s,%s,failed\n' "$sequence" "$arm" \
            "$profile" "$repetition" "$replicate_id" "$current_project" \
            >>"$artifact_root/cells.csv"
        printf 'cell %s failed with exit code %s\n' "$replicate_id" "$exit_code" \
            >"$artifact_root/INVALID.md"
        exit "$exit_code"
    fi

    if ! python3 - "$cell_dir/raw.csv" "$arm" "$profile" "$repetition" <<'PY'
import csv, sys
path, arm, profile, repetition = sys.argv[1:]
rows = list(csv.DictReader(open(path, newline="")))
training = [row for row in rows if row["phase"] == "training"]
measured = [row for row in rows if row["phase"] == "measured"]
expected = {"small-hot-metadata": 16, "large-sequential": 4, "mixed-pipeline": 1}
assert len(training) == 43, len(training)
assert len(measured) == expected[profile], len(measured)
assert all(row["arm"] == arm and row["repetition"] == repetition for row in rows)
assert all(row["verified"] == "1" and row["terminal_state"] == "complete"
           and not row["failure"] and int(row["worker_id"]) >= 0
           and int(row["attempt"]) >= 1 for row in rows)
PY
    then
        COMPOSE_PROJECT_NAME="$current_project" docker compose logs --no-color \
            >"$cell_dir/services.log" 2>&1 || true
        printf '%s,%s,%s,%s,%s,%s,failed-validation\n' "$sequence" "$arm" \
            "$profile" "$repetition" "$replicate_id" "$current_project" \
            >>"$artifact_root/cells.csv"
        printf 'cell %s failed raw-row validation\n' "$replicate_id" \
            >"$artifact_root/INVALID.md"
        exit 1
    fi

    aggregate="$artifact_root/$arm/$profile/raw.csv"
    mkdir -p "$(dirname "$aggregate")"
    if [[ ! -f "$aggregate" ]]; then
        cp "$cell_dir/raw.csv" "$aggregate"
    else
        tail -n +2 "$cell_dir/raw.csv" >>"$aggregate"
    fi
    printf '%s,%s,%s,%s,%s,%s,passed\n' "$sequence" "$arm" "$profile" \
        "$repetition" "$replicate_id" "$current_project" \
        >>"$artifact_root/cells.csv"
    cleanup
    current_project=""
done

python3 tests/benchmark/analyze_trace_guided_independent.py "$artifact_root"
find "$artifact_root" -type f -name '*.csv' -o -name '*.json' | sort | \
    xargs sha256sum >"$artifact_root/data.sha256"
trap - EXIT
printf '%s\n' "$artifact_root"
