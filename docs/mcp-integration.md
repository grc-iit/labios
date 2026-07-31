# LABIOS MCP Integration Guide

LABIOS ships an MCP (Model Context Protocol) server that is a public **Label
I/O frontend**. Core store, retrieve, and process requests compile to versioned,
typed labels and traverse:

```text
MCP client -> packaged Python Client -> dispatcher -> scheduler -> worker
           -> external file/SQLite/KV backend
```

The MCP process does not import Redis or NATS clients, request private subjects,
or mount a worker volume. Its Python `Client` uses LABIOS runtime endpoints in
the same way as every public SDK client. Runtime observations are Observe labels;
label inspection uses `Client.inspect_label`.

Workspace-backed knowledge remains explicitly pending Prompt 14. LABIOS is not
a shell, arbitrary compute runtime, or generic tool broker.

## Start and connect

```bash
cd /path/to/labios
docker compose up -d --build --wait
docker compose ps
```

Configure an MCP client with an absolute path:

```json
{
  "mcpServers": {
    "labios": {
      "command": "/absolute/path/to/labios/mcp/connect.sh"
    }
  }
}
```

`connect.sh` attaches stdio to the packaged MCP container. The image is built
from the repository root so it contains the native Python extension, the
`labios` package, generated registry-v2 bindings, and FlatBuffers 24.3.25.
The standalone, executable build path is:

```bash
docker build -f mcp/Dockerfile -t labios-mcp .
docker run --rm labios-mcp python -c \
  'import flatbuffers, labios, labios_mcp; print(labios.Operation)'
```

## Stable response envelope

Every tool returns one JSON object in MCP text content.

Successful terminal I/O:

```json
{
  "ok": true,
  "status": "completed",
  "label_id": 123,
  "completion": {"state": "complete", "lifecycle": "completed"}
}
```

Failures and nonterminal projections use:

```json
{
  "ok": false,
  "status": "timeout",
  "label_id": 123,
  "error": {
    "kind": "timeout",
    "category": "TIMEOUT",
    "message": "wait deadline elapsed; operation remains active",
    "retryable": true
  },
  "completion": {"state": "pending", "lifecycle": "scheduled"}
}
```

Stable `status` values distinguish:

| Status | Meaning |
|---|---|
| `malformed_request` | MCP JSON is missing, ill-typed, out of range, or names an unsupported transform. No label is submitted. |
| `admission_failure` | Label I/O normalization/validation rejected the program. `error.category` is the stable IR category where available. |
| `parked` | The label is admitted but nonterminal; park reason/retry data is in `completion`. |
| `timeout` | MCP wait elapsed. The owning operation and label remain active. |
| `cancelled` | `cancel_on_timeout` won before execution; category is `CANCELED`. |
| `cancellation` | Execution won the race; category is `CANCELLATION_TOO_LATE`. |
| `execution_failure` | A registered stage, backend, completion protocol, or frontend execution failed. |
| `completion_unknown` | The completion ID is unknown or its retained record expired. |
| `unsupported_feature` | The advertised placeholder is intentionally not implemented, currently Prompt-14 knowledge. |

Timeout never implies cancellation. `cancel_on_timeout` calls the same owning
Python `Operation.cancel()` after a timeout; MCP does not recreate pending state.
A too-late outcome does not claim rollback.

## Tools

### `labios_store`

Writes exact bytes to an external backend through a typed core Write label.

| Field | Type | Required | Meaning |
|---|---|---:|---|
| `destination` | URI string | yes | External backend destination, for example `file:///agent/out.bin` or `sqlite:///agent/item`. |
| `data` | string | yes | UTF-8 text or base64 text. |
| `encoding` | `utf-8` / `base64` | no | Input encoding; default `utf-8`. |
| `intent` | enum | no | Sealed Label I/O intent; default `none`. |
| `priority` | 0..255 | no | Sealed scheduling preference. |
| `ttl_seconds` | integer | no | Checked latest-start TTL; default 0. |
| `timeout_ms` | integer | no | Wait only; default 30000. |
| `cancel_on_timeout` | boolean | no | Attempt public cancellation after timeout; default false. |

```json
{
  "destination": "file:///agent/exact.bin",
  "data": "AEFC/w==",
  "encoding": "base64",
  "intent": "tool_output",
  "priority": 200
}
```

### `labios_retrieve`

Reads exact public bytes through a typed core Read label.

