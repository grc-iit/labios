# LABIOS Deployment Guide

This guide covers the Docker Compose topology, scaling workers, environment
variable overrides, and multi-node deployment.

## Docker Compose Topology

The default `docker-compose.yml` starts 10 services:

```
┌─────────┐     ┌──────────┐     ┌─────────┐
│  NATS   │     │ Dragonfly│     │ Redis-KV│
│ :4222   │     │  :6379   │     │  :6380  │
│ JetStream│    │ warehouse│     │ kv://   │
└────┬────┘     └────┬─────┘     └────┬────┘
     │               │                │
     └───────┬───────┘                │
             │                        │
     ┌───────v───────┐                │
     │  Dispatcher   │                │
     │  shuffler +   │                │
     │  scheduler    │                │
     └───────┬───────┘                │
             │                        │
     ┌───────v───────┐                │
     │   Manager     │                │
     │  worker reg   │                │
     │  elastic ctrl │                │
     └───┬───┬───┬───┘                │
         │   │   │                    │
    ┌────v┐ ┌v──┐ ┌v────┐            │
    │W-1  │ │W-2│ │W-3  │────────────┘
    │fast │ │mid│ │bulk │
    └─────┘ └───┘ └─────┘

    ┌─────────┐  ┌─────────┐
    │  Test   │  │  MCP    │
    │ runner  │  │ server  │
    └─────────┘  └─────────┘
```

### Service Descriptions

| Service | Image | Ports | Purpose |
|---------|-------|-------|---------|
| nats | nats:2.10-alpine | 4222 (client), 8222 (monitoring) | Message broker. All labels flow through NATS subjects with JetStream persistence. |
| redis | docker.dragonflydb/dragonfly | 6379 | Internal warehouse and metadata store (DragonflyDB). Stages data in transit, tracks label status. This is internal plumbing, not a user-facing backend. |
| redis-kv | redis:7-alpine | 6380 | External KV backend for `kv://` URIs. Simulates user infrastructure for development. |
| dispatcher | labios-dispatcher | internal | Processes label batches: OBSERVE handler, shuffler (aggregation, dependency detection), scheduler (4 policies), continuation processor, telemetry publisher. |
| worker-1 | labios-worker | internal | Speed-optimized: speed=5, energy=1, capacity=10GB. |
| worker-2 | labios-worker | internal | Balanced: speed=3, energy=3, capacity=50GB. |
| worker-3 | labios-worker | internal | Capacity-optimized: speed=1, energy=5, capacity=200GB. |
| manager | labios-manager | internal | Worker Manager: bucket-sorted registry, score computation, elastic scaling decisions. |
| test | labios-test | internal | Runs smoke and integration tests. Exits after completion. |
| mcp | labios-mcp | internal | MCP server for coding agent integration. Connects via Docker stdio. |

### Starting and Stopping

```bash
docker compose up -d              # Start everything
docker compose ps                 # Check health status
docker compose logs dispatcher    # View dispatcher logs
docker compose down               # Stop (keep volumes)
docker compose down -v            # Stop and remove data volumes
```

### Health Checks

Every service has a Docker health check:

```bash
# NATS
curl -sf http://localhost:8222/healthz

# DragonflyDB
docker compose exec redis redis-cli ping

# Workers (check NATS subscription)
docker compose logs worker-1 | grep "subscribed"

# MCP server
docker compose exec mcp python -c "import labios_mcp; print('ok')"
```

## Scaling Workers

### Adding Workers to Docker Compose

Add a new service block to `docker-compose.yml`:

```yaml
worker-4:
  build:
    context: .
    target: worker
  command: ["labios-worker"]
  environment:
    LABIOS_NATS_URL: nats://nats:4222
    LABIOS_REDIS_HOST: redis
    LABIOS_WORKER_ID: worker-4
    LABIOS_WORKER_SPEED: 8
    LABIOS_WORKER_ENERGY: 2
    LABIOS_WORKER_CAPACITY: 500GB
    LABIOS_WORKER_TIER: 1
  volumes:
    - worker-4-data:/labios/data
  depends_on:
    nats: { condition: service_healthy }
    redis: { condition: service_healthy }
```

Workers self-register with the Manager via NATS on startup. No dispatcher
configuration changes needed.

### Elastic Scaling

When elastic scaling is enabled, the Manager automatically commissions and
decommissions workers based on queue depth and energy budget:

```toml
[elastic]
enabled = true
docker_socket = "/var/run/docker.sock"
check_interval_s = 10

[elastic.per_tier]
tier_0_min = 1
tier_0_max = 10
tier_1_min = 0
tier_1_max = 5
tier_2_min = 0
tier_2_max = 3
```

The Manager uses the Docker Engine API to launch and stop worker containers.
Commission happens when queue depth exceeds a threshold. Decommission happens
when workers are idle beyond the suspend timeout.

## Environment Variables

All configuration can be overridden via environment variables. These take
precedence over the TOML config file.

### Connection

