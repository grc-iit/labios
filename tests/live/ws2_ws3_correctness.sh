#!/usr/bin/env bash
set -euo pipefail

COMPOSE=${COMPOSE:-"docker compose"}
PASSES=${PASSES:-2}
# Remote Docker contexts can otherwise exhaust sshd's unauthenticated-connection
# window while Compose starts and health-checks the full topology.
export COMPOSE_PARALLEL_LIMIT=${COMPOSE_PARALLEL_LIMIT:-1}
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$ROOT"
RUN_ROOT=${RUN_ROOT:-artifacts/ws2-ws3-live}
mkdir -p "$RUN_ROOT"

compose() { $COMPOSE "$@"; }
driver() { compose run --rm --no-deps -T test labios-live-correctness-driver "$@"; }
redis_hget() { compose exec -T redis redis-cli --raw HGET "labios:catalog:$1" "$2" | tr -d '\r'; }
extract_id() { grep '^ID=' | tail -1 | cut -d= -f2; }
wait_status() {
    local id=$1 expected=$2 timeout=${3:-60}
    for ((i=0; i<timeout*5; ++i)); do
        [[ "$(redis_hget "$id" status 2>/dev/null || true)" == "$expected" ]] && return 0
        sleep .2
    done
    echo "timed out waiting for label $id status=$expected (actual=$(redis_hget "$id" status || true))" >&2
    return 1
}
record() { printf '%s\n' "$*" | tee -a "$EVIDENCE"; }
run_driver() {
    record "\`$*\`"
    local output
    output=$(driver "$@")
    printf '```text\n%s\n```\n' "$output" >>"$EVIDENCE"
    printf '%s\n' "$output"
}
ids_from_batch() { grep '^ITEM=' | cut -d: -f2 | paste -sd, -; }

