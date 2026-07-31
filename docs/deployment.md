# LABIOS Deployment Guide

## Supported classification

The repository ships one supported deployment shape: a **single-host reference
deployment** using Docker Compose. It is intended for development, correctness
testing, and demonstrations. Production hardening, Kubernetes manifests,
leader election, and a verified multi-node topology are future work.

## Default topology

```text
localhost
  NATS JetStream ─┬─ dispatcher ─┬─ worker-1 ─┐
  DragonflyDB ────┤              ├─ worker-2 ─┼─ shared worker-data volume
  Redis fixture ──┘              └─ worker-3 ─┘
                         manager       MCP Label I/O frontend
```

| Service | Purpose | Host port |
|---|---|---|
| `nats` | Durable label transport and control-plane messaging | `127.0.0.1:4222`; monitoring `:8222` |
| `redis` | Internal DragonflyDB warehouse and catalog plumbing | `127.0.0.1:6379` |
| `redis-kv` | Development fixture representing a user's external `kv://` backend | `127.0.0.1:6380` |
| `manager` | Worker registry | none |
| `dispatcher` | Label admission, shuffling, and scheduling | none |
| `worker-1`…`worker-3` | Tier-1 file, SQLite, and configured KV execution | none |
| `mcp` | Public Label I/O frontend with the packaged native Python SDK; no direct-store adapter or worker volume | none |
| `test` | One-shot test/demo image, enabled only when explicitly run | none |

The internal DragonflyDB warehouse is not a user storage backend. The separate
`redis-kv` container is only a local simulation of user infrastructure. The MCP
service receives runtime endpoints only as public Python `Client` configuration;
its application code does not import Redis/NATS adapters or address internal
keys/subjects. Build its self-contained image with
`docker build -f mcp/Dockerfile -t labios-mcp .` from the repository root.

## Start, inspect, and stop

```bash
docker compose config --quiet
docker compose up -d --build --wait
docker compose ps
docker compose exec dispatcher labios-demo
docker compose down
```

Delete all persisted reference data and any one-shot profile containers with
`docker compose --profile test down -v --remove-orphans`.

### Health and restart behavior

NATS, DragonflyDB, the Redis fixture, manager, dispatcher, workers, and MCP have
health checks. Daemons use `restart: unless-stopped`; the `test` profile is a
non-daemon and uses `restart: no`. C++ services do not become ready until their
startup connections and subscriptions have succeeded.

```bash
curl -fsS http://127.0.0.1:8222/healthz
docker compose exec redis redis-cli ping
docker compose logs --tail=50 dispatcher manager
docker compose logs --tail=50 worker-1 worker-2 worker-3
```

## Persistence

Named volumes survive `docker compose restart` and `docker compose down`:

| Volume | Contents |
|---|---|
| `nats-data` | JetStream stream and consumer state |
| `dragonfly-data` | DragonflyDB snapshots containing warehouse/catalog state |
| `redis-kv-data` | Append-only state for the external Redis development fixture |
| `worker-data` | File backend objects and the shared SQLite database |

NATS uses `/data` as its JetStream store. DragonflyDB uses `/data` and writes a
snapshot during graceful shutdown. Use the configured grace periods; do not
force-kill plumbing services and infer durability from that.

A simple restart check is:

```bash
docker compose exec redis redis-cli set labios:deployment:probe durable
docker compose restart nats redis
docker compose restart manager dispatcher worker-1 worker-2 worker-3 mcp
docker compose up -d --wait
docker compose exec redis redis-cli get labios:deployment:probe
curl -fsS "http://127.0.0.1:8222/jsz?streams=true"
```

The dependent daemons are restarted because the current C++ connections do not
provide a verified transparent plumbing-reconnect guarantee. The key should be
`durable`, and the NATS response should include the `LABIOS_LABELS` stream after
labels have been submitted.

## Shared worker storage

Every default worker mounts the same `worker-data` volume at `/labios/data`.
This matches the worker registry's `Shared` file and SQLite attachment: an
operation scheduled on any worker can see data written by another worker. The
SQLite database is `/labios/data/labios.db`.

The golden-path demo and the `[deployment]` data-path test issue at least 20
round-robin operations over both backends and verify read-back:

```bash
docker compose run --rm --build test labios-data-path-test "[deployment]"
```

### Node-local storage is not the default

Giving each worker a different volume changes the topology to node-local
storage. Such workers must advertise distinct locality domains, and placement
must enforce hard source/destination locality. The repository does not ship or
validate that topology by default. Merely replacing `worker-data` with per-worker
volumes would contradict the current `Shared` attachment and can produce
not-found reads; do not do it.

## Manual worker changes

Worker IDs are numeric (`Config::worker_id` is an integer). A manually added
single-host worker must use a unique positive integer and mount the same shared
volume, for example:

```yaml
worker-4:
  build: { context: ., target: worker }
  environment:
    LABIOS_NATS_URL: nats://nats:4222
    LABIOS_REDIS_HOST: redis
    LABIOS_WORKER_ID: "4"
    LABIOS_WORKER_TIER: "1"
    LABIOS_WORKER_SPEED: "2"
    LABIOS_WORKER_ENERGY: "2"
    LABIOS_WORKER_CAPACITY: 50GB
    LABIOS_WORKER_IDLE_TIMEOUT_MS: "86400000"
  volumes:
    - worker-data:/labios/data
```

It also needs the same healthy-service dependencies as the existing workers.
The elasticity code is a disabled research prototype; automatic provisioning
is not part of the supported reference deployment.

## Security limitations

The Compose file deliberately remains easy to run locally, but its defaults are
unsafe for production:

- NATS has no account authentication, authorization, or TLS.
- DragonflyDB and the external Redis fixture have no password, ACL, or TLS.
- Monitoring and data-plane ports are exposed on localhost.
- Images and dependencies are not configured here for production secret or
  certificate rotation.
- The MCP stdio endpoint can submit I/O and therefore needs agent-side access control before production exposure; Compose does not provide it.

Binding to localhost reduces accidental network exposure but is not production
hardening. A production design must add authentication, authorization, TLS,
secret management, backups, monitoring, and recovery testing before exposing
any endpoint.

## Multi-node and Kubernetes status

No multi-node Compose, Kubernetes, Helm, or operator deployment is currently
shipped or validated. Multi-node work additionally requires truthful locality,
shared/external storage choices, durable plumbing design, authentication/TLS,
and high-availability decisions. Treat it as future work, not as a deployment
mode implied by the component architecture.
