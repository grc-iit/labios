#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$ROOT"

compose() { COMPOSE_PROGRESS=plain docker compose "$@"; }

run_arm() {
    local arm=$1 policy profile
    case "$arm" in
        baseline)
            policy=round-robin
            profile=
            ;;
        ablation)
            policy=minmax
            profile=/etc/labios/profiles/trace_ablation.toml
            ;;
        informed)
            policy=minmax
            profile=/etc/labios/profiles/trace_guided.toml
            ;;
        *)
            echo "unknown arm: $arm" >&2
            return 2
            ;;
    esac

    compose down -v --remove-orphans
    LABIOS_SCHEDULER_POLICY="$policy" \
    LABIOS_SCHEDULER_PROFILE="$profile" \
    LABIOS_WORKER_1_TEST_DELAY_MS=0 \
    LABIOS_WORKER_2_TEST_DELAY_MS=20 \
    LABIOS_WORKER_3_TEST_DELAY_MS=60 \
        compose up -d --wait

    local profile_name=none
    [[ -n "$profile" ]] && profile_name=$(basename "$profile" .toml)
    compose run --rm -T --no-deps \
        -e LABIOS_BENCH_LIVE=1 \
        -e LABIOS_BENCH_ARM="$arm" \
        -e LABIOS_BENCH_ACTIVE_POLICY="$policy" \
        -e LABIOS_BENCH_ACTIVE_PROFILE="$profile_name" \
        -e LABIOS_BENCH_RUN_ID="p10-dry-$arm" \
        -e LABIOS_BENCH_REPETITIONS=1 \
        -e LABIOS_BENCH_OUTPUT="/tmp/p10-$arm.csv" \
        test labios-bench_trace_guided_selection "[!benchmark]"
}

trap 'compose --profile test down -v --remove-orphans' EXIT
compose --profile test build
run_arm baseline
run_arm ablation
run_arm informed