| Field | Type | Required | Meaning |
|---|---|---:|---|
| `source` | URI string | yes | External backend source. |
| `size` | integer | no | Requested byte count; 0 asks a supporting backend for the whole value. |
| `encoding` | `base64` / `utf-8` | no | Output representation; default `base64`. |
| common fields | | no | `intent`, `priority`, `ttl_seconds`, `timeout_ms`, `cancel_on_timeout`. |

Returned `data` is base64 by default, so arbitrary bytes round-trip without
replacement decoding.

### `labios_process`

Executes one structured bytes-to-bytes pipeline from a typed source to a typed
destination on one worker. `source`, `destination`, and a nonempty `pipeline`
are required. Each stage is an object:

```json
{
  "source": "file:///agent/input.bin",
  "destination": "sqlite:///agent/result",
  "pipeline": [
    {"operation": "builtin://identity"},
    {"operation": "builtin://truncate", "args": "16", "input_stage": 0}
  ],
  "intent": "intermediate",
  "priority": 175
}
```

Registered operations are `identity`, `compress_rle`, `decompress_rle`,
`filter_bytes`, `sum_uint64`, `sort_uint64`, `sample`, `truncate`,
`deduplicate`, `median_uint64`, and `format_convert` in the `builtin://`
namespace. Stage arguments and graph indices remain structured fields.
Unregistered names, legacy `grep:TODO` strings, shell commands, globs, and
arbitrary `repo://` operations are rejected before submission. Pipeline stages
remain single-worker; distributed stage placement is not implemented.

### `labios_observe`

Uses public Observe or inspection APIs. Supported queries are:

- `system/health`
- `queue/depth`
- `workers/scores`
- `workers/count`
- `channels/list`
- `workspaces/list`
- `config/current`
- `data/location?file=...`
- `label/status` with `label_id`
- `label/inspect` with `label_id`

`label/inspect` returns IR/operation versions, typed source/destination,
structured pipeline, sealed intent/priority/TTL, and placement history. Worker
score/count and health come from Observe labels rather than direct manager,
Redis, or NATS access. The shared `labios.parse_worker_registry_message` decoder
is used only when a registry snapshot is supplied by a supported inspection
surface; it accepts verified `LWR2` protocol-v2 snapshots only and has no
CSV/text fallback.

### `labios_knowledge`

This name remains discoverable for migration clarity, but returns
`unsupported_feature/PENDING_PROMPT_14`. No direct workspace-key fallback is
retained. Cross-process workspace/knowledge behavior belongs to Prompt 14 after
coordination work; Prompt 11 does not implement it.

## Compatibility breaks from the prototype

Prompt 11 intentionally removes runtime-bypass behavior:

1. `labios_store` no longer accepts `key`, `scope`, `metadata`, or workspace
   TTL semantics. Use `destination`, exact `data`, and Label I/O fields.
2. `labios_retrieve` no longer searches workspace tiers by `key`/`scope` or
   returns workspace versions. Use `source` and optional byte `size`.
3. `labios_process` now requires an explicit `destination`; `pipeline` entries
   are structured registered stages, not line-oriented strings. Globs and the
   Python-side grep/head/tail/count/wc/sort/uniq/filter/sample implementation
   were removed.
4. Process output is written to the declared backend and is retrieved with
   `labios_retrieve`; it is not read from a mounted worker path or returned as
   an in-process line summary.
5. Worker registry CSV, legacy-row acceptance, and text fallback are removed.
6. Observation results now use the stable response envelope rather than ad hoc
   `{"error": ...}` strings.
7. `labios_knowledge` is a stable pending response until Prompt 14; old direct
   DragonflyDB workspace scans are gone.

There is no hidden direct-store compatibility mode. Existing users must migrate
to external backend URIs and structured stages.

## Verification

Hermetic MCP tests (after building the SDK):

```bash
cmake --preset dev
cmake --build build/dev -j"$(nproc)" --target _labios
PYTHONPATH="$PWD/build/dev/python:$PWD/mcp" \
  python3 -m pytest mcp/tests -m "not live" -v
```

Packaged image import and live Compose tests:

```bash
docker compose build mcp
docker compose up -d --wait
docker compose exec -T mcp python -c \
  'import flatbuffers, labios, labios_mcp; print(labios.__file__)'
docker compose exec -T -e LABIOS_MCP_LIVE=1 mcp \
  python -m pytest tests -m live -v
docker compose exec -T mcp python /opt/labios/examples/mcp/golden.py
```

The golden example speaks MCP over stdio, verifies exact binary store/retrieve
and source→identity→destination bytes, inspects the versioned typed label and
placement history, and checks public health/worker/config observations.