| Variable | Default | Description |
|----------|---------|-------------|
| `LABIOS_NATS_URL` | `nats://localhost:4222` | NATS server URL |
| `LABIOS_REDIS_HOST` | `localhost` | DragonflyDB host |
| `LABIOS_REDIS_PORT` | `6379` | DragonflyDB port |

### Worker Identity

| Variable | Default | Description |
|----------|---------|-------------|
| `LABIOS_WORKER_ID` | `worker-1` | Unique worker identifier |
| `LABIOS_WORKER_SPEED` | `1` | Processing speed score (1-10) |
| `LABIOS_WORKER_ENERGY` | `1` | Energy consumption score (1-10, lower is greener) |
| `LABIOS_WORKER_CAPACITY` | `10GB` | Available storage capacity |
| `LABIOS_WORKER_TIER` | `0` | Worker tier: 0=Databot, 1=Pipeline, 2=Agentic |

### Dispatcher

| Variable | Default | Description |
|----------|---------|-------------|
| `LABIOS_BATCH_SIZE` | `100` | Labels per batch before scheduling |
| `LABIOS_BATCH_TIMEOUT_MS` | `50` | Max wait time before flushing an incomplete batch |
| `LABIOS_SCHEDULER_POLICY` | `round-robin` | Scheduling policy: round-robin, random, constraint, minmax |
| `LABIOS_SCHEDULER_PROFILE` | (none) | Path to weight profile TOML for constraint/minmax policies |
| `LABIOS_AGGREGATION_ENABLED` | `true` | Enable shuffler aggregation of adjacent writes |
| `LABIOS_REPLY_TIMEOUT_MS` | `5000` | Timeout waiting for worker reply |

### Label and Cache

| Variable | Default | Description |
|----------|---------|-------------|
| `LABIOS_LABEL_MIN_SIZE` | `64KB` | Minimum label data granularity |
| `LABIOS_LABEL_MAX_SIZE` | `1MB` | Maximum label data granularity |
| `LABIOS_CACHE_FLUSH_MS` | `1000` | Small-I/O cache flush interval |
| `LABIOS_CACHE_READ_POLICY` | `miss` | Cache read policy: `miss` (read-through on miss) or `always` (always cache) |

### POSIX Intercept

| Variable | Default | Description |
|----------|---------|-------------|
| `LABIOS_INTERCEPT_PREFIX` | `/labios` | Path prefix for intercepted I/O (others pass through) |
| `LABIOS_INTERCEPT_RETRY` | `3` | Connection retry count before falling back to real FS |

## Multi-Node Deployment

LABIOS components communicate exclusively through NATS and DragonflyDB. Any
component can run on any host as long as it can reach these two services.

### Example: 3-Node Cluster

```
Node 1 (control):  NATS, DragonflyDB, Dispatcher, Manager
Node 2 (compute):  Worker-1, Worker-2, Worker-3
Node 3 (compute):  Worker-4, Worker-5, Worker-6
```

On Node 1, start NATS and DragonflyDB. On Nodes 2 and 3, start workers with
`LABIOS_NATS_URL=nats://node1:4222` and `LABIOS_REDIS_HOST=node1`.

### Kubernetes

LABIOS does not yet ship Helm charts but the architecture maps directly to K8s:

- NATS: use the [nats-operator](https://github.com/nats-io/nats-operator) or NATS Helm chart
- DragonflyDB: use the [Dragonfly operator](https://github.com/dragonflydb/dragonfly-operator)
- Dispatcher: single-replica Deployment
- Manager: single-replica Deployment (leader election via NATS)
- Workers: StatefulSet with per-pod PVCs for data volumes
- MCP server: sidecar in the agent pod or standalone Deployment

## Volumes and Data Layout

Each worker mounts a persistent volume at `/labios/data`. Files written through
`file://` URIs land here. The PosixBackend maps URI paths to subdirectories
under this mount.

```
/labios/data/
├── hello.dat         # from file:///data/hello.dat
├── output/
│   └── results.bin   # from file:///data/output/results.bin
└── .labios/
    └── sqlite/       # SQLite backend databases
```

The warehouse (DragonflyDB) stores data in transit under `labios:data:{id}`
keys. This is ephemeral staging, not permanent storage. The catalog stores
metadata under `labios:catalog:*` keys.

## Monitoring

### NATS Monitoring

NATS exposes HTTP monitoring on port 8222:

```bash
curl http://localhost:8222/connz    # Active connections
curl http://localhost:8222/subsz    # Subscriptions
curl http://localhost:8222/jsz      # JetStream stats
```

### DragonflyDB Monitoring

```bash
docker compose exec redis redis-cli info memory
docker compose exec redis redis-cli info stats
docker compose exec redis redis-cli dbsize
```

### LABIOS Telemetry

The dispatcher publishes telemetry to NATS subject `labios.telemetry`. Use
the observability API to query runtime metrics:

```python
import labios
client = labios.connect_to("nats://localhost:4222", "localhost", 6379)
print(client.observe("observe://system/metrics"))
print(client.observe("observe://system/queue_depth"))
print(client.observe("observe://system/worker_scores"))
```
