# 15 — Scientific Label I/O frontend

**Queue status:** Blocked by Prompt 14; third in the RC-to-final queue.

Run after 14. Read `AGENTS.md`, `.planning/LABIOS-2.1.md`, `docs/label-ir.md`, the
public SDK, pipeline API, P04 live test, and evidence map.

## Objective

Demonstrate that a scientific producer/transform/consumer workflow compiles into
the same Label I/O semantics as the SDK path. Use a thin SDK adapter; do not expand
the POSIX interceptor unless the workflow cannot be expressed otherwise.

## Ownership

One bounded scientific adapter/example/workload, canonical label comparison helper,
tests, benchmark fixture, and user documentation. Do not add a general scientific
framework, distributed pipeline stages, new storage backends, or uncontrolled POSIX
coverage.

## Work

1. Choose a Montage-style byte workflow with real source, deterministic registered
   transformations, distinct destination, completion, and verified output.
2. Compile the scientific call and an equivalent direct SDK call into labels.
   Define equivalence over normalized sealed-program fields: IR/operation version,
   typed resources, binding, dependencies, pipeline, intent, priority, isolation,
   durability, and completion behavior. Exclude runtime residual and IDs/timestamps
   that are intentionally unique.
3. Reuse one canonicalization/comparison helper in tests and evidence tooling.
4. Run both paths through live services and verify equivalent terminal semantics and
   destination bytes. Measure only if a fair baseline and boundary are declared;
   otherwise present correctness evidence, not speedup.

## Exit

The adapter is meaningfully thinner than the runtime, equivalent-label evidence is
machine checked, the live result is verified, and docs explain the Label I/O value
instead of presenting another read/write wrapper.
