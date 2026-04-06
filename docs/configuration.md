# LABIOS Configuration Reference

LABIOS loads configuration from a TOML file, then applies environment variable
overrides. Runtime parameters can also be changed dynamically via the
`config.set()` API.

Precedence (highest to lowest):
1. `client.set_config()` at runtime
2. Environment variables (`LABIOS_*`)
3. TOML config file (`conf/labios.toml`)
4. Compiled defaults

## TOML Configuration File

The default config file is `conf/labios.toml`. Pass a custom path when
connecting:

```cpp
auto cfg = labios::load_config("/path/to/custom.toml");
```

```python
client = labios.connect("/path/to/custom.toml")
```

### Complete TOML Reference

```toml
# ─── Connection ──────────────────────────────────────────────
[nats]
url = "nats://localhost:4222"          # NATS server URL

[redis]
host = "localhost"                      # DragonflyDB host
port = 6379                             # DragonflyDB port

# ─── Worker Identity ────────────────────────────────────────
[worker]
id = "worker-1"                         # Unique worker ID
speed = 1                               # Processing speed score (1-10)
energy = 1                              # Energy consumption (1-10, lower = greener)
capacity = "10GB"                       # Available storage capacity

[worker.tier]
level = 0                               # 0=Databot, 1=Pipeline, 2=Agentic

# ─── Label Granularity ──────────────────────────────────────
[label]
min_size = "64KB"                       # Minimum label data chunk
max_size = "1MB"                        # Maximum label data chunk

# ─── Small-I/O Cache ────────────────────────────────────────
[cache]
flush_interval_ms = 1000               # Timer-based flush interval
read_policy = "miss"                    # "miss" = read-through on miss
                                        # "always" = cache every read result

# ─── POSIX Intercept ────────────────────────────────────────
[intercept]
prefix = "/labios"                      # Paths under this prefix get intercepted
retry_count = 3                         # Connection retries before FS fallback

# ─── Dispatcher ──────────────────────────────────────────────
[dispatcher]
batch_size = 100                        # Labels per scheduling batch
batch_timeout_ms = 50                   # Max wait before flushing partial batch
aggregation_enabled = true              # Enable write aggregation in shuffler
reply_timeout_ms = 5000                 # Timeout waiting for worker reply

# ─── Scheduler ───────────────────────────────────────────────
[scheduler]
policy = "round-robin"                  # round-robin | random | constraint | minmax
profile_path = ""                       # Path to weight profile TOML (constraint/minmax)

# ─── Elastic Scaling ────────────────────────────────────────
[elastic]
enabled = false                         # Enable auto-scaling
docker_socket = "/var/run/docker.sock"  # Docker Engine API socket
check_interval_s = 10                   # How often to evaluate scaling decisions
idle_suspend_timeout_s = 60             # Idle time before suspending a worker
energy_budget = 0                       # Max total energy score (0 = unlimited)

[elastic.per_tier]
tier_0_min = 1                          # Minimum Databot workers
tier_0_max = 10                         # Maximum Databot workers
tier_1_min = 0                          # Minimum Pipeline workers
tier_1_max = 5                          # Maximum Pipeline workers
tier_2_min = 0                          # Minimum Agentic workers
tier_2_max = 3                          # Maximum Agentic workers
```

### Size Strings

Size fields accept human-readable strings: `64KB`, `1MB`, `2GB`, `10TB`. The
parser is case-insensitive. Plain integers are treated as bytes.

## Weight Profiles

Weight profiles control how the constraint and MinMax schedulers score workers.
Four profiles ship with LABIOS in `conf/profiles/`:

### `low_latency.toml`

Prioritizes availability and low queue load for latency-sensitive workloads.

```toml
[weights]
availability = 0.50
load = 0.35
speed = 0.15
```

### `high_bandwidth.toml`

Prioritizes raw processing speed for throughput-bound workloads.

```toml
[weights]
capacity = 0.15
load = 0.15
speed = 0.70
```

### `energy_savings.toml`

Prioritizes energy efficiency for green computing and cost reduction.

```toml
[weights]
capacity = 0.15
load = 0.20
speed = 0.15
energy = 0.50
```

### `agentic.toml`

