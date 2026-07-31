# LABIOS Configuration Reference

LABIOS constructs a process-local `Config` from compiled defaults, an optional
TOML file, and implemented environment overrides (environment wins). Each
service reads `LABIOS_CONFIG_PATH` to choose its TOML file.

`Client::set_config()` changes only that client's in-process `Config` object. It
does **not** update dispatcher, manager, worker, or other client configuration,
and it is not a distributed control-plane API. Restart a service with a TOML or
environment change to configure that service.

## Reference TOML shape

The checked-in defaults are in `conf/labios.toml`. These are the keys currently
read by `load_config`:

```toml
[nats]
url = "nats://localhost:4222"
max_deliver = 5
ack_wait_ms = 10000

[redis]
host = "localhost"
port = 6379

[worker]
id = 0                    # integer, not a name such as "worker-4"
speed = 1
energy = 1
tier = 0                  # 0 Databot, 1 Pipeline, 2 Agentic metadata
capacity = "1GB"

[manager]
max_worker_capacity = "1024GB"

[client]
reply_timeout_ms = 30000

[label]
min_size = "64KB"
max_size = "1MB"

[cache]
flush_interval_ms = 500
default_read_policy = "read-through"

[intercept]
prefixes = ["/labios"]

[dispatcher]
batch_size = 100
batch_timeout_ms = 50
aggregation_enabled = true
dep_granularity = "per-file"

[scheduler]
policy = "round-robin"    # round-robin | random | constraint | minmax
profile_path = ""
worker_refresh_ms = 5000

[elastic]
enabled = false
min_workers = 1
max_workers = 10
pressure_threshold = 5
worker_idle_timeout_ms = 30000
decommission_timeout_ms = 60000
commission_cooldown_ms = 5000
eval_interval_ms = 2000
docker_socket = "/var/run/docker.sock"
docker_image = ""
docker_network = ""
elastic_worker_speed = 3
elastic_worker_energy = 3
elastic_worker_capacity = "50GB"
min_databot_workers = 1
max_databot_workers = 10
min_pipeline_workers = 0
max_pipeline_workers = 5
min_agentic_workers = 0
max_agentic_workers = 2
```

The elasticity implementation is disabled in the supported reference
deployment and remains a research prototype.

Size strings accept `B`, `KB`, `MB`, and `GB` suffixes, case-insensitively.
Plain numbers are bytes. (`TB` is not currently parsed as a special suffix.)

## Implemented environment overrides

Only the names below are read by the current configuration loader; there is no
generic dots-to-underscores mapping.

| Area | Environment variables |
|---|---|
| Config file | `LABIOS_CONFIG_PATH` |
| NATS | `LABIOS_NATS_URL`, `LABIOS_NATS_MAX_DELIVER`, `LABIOS_NATS_ACK_WAIT_MS` |
| DragonflyDB | `LABIOS_REDIS_HOST`, `LABIOS_REDIS_PORT` |
| Worker | `LABIOS_WORKER_ID`, `LABIOS_WORKER_SPEED`, `LABIOS_WORKER_ENERGY`, `LABIOS_WORKER_TIER`, `LABIOS_WORKER_CAPACITY`, `LABIOS_WORKER_IDLE_TIMEOUT_MS` |
| Manager | `LABIOS_MAX_WORKER_CAPACITY` |
| Label/cache | `LABIOS_LABEL_MIN_SIZE`, `LABIOS_LABEL_MAX_SIZE`, `LABIOS_CACHE_FLUSH_MS`, `LABIOS_CACHE_READ_POLICY` |
| POSIX intercept | `LABIOS_INTERCEPT_PREFIXES` (comma-separated) |
| Client | `LABIOS_REPLY_TIMEOUT_MS` |
| Dispatcher | `LABIOS_DISPATCHER_BATCH_SIZE`, `LABIOS_DISPATCHER_BATCH_TIMEOUT_MS`, `LABIOS_DISPATCHER_AGGREGATION`, `LABIOS_DISPATCHER_DEP_GRANULARITY` |
| Scheduler | `LABIOS_SCHEDULER_POLICY`, `LABIOS_SCHEDULER_PROFILE`, `LABIOS_SCHEDULER_WORKER_REFRESH_MS` |
| Elastic prototype | `LABIOS_ELASTIC_ENABLED`, `LABIOS_ELASTIC_MIN_WORKERS`, `LABIOS_ELASTIC_MAX_WORKERS`, `LABIOS_DOCKER_SOCKET`, `LABIOS_DOCKER_IMAGE`, `LABIOS_DOCKER_NETWORK`, `LABIOS_ELASTIC_WORKER_SPEED`, `LABIOS_ELASTIC_WORKER_ENERGY`, `LABIOS_ELASTIC_WORKER_CAPACITY` |

The optional external user-Redis adapter is selected by worker-service
variables `LABIOS_KV_HOST` and `LABIOS_KV_PORT`. These are backend construction
settings, not fields in `Config`, and must never point at LABIOS's internal
DragonflyDB warehouse.

Boolean dispatcher aggregation accepts `true` or `1`; elastic enabled accepts
`true` or `1`. Invalid integer environment values fall back to the prior value.

## Weight profiles

`conf/profiles/` contains `low_latency.toml`, `high_bandwidth.toml`,
`energy_savings.toml`, and `agentic.toml`. The current C++ loader consumes these
weights:

- `availability`
- `capacity`
- `load`
- `speed`
- `energy`
- `tier`

A profile may contain research fields such as `skills`, `compute`, or
`reasoning`, but the current `WeightProfile` loader does not apply them. Set a
profile for a service before startup, for example:

```bash
LABIOS_SCHEDULER_POLICY=constraint \
LABIOS_SCHEDULER_PROFILE=/etc/labios/profiles/low_latency.toml \
docker compose up -d --build --wait
```

## Local runtime setters

The following keys are accepted by `Config::set`, and therefore by
`Client::set_config`:

- `batch_size`
- `batch_timeout_ms`
- `scheduler_policy`
- `aggregation_enabled`
- `reply_timeout_ms`
- `cache_flush_interval_ms`
- `cache_read_policy`

Example:

```text
bool changed = client.set_config("reply_timeout_ms", "45000");
```

This changes local client state only. In particular, setting
`scheduler_policy` on a client does not reconfigure the independently running
dispatcher.

## Observe query routes

`Client::observe()` prepends the `observe://` scheme. Pass the route without the
scheme:

```text
auto health = client.observe("system/health");
auto depth = client.observe("queue/depth");
auto scores = client.observe("workers/scores");
auto count = client.observe("workers/count");
auto channels = client.observe("channels/list");
auto workspaces = client.observe("workspaces/list");
auto config = client.observe("config/current");
auto location = client.observe("data/location");
```

These are the registered route names. Forms such as
`observe://system/queue_depth`, passed to `Client::observe`, are incorrect
because the client would prepend a second scheme and the route itself does not
match.

## Completion timeout semantics

`reply_timeout_ms` bounds synchronous reply waiting. `CompletionState::Timeout`
is distinct from failure and cancellation. C++ synchronous `write`, `read`, and
`wait` surface a typed `CompletionError` whose `state()` is `Timeout` and whose
message explains that the label remains active. Call `test()` or `wait_for()` to
observe it later, or call `cancel(label_id)` explicitly while cancellation is
still legal. A timeout never implies cancellation or rollback.
