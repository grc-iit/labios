# LABIOS Native SDK Guide

The C++ and C APIs express one Label I/O lifecycle: submit a sealed I/O program,
retain its owning operation handle, inspect or wait for catalog-backed completion,
and explicitly release caller-owned C results. Clients never contact workers
directly.

Python is outside this native API coherence pass and remains a subset described
by the engineering baseline in `.planning/LABIOS-2.1.md`.

## C++ golden path

The complete, compile-tested source is
[`examples/native/cpp_golden.cpp`](../examples/native/cpp_golden.cpp). It covers
label-level submission, URI conveniences, intent and priority, pipelines,
timeout and completion, cancellation, categorized errors, and observability.

```cpp
#include <labios/client.h>
#include <labios/config.h>

#include <chrono>
#include <cstddef>
#include <iostream>
#include <vector>

int main() {
    try {
        auto client = labios::connect(labios::load_config("conf/labios.toml"));
        std::vector<std::byte> data(4096, std::byte{0x2a});

        labios::LabelParams params{};
        params.type = labios::LabelType::Write;
        params.dest_uri = "file:///examples/label.bin";
        params.intent = labios::Intent::Checkpoint;
        params.priority = 200;
        auto operation = client.publish(client.create_label(params), data);

        auto result = operation.wait_for(std::chrono::milliseconds(1));
        if (result.state == labios::CompletionState::Timeout) {
            // Timeout did not cancel or invalidate the operation.
            result = operation.wait_all(std::chrono::seconds(30));
        }
        if (result.state != labios::CompletionState::Complete) {
            const auto cancellation = operation.cancel();
            std::cerr << "cancel outcomes=" << cancellation.size() << '\n';
        }

        client.write_to("file:///examples/uri.bin", data);
        auto bytes = client.read_from("file:///examples/uri.bin", data.size());

        labios::sds::Pipeline pipeline;
        pipeline.stages.push_back({"builtin://identity", "", -1, -1});
        auto transformed = client.execute_pipeline(
            "file:///examples/uri.bin",
            "sqlite:///examples/pipeline-result",
            pipeline, labios::Intent::Intermediate);
        client.wait(transformed);

        std::cout << client.observe("system/health") << '\n';
        return bytes == data ? 0 : 1;
    } catch (const labios::CompletionError& error) {
        std::cerr << "label " << error.label_id() << ": " << error.what() << '\n';
        return 1;
    }
}
```

### Operation ownership and thread safety

`Operation` owns shared session state plus an immutable set of label IDs. Copies
may be observed, waited, or cancelled concurrently. Destroying or moving the
originating `Client` does not cancel the operation and does not invalidate its
handles. The session closes after its final operation/channel/workspace handle
is released.

`PendingIO` remains a compatibility spelling for `Operation`, but the former
public mutable `pending` vector no longer exists. Use `label_ids()` and
`label_id()`.

A retained label ID can reconstruct a catalog-backed handle after client
restart:

```text
std::vector<uint64_t> ids{saved_label_id};
auto operation = client.operation(ids, labios::OperationKind::Read);
auto current = operation.test();
```

### Completion semantics

- `test(index)` is nonblocking and reports `Pending`, `Parked`, or a terminal
  state for one operation member (`index` defaults to zero).
- `wait_for()` and `wait_all()` return members in input order.
- `wait_any()` selects earliest persisted terminal time, then lowest label ID.
- `Timeout` is typed and nonterminal. Every handle remains reusable.
- `cancel()` returns one `CancellationResult` per label: `Cancelled`, `TooLate`,
  `Terminal`, or `Unknown`.
- Cancellation prevents external I/O only when its catalog compare-and-set wins
  before `Executing`. It does not promise rollback.
- Completed read retrieval is repeatable within result retention; waiting does
  not erase the completion or result bytes.

Each `CompletionResult` also carries the catalog-authoritative `lifecycle`:
`Submitted`, `Admitted`, `Queued`, `Parked`, `Shuffled`, `Scheduled`,
`Executing`, `Completed`, `Failed`, `Cancelled`, or `Unknown`. `state` answers
whether a wait is pending/terminal; `lifecycle` identifies the exact durable
runtime phase. `Submitted` is only a provisional client record and has no
recovery/completion guarantee until dispatcher admission. Some short
transitions, such as `Admitted`, may not be observable between two client polls.

Terminal failures expose the stable Label I/O category separately from detail in
`CompletionResult::category` and `CompletionResult::error`.

## URI and label-level unity

`write`, `read`, `write_to`, `read_from`, `write_with_intent`, and
`execute_pipeline` all construct and submit the same normalized Label I/O IR.
URI strings are convenience ingress syntax; typed `ResourceRef` values are the
canonical representation after normalization.