Balanced profile for agent workloads with tier, skills, and reasoning weights.

```toml
[weights]
availability = 0.30
capacity = 0.10
load = 0.10
speed = 0.10
tier = 0.20
skills = 0.10
reasoning = 0.10
```

### Custom Profiles

Create a new TOML file with a `[weights]` section. All weight values must
sum to 1.0. Available weight dimensions:

| Weight | Description |
|--------|-------------|
| `availability` | Worker is online and accepting labels |
| `capacity` | Free storage space relative to total |
| `load` | Inverse of current queue depth |
| `speed` | Processing speed score |
| `energy` | Inverse energy consumption (lower energy = higher score) |
| `tier` | Worker tier match (Databot, Pipeline, Agentic) |
| `skills` | Worker skill coverage for agentic operations |
| `reasoning` | Worker reasoning capability score |
| `compute` | Raw compute capacity |

## Runtime Configuration

The `config.set()` API changes parameters without restarting. Changes apply
to the next label batch processed by the dispatcher.

Settable parameters:

| Key | Type | Description |
|-----|------|-------------|
| `batch_size` | integer | Labels per scheduling batch |
| `batch_timeout_ms` | integer | Batch flush timeout |
| `scheduler_policy` | string | Scheduling policy name |
| `aggregation_enabled` | bool | Shuffler aggregation toggle |
| `reply_timeout_ms` | integer | Worker reply timeout |
| `cache_flush_interval_ms` | integer | Cache flush timer |
| `cache_read_policy` | string | Cache read behavior |

```cpp
client.set_config("scheduler_policy", "constraint");
client.set_config("batch_size", "200");
```

```python
client.set_config("scheduler_policy", "constraint")
```

## Environment Variable Reference

Every config field has a corresponding environment variable. Variables use the
`LABIOS_` prefix with underscores replacing dots.

| TOML Path | Environment Variable | Default |
|-----------|---------------------|---------|
| `nats.url` | `LABIOS_NATS_URL` | `nats://localhost:4222` |
| `redis.host` | `LABIOS_REDIS_HOST` | `localhost` |
| `redis.port` | `LABIOS_REDIS_PORT` | `6379` |
| `worker.id` | `LABIOS_WORKER_ID` | `worker-1` |
| `worker.speed` | `LABIOS_WORKER_SPEED` | `1` |
| `worker.energy` | `LABIOS_WORKER_ENERGY` | `1` |
| `worker.capacity` | `LABIOS_WORKER_CAPACITY` | `10GB` |
| `worker.tier.level` | `LABIOS_WORKER_TIER` | `0` |
| `label.min_size` | `LABIOS_LABEL_MIN_SIZE` | `64KB` |
| `label.max_size` | `LABIOS_LABEL_MAX_SIZE` | `1MB` |
| `cache.flush_interval_ms` | `LABIOS_CACHE_FLUSH_MS` | `1000` |
| `cache.read_policy` | `LABIOS_CACHE_READ_POLICY` | `miss` |
| `intercept.prefix` | `LABIOS_INTERCEPT_PREFIX` | `/labios` |
| `intercept.retry_count` | `LABIOS_INTERCEPT_RETRY` | `3` |
| `dispatcher.batch_size` | `LABIOS_BATCH_SIZE` | `100` |
| `dispatcher.batch_timeout_ms` | `LABIOS_BATCH_TIMEOUT_MS` | `50` |
| `dispatcher.aggregation_enabled` | `LABIOS_AGGREGATION_ENABLED` | `true` |
| `dispatcher.reply_timeout_ms` | `LABIOS_REPLY_TIMEOUT_MS` | `5000` |
| `scheduler.policy` | `LABIOS_SCHEDULER_POLICY` | `round-robin` |
| `scheduler.profile_path` | `LABIOS_SCHEDULER_PROFILE` | (none) |
| `elastic.enabled` | `LABIOS_ELASTIC_ENABLED` | `false` |
| `elastic.docker_socket` | `LABIOS_ELASTIC_DOCKER_SOCKET` | `/var/run/docker.sock` |
| `elastic.check_interval_s` | `LABIOS_ELASTIC_CHECK_INTERVAL_S` | `10` |
