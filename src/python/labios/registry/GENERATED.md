# Generated worker-registry v2 bindings

These files are generated from `schemas/worker_registry.fbs` with FlatBuffers
`flatc` 24.3.25 using:

```bash
flatc --python --python-typing -o <temporary-output> schemas/worker_registry.fbs
```

Copy only `<temporary-output>/labios/registry/` here; do not replace the parent
`labios/__init__.py`. Protocol version 2 and file identifier `LWR2` are validated
by `labios.registry_v2.parse_worker_registry_message`.
