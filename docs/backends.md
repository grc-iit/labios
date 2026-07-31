# LABIOS Backend Guide

Backends are thin last-mile adapters for external user storage. Label
normalization, shuffling, scheduling, pipelines, and coordination happen before
the backend receives the full `LabelData`.

Internal DragonflyDB and NATS are runtime plumbing. In particular, `kv://`
connects to a user's configured Redis-compatible service, never LABIOS's
warehouse.

## BackendStore contract

The callable contract in `<labios/backend/backend.h>` is:

```text
template<typename B>
concept BackendStore = requires(B backend, const labios::LabelData& label,
                                std::span<const std::byte> data) {
    { backend.put(label, data) } -> std::same_as<labios::BackendResult>;
    { backend.get(label) } -> std::same_as<labios::BackendDataResult>;
    { backend.del(label) } -> std::same_as<labios::BackendResult>;
    { backend.query(label) } -> std::same_as<labios::BackendQueryResult>;
    { backend.scheme() } -> std::same_as<std::string_view>;
};
```

Return members are `success`, `error`, and, where applicable, `data` or
`json_data`:

```text
labios::BackendResult status{true, {}};
labios::BackendDataResult value{true, {}, std::vector<std::byte>{}};
labios::BackendQueryResult query{true, {}, "{}"};
```

## Implemented schemes

| Scheme | Adapter | External target |
|---|---|---|
| `file://` | `PosixBackend` | Worker-visible filesystem attachment |
| `sqlite://` | `SQLiteBackend` | Worker-visible SQLite database |
| `kv://` | `KVBackend` | Optional user Redis-compatible service |
| `observe://` | Dispatcher-local handler | Registered runtime observations |

S3, vector, graph, and parallel-filesystem adapters are planned and must not be
presented as callable backends.

## Registering an adapter

`BackendRegistry::register_backend` accepts the backend value and derives its
scheme; it does not accept a separate name or `unique_ptr`:

```text
labios::BackendRegistry registry;
registry.register_backend(labios::PosixBackend("/srv/user-data"));
auto* backend = registry.resolve("file");
```

A new adapter follows the same shape:

```text
class MyBackend {
public:
    labios::BackendResult put(const labios::LabelData& label,
                              std::span<const std::byte> data);
    labios::BackendDataResult get(const labios::LabelData& label);
    labios::BackendResult del(const labios::LabelData& label);
    labios::BackendQueryResult query(const labios::LabelData& label);
    std::string_view scheme() { return "myscheme"; }
};

static_assert(labios::BackendStore<MyBackend>);
```

The adapter must preserve the label's operation, resource scope, version,
isolation, durability, and result semantics. It must return categorized detail
to the worker and must not perform scheduler optimization. Tests that emulate a
user service may add a Compose fixture, but that fixture remains external
backend infrastructure rather than runtime plumbing.