Implemented external worker attachments are `file://`, `sqlite://`, and optional
user-Redis `kv://`. Internal DragonflyDB is warehouse/catalog plumbing, not a
`kv://` backend.

## Owning channel and workspace handles

```text
auto channel = client.create_channel("progress", 300);
const auto sequence = channel.publish(data);
const auto subscription = channel.subscribe(
    [](const labios::ChannelMessage& message) {
        std::cout << message.sequence << '\n';
    });
channel.unsubscribe(subscription);

auto workspace = client.create_workspace("project", 300);
workspace.put("artifact", data);
auto artifact = workspace.get("artifact");
workspace.grant(other_app_id);
```

These handles own/share the state needed to remain safe after `Client`
destruction. Calls on empty handles throw `ClientError`; a destroyed workspace
throws a stable failure and a destroyed channel rejects further publication.

**Current limitation:** channel registries and sequence allocation, plus
workspace registries, owner identity, and ACLs, remain process-local. Their data
uses DragonflyDB, but this release does not claim cross-process coordination.

## Process-local configuration

`Client::set_config()` changes only the current client process's `Config` object.
It does not reconfigure dispatchers, managers, workers, or other clients.

```text
const bool changed = client.set_config("reply_timeout_ms", "45000");
```

## C golden path

The complete compile-tested source is
[`examples/native/c_golden.c`](../examples/native/c_golden.c).

```c
#include <labios/labios.h>
#include <stddef.h>

int main(void) {
    labios_client_t client = NULL;
    labios_status_t status = NULL;
    const char data[] = "Label I/O";

    labios_error_t code = labios_connect(
        "nats://localhost:4222", "localhost", 6379, &client);
    if (code != LABIOS_OK) return 1;

    code = labios_async_write(client, "/examples/c.bin",
                              data, sizeof(data), 0, &status);
    if (code != LABIOS_OK) return 1;

    /* The status owns the required session lifetime. */
    labios_disconnect_ref(&client);

    labios_completion_list_t results = {0};
    code = labios_wait_for(status, 1, &results);
    if (code == LABIOS_ERR_TIMEOUT) {
        labios_completion_list_release(&results);
        code = labios_wait_for(status, 30000, &results);
    }
    labios_completion_list_release(&results);
    labios_status_release(&status);
    return code == LABIOS_OK ? 0 : 1;
}
```

### C ownership rules

- Client and status handles are opaque and registry-validated.
- Status handles own the operation session and remain usable after client
  release.
- `labios_status_release(&status)` and `labios_disconnect_ref(&client)` clear the
  caller variable and are idempotent.
- Copied stale handles are rejected without dereferencing freed memory.
- A concurrent wait obtains shared ownership before release removes the public
  token, so in-flight calls finish safely.
- `labios_completion_result_t` (including `labios_lifecycle_state_t`),
  `labios_completion_list_t`,
  `labios_cancel_list_t`, `labios_buffer_t`, and `labios_error_info_t` are
  caller-owned after successful return. Release each with its named release
  function. Release functions are idempotent for zeroed/already-released values.
- `labios_wait_read` copies into caller storage and returns the required size in
  `bytes_read`; `LABIOS_ERR_BUFFER_TOO_SMALL` means no truncation occurred.
  `labios_wait_read_alloc` instead returns a LABIOS-allocated buffer released by
  `labios_buffer_release`.
- `labios_last_error` copies the current thread's stable category and message
  into caller-owned storage.
- `labios_test` inspects member zero; `labios_test_label` selects a member by
  index for split operations.
- `labios_wait_all` is the explicit ordered-all wait; `labios_wait_for` is its
  compatibility spelling. `labios_wait_any` applies the same deterministic
  terminal-time/label-ID rule as C++.

C timeout is `LABIOS_ERR_TIMEOUT`, cancellation is `LABIOS_ERR_CANCELLED`, a
lost cancellation race is `LABIOS_ERR_TOO_LATE`, unknown/expired completion is
`LABIOS_ERR_NOT_FOUND`, and stale handles return `LABIOS_ERR_RELEASED`.

## Compatibility decisions

- Source-compatible: existing opaque uses of `PendingIO`, Client wait wrappers,
  synchronous/async C calls, and existing C numeric error values.
- Deprecated: the `PendingIO` name and boolean `Client::cancel(uint64_t)` wrapper.
- Breaking: direct `PendingIO::pending` access; raw `Channel*`/`Workspace*`
  returns; code that depended on destructive one-shot read retrieval.
- Corrected behavior: unknown IDs are lookup errors, `wait_any` is deterministic,
  `wait_all` preserves pending views on timeout, and cancellation exposes the
  race outcome.
