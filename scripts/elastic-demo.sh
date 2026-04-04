#!/usr/bin/env bash
# scripts/elastic-demo.sh
# M4 Elastic Workers Demo
set -euo pipefail

COMPOSE_FILE="docker-compose.elastic.yml"
LABEL_COUNT="${1:-10000}"

echo "=== M4 Elastic Workers Demo ==="
echo "Labels: $LABEL_COUNT"
echo ""

echo "[1/5] Building and starting LABIOS (1 worker)..."
docker compose -f "$COMPOSE_FILE" build --quiet
docker compose -f "$COMPOSE_FILE" down -v 2>/dev/null || true
docker compose -f "$COMPOSE_FILE" up -d
sleep 8

echo "[2/5] System ready. Current workers:"
docker ps --filter "ancestor=labios-worker" --format "  {{.Names}} ({{.Status}})" 2>/dev/null || true
echo ""

echo "[3/5] Flooding queue with $LABEL_COUNT labels..."
docker compose -f "$COMPOSE_FILE" run --rm --entrypoint "labios-elastic-flood-test $LABEL_COUNT" test &
FLOOD_PID=$!

echo "[4/5] Watching for elastic scale-up..."
for i in $(seq 1 60); do
    elastic_count=$(docker ps --filter "label=labios.elastic=true" --format "{{.Names}}" 2>/dev/null | wc -l)
    total_workers=$(docker ps --filter "ancestor=labios-worker" --format "{{.Names}}" 2>/dev/null | wc -l)
    printf "  [%2d] Total workers: %d (elastic: %d)\n" "$i" "$total_workers" "$elastic_count"
    if [ "$total_workers" -ge 3 ]; then
        echo "  Scale-up complete!"
        break
    fi
    sleep 2
done
echo ""

echo "[5/5] Waiting for queue drain and scale-down..."
for i in $(seq 1 120); do
    elastic_count=$(docker ps --filter "label=labios.elastic=true" --format "{{.Names}}" 2>/dev/null | wc -l)
    total_workers=$(docker ps --filter "ancestor=labios-worker" --format "{{.Names}}" 2>/dev/null | wc -l)
    printf "  [%3d] Total workers: %d (elastic: %d)\n" "$i" "$total_workers" "$elastic_count"
    if [ "$elastic_count" -eq 0 ] && [ "$total_workers" -le 1 ]; then
        echo "  Scale-down complete! Back to 1 worker."
        break
    fi
    sleep 2
done

wait "$FLOOD_PID" 2>/dev/null || true

echo ""
echo "=== Demo Complete ==="
echo "Manager logs:"
docker compose -f "$COMPOSE_FILE" logs manager 2>/dev/null | grep -E "\[elastic\]|commissioned|decommissioned|resumed|elastic mode" | tail -20

docker compose -f "$COMPOSE_FILE" down -v