for pass in $(seq 1 "$PASSES"); do
    RUN_ID="$(date -u +%Y%m%dT%H%M%SZ)-$(git rev-parse --short HEAD)-pass${pass}"
    RUN_DIR="$RUN_ROOT/$RUN_ID"
    mkdir -p "$RUN_DIR"
    EVIDENCE="$RUN_DIR/evidence.md"
    export LABIOS_TEST_EXECUTION_DELAY_MS=0
    export LABIOS_DISPATCHER_BATCH_TIMEOUT_MS=100
    export LABIOS_WORKER_1_CAPACITY=10GB LABIOS_WORKER_2_CAPACITY=50GB LABIOS_WORKER_3_CAPACITY=200GB

    compose down -v --remove-orphans >>"$RUN_DIR/compose.log" 2>&1 || true
    compose up -d --wait >>"$RUN_DIR/compose.log" 2>&1
    {
        echo "# WS2/WS3 live correctness evidence"
        echo
        echo "- Run ID: \`$RUN_ID\`"
        echo "- Pass: $pass/$PASSES (fresh named volumes)"
        echo "- Commit: \`$(git rev-parse HEAD)\`"
        echo "- Dirty tree hash: \`$(git diff | git hash-object --stdin)\`"
        echo "- Docker context: \`${DOCKER_CONTEXT:-default}\`"
        echo "- Test-driver image: \`$(docker image inspect "${COMPOSE_PROJECT_NAME:-labios}-test:latest" --format '{{.Id}}')\`"
        echo "- Topology: NATS JetStream + DragonflyDB catalog + manager + dispatcher + three Tier-1 workers; shared worker-data volume; external Redis fixture"
        echo "- Images:"
        compose images --format json | sed 's/^/  - `/' | sed 's/$/`/'
        echo
    } >"$EVIDENCE"

    record "## 1. Source → pipeline → destination"
    run_driver pipeline "$RUN_ID-s1" >/dev/null

    record "## 2. Worker loss during Executing, redelivery, and dedupe"
    compose stop -t 2 worker-2 worker-3 >>"$RUN_DIR/compose.log" 2>&1
    export LABIOS_TEST_EXECUTION_DELAY_MS=3000
    compose up -d --force-recreate --wait worker-1 >>"$RUN_DIR/compose.log" 2>&1
    out=$(run_driver submit-write "file:///live/$RUN_ID-s2.bin" 4096 82)
    s2=$(printf '%s\n' "$out" | extract_id)
    wait_status "$s2" executing 30
    record "Observed $s2 in durable status \`executing\`; \`docker compose kill -s KILL worker-1\`."
    compose kill -s KILL worker-1 >>"$RUN_DIR/compose.log" 2>&1 || true
    compose up -d --wait worker-1 >>"$RUN_DIR/compose.log" 2>&1
    run_driver wait "$s2" complete 120000 >/dev/null
    run_driver verify "file:///live/$RUN_ID-s2.bin" 4096 82 >/dev/null
    before=$(compose exec -T worker-1 stat -c %Y "/labios/data/live/$RUN_ID-s2.bin")
    run_driver replay "$s2" >/dev/null
    sleep 4
    after=$(compose exec -T worker-1 stat -c %Y "/labios/data/live/$RUN_ID-s2.bin")
    [[ "$before" == "$after" ]]
    record "Submitted IDs: \`$s2\`. Terminal: complete. Durable completion present; explicit post-completion replay left backend mtime unchanged ($before), proving dedupe suppressed a second visible effect."
    run_driver inspect "$s2" >/dev/null

    record "## 3. Dispatcher loss after admission and before delivery"
    export LABIOS_TEST_EXECUTION_DELAY_MS=0 LABIOS_DISPATCHER_BATCH_TIMEOUT_MS=5000
    compose up -d --force-recreate --wait dispatcher worker-1 worker-2 worker-3 >>"$RUN_DIR/compose.log" 2>&1
    batch=$(run_driver submit-batch "file:///live/$RUN_ID-s3" 6 2048 51)
    s3_ids=$(printf '%s\n' "$batch" | ids_from_batch)
    first=${s3_ids%%,*}
    wait_status "$first" queued 10
    compose kill -s KILL dispatcher >>"$RUN_DIR/compose.log" 2>&1 || true
    record "Killed dispatcher with admitted ID $first still queued; submitted IDs: \`$s3_ids\`."
    compose up -d --wait dispatcher >>"$RUN_DIR/compose.log" 2>&1
    run_driver wait "$s3_ids" complete 120000 >/dev/null
    run_driver inspect "$s3_ids" >/dev/null
    for i in $(seq 0 5); do run_driver verify "file:///live/$RUN_ID-s3-$i.bin" 2048 51 >/dev/null; done
    record "All six IDs accounted for with one terminal completion and correct unique destination bytes."

    record "## 4. Zero workers, direct + Composite, dispatcher restart"
    export LABIOS_DISPATCHER_BATCH_TIMEOUT_MS=100
    compose up -d --force-recreate --wait dispatcher >>"$RUN_DIR/compose.log" 2>&1
    compose stop -t 2 worker-1 worker-2 worker-3 >>"$RUN_DIR/compose.log" 2>&1
    direct_out=$(run_driver submit-write "file:///live/$RUN_ID-s4-direct.bin" 1024 68)
    s4_direct=$(printf '%s\n' "$direct_out" | extract_id)
    composite_out=$(run_driver submit-composite "file:///live/$RUN_ID-s4-composite")
    s4_composite=$(printf '%s\n' "$composite_out" | extract_id)
    s4_children=$(printf '%s\n' "$composite_out" | grep '^CHILD_IDS=' | cut -d= -f2)
    wait_status "$s4_direct" parked 30
    wait_status "$s4_composite" parked 30
    compose restart dispatcher >>"$RUN_DIR/compose.log" 2>&1
    sleep 1
    compose start worker-1 >>"$RUN_DIR/compose.log" 2>&1
    run_driver wait "$s4_direct,$s4_composite,$s4_children" complete 120000 >/dev/null
    run_driver verify "file:///live/$RUN_ID-s4-direct.bin" 1024 68 >/dev/null
    run_driver verify "file:///live/$RUN_ID-s4-composite-a.bin" 1024 161 >/dev/null
    run_driver verify "file:///live/$RUN_ID-s4-composite-b.bin" 1024 178 >/dev/null
    run_driver inspect "$s4_direct,$s4_composite" >/dev/null
    record "IDs: direct \`$s4_direct\`, Composite \`$s4_composite\`, children \`$s4_children\`. Parking survived restart; bounded backoff history led to eventual completion."

    record "## 5. Manager restart and registry-v2 epochs"
    compose start worker-2 worker-3 >>"$RUN_DIR/compose.log" 2>&1
    sleep 3
    old_registry=$(run_driver registry)
    old_epoch=$(printf '%s\n' "$old_registry" | grep '^WORKER=1:' | cut -d: -f2)
    compose restart manager >>"$RUN_DIR/compose.log" 2>&1
    for _ in $(seq 1 30); do
        rebuilt=$(driver registry 2>/dev/null || true)
        [[ $(printf '%s\n' "$rebuilt" | grep -c '^WORKER=' || true) -eq 3 ]] && break
        sleep 1
    done
    [[ $(printf '%s\n' "$rebuilt" | grep -c '^WORKER=' || true) -eq 3 ]]
    printf '```text\n%s\n```\n' "$rebuilt" | tee -a "$EVIDENCE"
    compose restart worker-1 >>"$RUN_DIR/compose.log" 2>&1
    sleep 3
    replacement=$(run_driver registry)
    new_epoch=$(printf '%s\n' "$replacement" | grep '^WORKER=1:' | cut -d: -f2)
    [[ "$new_epoch" != "$old_epoch" ]]
    run_driver deregister 1 "$old_epoch" >/dev/null
    sleep 1
    final_registry=$(run_driver registry)
    printf '%s\n' "$final_registry" | grep -q "^WORKER=1:$new_epoch:"
    record "Manager reconstructed three v2 rows; worker 1 replacement epoch changed $old_epoch → $new_epoch; stale deregistration was rejected."

    record "## 6. One mixed live scheduling batch"
    export LABIOS_WORKER_1_CAPACITY=16KB LABIOS_WORKER_2_CAPACITY=24KB LABIOS_WORKER_3_CAPACITY=32KB
    compose up -d --force-recreate --wait worker-1 worker-2 worker-3 >>"$RUN_DIR/compose.log" 2>&1
    seed_read=$(run_driver submit-write "file:///live/$RUN_ID-s6-local-read.bin" 1024 97 | extract_id)
    seed_write=$(run_driver submit-write "file:///live/$RUN_ID-s6-local-write.bin" 1024 98 | extract_id)
    run_driver wait "$seed_read,$seed_write" complete 120000 >/dev/null
    compose stop -t 2 dispatcher >>"$RUN_DIR/compose.log" 2>&1
    ready=$(run_driver submit-write "file:///live/$RUN_ID-s6-ready.bin" 1024 17 | extract_id)
    # Catalog-stage, but do not transport, the external predecessor. This makes
    # the successor genuinely dependency-blocked in the captured mixed batch.
    predecessor=$(run_driver prepare-write "file:///live/$RUN_ID-s6-pred.bin" 1024 34 | extract_id)
    blocked=$(run_driver submit-dependent "file:///live/$RUN_ID-s6-blocked.bin" "$predecessor" 1024 35 | extract_id)
    local_read=$(run_driver submit-read "file:///live/$RUN_ID-s6-local-read.bin" 1024 | extract_id)
    local_write=$(run_driver submit-write "file:///live/$RUN_ID-s6-local-write.bin" 1024 99 | extract_id)
    comp_out=$(run_driver submit-composite "file:///live/$RUN_ID-s6-composite")
    composite=$(printf '%s\n' "$comp_out" | extract_id)
    composite_children=$(printf '%s\n' "$comp_out" | grep '^CHILD_IDS=' | cut -d= -f2)
    unknown=$(run_driver submit-pipeline "file:///live/$RUN_ID-s6-local-read.bin" "sqlite:///live/$RUN_ID-s6-unknown" | extract_id)
    over=$(run_driver submit-write "file:///live/$RUN_ID-s6-over.bin" 131072 119 | extract_id)
    mixed_ids="$ready,$blocked,$local_read,$local_write,$composite,$unknown,$over"
    compose start dispatcher >>"$RUN_DIR/compose.log" 2>&1
    initially_completed="$ready,$local_read,$local_write,$composite,$composite_children,$unknown"
    run_driver wait "$initially_completed" complete 180000 >/dev/null
    wait_status "$blocked" parked 30
    wait_status "$over" parked 30
    run_driver state "$over" >/dev/null
    # Capture the first attempt before satisfying the predecessor: exactly one
    # explicit decision for every unit, with the successor legally parked.
    run_driver inspect "$mixed_ids" >/dev/null
    republished=$(run_driver publish-snapshot "$predecessor" | extract_id)
    [[ "$republished" == "$predecessor" ]]
    run_driver wait "$predecessor,$blocked" complete 180000 >/dev/null
    run_driver inspect "$predecessor,$blocked" >/dev/null
    run_driver verify "file:///live/$RUN_ID-s6-ready.bin" 1024 17 >/dev/null
    run_driver verify "file:///live/$RUN_ID-s6-pred.bin" 1024 34 >/dev/null
    run_driver verify "file:///live/$RUN_ID-s6-blocked.bin" 1024 35 >/dev/null
    run_driver verify-result "$local_read" 1024 97 >/dev/null
    run_driver verify "file:///live/$RUN_ID-s6-local-write.bin" 1024 99 >/dev/null
    run_driver verify "file:///live/$RUN_ID-s6-composite-a.bin" 1024 161 >/dev/null
    run_driver verify "file:///live/$RUN_ID-s6-composite-b.bin" 1024 178 >/dev/null
    run_driver verify "sqlite:///live/$RUN_ID-s6-unknown" 1024 97 >/dev/null
    record "Mixed unit IDs: \`$mixed_ids\`. Every executable unit completed with checked bytes; dependency and Composite legality held; unknown size completed without fabricated demand; 128 KiB unit remained legally parked against 16/24/32 KiB capacities. Decision snapshots are recorded above."

    record "## 7. Cancellation before execution and at Scheduled/Executing"
    export LABIOS_WORKER_1_CAPACITY=10GB LABIOS_WORKER_2_CAPACITY=50GB LABIOS_WORKER_3_CAPACITY=200GB LABIOS_TEST_EXECUTION_DELAY_MS=3000
    compose stop -t 2 worker-1 worker-2 worker-3 >>"$RUN_DIR/compose.log" 2>&1
    cancel_parked=$(run_driver submit-write "file:///live/$RUN_ID-s7-parked.bin" 1024 113 | extract_id)
    wait_status "$cancel_parked" parked 30
    parked_cancel=$(run_driver cancel "$cancel_parked")
    printf '%s\n' "$parked_cancel" | grep -q 'CANCEL_WON=1'

    compose up -d --force-recreate --wait worker-1 >>"$RUN_DIR/compose.log" 2>&1
    compose pause worker-1 >>"$RUN_DIR/compose.log" 2>&1
    cancel_scheduled=$(run_driver submit-write "file:///live/$RUN_ID-s7-scheduled.bin" 1024 114 | extract_id)
    wait_status "$cancel_scheduled" scheduled 30
    scheduled_cancel=$(run_driver cancel "$cancel_scheduled")
    printf '%s\n' "$scheduled_cancel" | grep -q 'CANCEL_WON=1'
    compose unpause worker-1 >>"$RUN_DIR/compose.log" 2>&1
    sleep 4
    if compose exec -T worker-1 test -e "/labios/data/live/$RUN_ID-s7-scheduled.bin"; then
        echo "cancelled Scheduled label produced an external file" >&2
        exit 1
    fi

    executing=$(run_driver submit-write "file:///live/$RUN_ID-s7-executing.bin" 1024 115 | extract_id)
    wait_status "$executing" executing 30
    executing_cancel=$(run_driver cancel "$executing")
    printf '%s\n' "$executing_cancel" | grep -q 'CANCEL_WON=0'
    printf '%s\n' "$executing_cancel" | grep -q ":complete:"
    run_driver verify "file:///live/$RUN_ID-s7-executing.bin" 1024 115 >/dev/null
    record "Terminal public C++ API outcomes: parked cancellation $cancel_parked=cancelled; Scheduled winner $cancel_scheduled=cancelled with no file; Executing winner $executing=complete and cancellation TooLate."

    compose logs --no-color >"$RUN_DIR/services.log" 2>&1 || true
    record "## Pass result"
    record "PASS — all seven scenarios completed from fresh volumes. Full service and Compose logs are adjacent to this file."
done
