# Label I/O IR Semantic Contract

**Contract version:** Label I/O IR version 1

**Status:** Normative LABIOS 2.1 architecture contract

**Scope:** Label semantics, compatibility normalization, mutation authority,
resource identity, validation, and legal runtime optimization

This document is the semantic contract between Label I/O frontends and the
LABIOS runtime. It specifies what a label means independently of whether every
part is implemented yet. Section 12 lists the current implementation gaps
explicitly.

The governing model is:

> A label is a sealed I/O program plus a mutable runtime residual.

The contract keeps the existing FlatBuffers Label as the wire object. It does
not introduce a textual Label I/O language, a nested program/residual envelope,
or a parallel wire format. Typed additions described here are additive fields
and tables in that same FlatBuffers format.

The key words MUST, MUST NOT, REQUIRED, SHALL, SHALL NOT, SHOULD, SHOULD NOT,
and MAY are normative.

## 1. Scope, terminology, phases, and conformance

### 1.1 I/O boundary

Label I/O describes reads, writes, deletes, flushes, observations, and
compositions of those I/O effects. A transformation stage is part of Label I/O
only when its inputs and outputs are declared I/O values and it has no
undeclared external effects.

The following are outside this contract:

- arbitrary computation with unknown effects;
- shell commands and generic tool calls;
- generic RPC or external-effect brokering;
- backend-specific commands whose I/O resources and effects are undeclared;
- direct user addressing of LABIOS's NATS queue or DragonflyDB warehouse.

DragonflyDB, NATS, catalog records, warehouse keys, and packed supertask data
are runtime plumbing. They are never user ResourceRef families. Channel and
workspace resources are user-visible LABIOS I/O primitives whose
implementations may use that plumbing; their identity is the channel or
workspace identity, not a DragonflyDB key or NATS subject.

### 1.2 Semantic objects

A **sealed program field** states producer intent. An ingress normalizer may
populate or canonicalize it before admission. Once admission succeeds, no
component may change it. A runtime-created aggregate or Composite is a new
program: the creating runtime component acts as its producer, assigns a new
identity, finishes all sealed fields, and then seals it before scheduling.

A **runtime residual field** records how the runtime resolved, analyzed,
combined, or placed the program. Only the component named in the partition
table may write it, and only under the mutation rule in that row. Residual
mutation must not change the declared I/O effect.

An **execution state field** records lifecycle or outcome. Only the lifecycle,
cache, executor, or completion component named in the partition table may
write it. State transitions are monotonic except for an explicit pre-execution
deferral from Scheduled or Shuffled back to Queued.

A **canonical resource identity** identifies the external object or named
transient primitive independently of its access range or selector. An **access
scope** is the byte range, key, row selector, object range, element selector,
sequence interval, or workspace entry touched by one operation.

An **ordering edge** is a directed edge between labels established by a
declared dependency, a conservative edge recorded by the admission hazard
resolver, a derived hazard, or a Flush barrier. A frontend that promises
ordered submission MUST lower that promise into declared dependency edges;
transport arrival order, numeric label ID, and app_id are not program order.

### 1.3 Processing phases

Every externally received label passes through these phases in order:

1. **Structural verification.** Verify the FlatBuffer before dereferencing it.
2. **Decode.** Decode only structurally verified fields.
3. **Version selection.** Read ir_version; absence means version 0.
4. **Compatibility normalization.** Normalize version-0 aliases, addresses,
   versions, flags, and staged-input conventions.
5. **Semantic admission validation.** Validate the normalized version-1
   program and reject ambiguity.
6. **Seal.** Freeze every sealed field.
7. **Resolve and analyze.** Add canonical identity keys, dependencies,
   aggregation provenance, and other authorized residuals.
8. **Schedule and execute.** Add routing, scores, hops, lifecycle, and result.
9. **Complete.** Publish one terminal result and run the sealed completion
   behavior.

A buffer that passes structural verification is not necessarily an admissible
I/O program. Serialization tests may intentionally exercise structurally valid
but non-executable objects.

### 1.4 Validation contexts

Conforming implementations expose or internally distinguish these contexts:

- **Producer admission:** accepts sealed inputs and only initial state.
- **Runtime transition:** checks one designated residual or lifecycle mutation.
- **Recovery snapshot:** checks a previously admitted residual/state snapshot
  without treating runtime-populated fields as producer input.
- **Completion input:** verifies and validates Completion buffers delivered to
  a client or continuation processor.

Using recovery validation to admit an untrusted producer label is forbidden.

## 2. Complete field partition and mutation authority

This section inventories every declared field currently in
schemas/label.fbs exactly once. There are 68 fields: 30 in support tables, 34
in Label, and 4 in Completion. All 34 Label fields map to the 34 current
LabelData members. The current C++ header is include/labios/label.h.

The proposed ir_version, operation_version, typed ResourceRef,
declared_dependencies, and StagedInputBinding fields are normative additions
and therefore are not part of this 68-field inventory. They are classified
separately in Sections 3 through 5 and Section 8.

### 2.1 Support-table fields

| # | FlatBuffers field | Current C++ representation | Partition | Authorized writer and mutation rule |
|---:|---|---|---|---|
| 1 | MemoryPtr.address | MemoryPtr.address | Sealed program | Version-0 ingress only. It is a process-local compatibility address, never a cross-process identity. Immutable after normalization. |
| 2 | MemoryPtr.size | MemoryPtr.size | Sealed program | Version-0 ingress only; normalize to the declared memory extent and freeze. |
| 3 | FilePath.path | FilePath.path | Sealed program | Producer or version-0 ingress before sealing; immutable afterward. |
| 4 | FilePath.offset | FilePath.offset | Sealed program | Producer or version-0 ingress before sealing; immutable afterward. |
| 5 | FilePath.length | FilePath.length | Sealed program | Producer or version-0 ingress before sealing. Legacy zero means unspecified, not an explicit empty range. |
| 6 | NetworkEndpoint.host | NetworkEndpoint.host | Sealed program | Producer or version-0 ingress before sealing; immutable afterward. |
| 7 | NetworkEndpoint.port | NetworkEndpoint.port | Sealed program | Producer or version-0 ingress before sealing; immutable afterward. |
| 8 | Pointer.ptr | Pointer variant | Sealed program | Compatibility input selected before sealing. A runtime consumer must use the canonical ResourceRef, not switch variants later. |
| 9 | LabelDependency.label_id | LabelDependency.label_id | Runtime residual | Admission dependency resolver or hazard analyzer may append a non-self predecessor. Existing edges may not be removed, retargeted, or reordered to weaken constraints. |
| 10 | LabelDependency.hazard_type | LabelDependency.hazard_type | Runtime residual | Written with its dependency edge by the same resolver. It is immutable once the edge is appended. |
| 11 | Continuation.kind | Continuation.kind | Sealed program | Producer or trusted frontend before admission; immutable afterward. |
| 12 | Continuation.target_channel | Continuation.target_channel | Sealed program | Producer before admission; used only when kind is Notify. |
| 13 | Continuation.chain_params | Continuation.chain_params | Sealed program | Producer through a structured continuation API. It is opaque codec-owned data, not a Label I/O textual language. |
| 14 | Continuation.condition | Continuation.condition | Sealed program | Producer through a typed predicate API when such a representation is supported. Arbitrary textual predicates are not valid version-1 programs. |
| 15 | RoutingDecision.worker_id | RoutingDecision.worker_id | Runtime residual | Scheduler sets it on scheduling and may replace it only during pre-execution deferral/rescheduling. Frozen once Executing starts. |
| 16 | RoutingDecision.policy | RoutingDecision.policy | Runtime residual | Scheduler sets it with worker_id. It names the actual placement policy, not producer intent. |
| 17 | HopRecord.component | HopRecord.component | Runtime residual | Each runtime component may append only its own registered component identity. Existing hops are immutable. |
| 18 | HopRecord.timestamp_us | HopRecord.timestamp_us | Runtime residual | Appended with the hop; must be nonzero and no earlier than the preceding hop. |
| 19 | AggregationInfo.original_ids | AggregationInfo.original_ids | Runtime residual | Aggregation planner sets once on a new synthetic label. IDs are unique original programs and do not include the synthetic ID. |
| 20 | AggregationInfo.merged_offset | AggregationInfo.merged_offset | Runtime residual | Aggregation planner sets once to the first byte of the proved contiguous merged scope. |
| 21 | AggregationInfo.merged_length | AggregationInfo.merged_length | Runtime residual | Aggregation planner sets once to the checked sum of original extents. |
| 22 | ScoreSnapshot.availability | ScoreSnapshot.availability | Runtime residual | Scheduler records the finite input value used for its current placement decision. |
| 23 | ScoreSnapshot.capacity | ScoreSnapshot.capacity | Runtime residual | Scheduler records the finite input value used for its current placement decision. |
| 24 | ScoreSnapshot.load | ScoreSnapshot.load | Runtime residual | Scheduler records the finite input value used for its current placement decision. |
| 25 | ScoreSnapshot.speed | ScoreSnapshot.speed | Runtime residual | Scheduler records the finite input value used for its current placement decision. |
| 26 | ScoreSnapshot.energy | ScoreSnapshot.energy | Runtime residual | Scheduler records the finite input value used for its current placement decision. |
| 27 | ScoreSnapshot.tier | ScoreSnapshot.tier | Runtime residual | Scheduler records the finite input value used for its current placement decision. |
| 28 | LabelResult.data_location | LabelResult.data_location | Execution state | Executor/completion coordinator sets once at terminal transition. It may name an external result resource or an opaque internal retrieval handle, but an internal handle is not a ResourceRef. |
| 29 | LabelResult.error | LabelResult.error | Execution state | Executor/completion coordinator sets once. Empty on Complete; category-prefixed and nonempty on Failed. |
| 30 | LabelResult.bytes_transferred | LabelResult.bytes_transferred | Execution state | Executor sets the observed logical bytes transferred at terminal transition. |

### 2.2 Label fields and LabelData members

| # | FlatBuffers field | LabelData member | Partition | Authorized writer and mutation rule |
|---:|---|---|---|---|
| 31 | Label.id | id | Sealed program | Frontend or ingress assigns a nonzero unique ID before sealing. A synthetic label receives a new ID and must not reuse a child ID. |
| 32 | Label.type | type | Sealed program | Producer selects one of the six core operations before admission. |
| 33 | Label.source | source | Sealed program | Version-0 compatibility input. It is frozen after admission and never used as a co-equal internal address. |
| 34 | Label.destination | destination | Sealed program | Version-0 compatibility input. It is frozen after admission and never used as a co-equal internal address. |
| 35 | Label.operation | operation | Sealed program | Producer selects a registered refinement name, or version-0 ingress maps an explicit alias. Immutable after admission. |
| 36 | Label.flags | flags | Execution state | Version-1 producers submit zero. Lifecycle and cache coordinators own defined bits. Version-0 Async is discarded as a frontend submission-mode hint and HighPrio is normalized into sealed priority before the runtime owns this field. |
| 37 | Label.priority | priority | Sealed program | Producer sets the scheduling priority. Version-0 HighPrio may normalize into this field before sealing. |
| 38 | Label.app_id | app_id | Sealed program | Authenticated frontend or ingress sets it; it must match the submitting principal and is immutable. |
| 39 | Label.data_size | data_size | Sealed program | Producer declares logical input or requested output size. Zero means unknown or operation-defined, never permission to invent bytes. |
| 40 | Label.intent | intent | Sealed program | Producer declares intent; schedulers may use but not change it. |
| 41 | Label.ttl_seconds | ttl_seconds | Sealed program | Producer declares the latest-start interval measured from created_us. Zero means no deadline. |
| 42 | Label.isolation | isolation | Sealed program | Producer declares the isolation domain. Runtime placement must preserve it. |
| 43 | Label.reply_to | reply_to | Sealed program | Authenticated transport ingress supplies or verifies it before sealing. Aggregation must preserve per-original completion rather than rewrite an admitted label. |
| 44 | Label.file_key | file_key | Runtime residual | Resource resolver derives it from canonical file identity for compatibility with current grouping. Producer content is only a legacy hint and is not authoritative. Set before shuffling and then frozen. |
| 45 | Label.version | version | Sealed program | Legacy resource/data version constraint. Version-0 ingress maps it to the primary ResourceRef; it is never used as ir_version. |
| 46 | Label.durability | durability | Sealed program | Producer declares Ephemeral or Durable completion requirements. |
| 47 | Label.continuation | continuation | Sealed program | Producer declares validated completion behavior before admission. |
| 48 | Label.source_uri | source_uri | Sealed program | Version-0 compatibility address. Normalize and freeze; consumers use source_resource. |
| 49 | Label.dest_uri | dest_uri | Sealed program | Version-0 compatibility address. Normalize and freeze; consumers use destination_resource. |
| 50 | Label.pipeline_data | pipeline | Sealed program | Structured Pipeline supplied through an API and serialized by the codec. Existing text serialization is private version-0 compatibility, not a public DSL. |
| 51 | Label.dependencies | dependencies | Runtime residual | Admission resolver seeds declared ordering edges and the hazard analyzer may append derived edges. Edges are append-only and the graph must remain acyclic. |
| 52 | Label.children | children | Sealed program | Required only for Composite. Producer, or runtime acting as producer of a new Composite, completes the unique child list before sealing. |
| 53 | Label.routing | routing | Runtime residual | Scheduler writes the actual placement decision under the RoutingDecision rules above. |
| 54 | Label.supertask_id | supertask_id | Runtime residual | Composite planner sets once on a child to the new Composite ID. It must not equal the child ID. |
| 55 | Label.aggregation | aggregation | Runtime residual | Aggregation planner sets once on a new synthetic Write after proving Section 9 legality. |
| 56 | Label.score_snapshot | score_snapshot | Runtime residual | Scheduler writes the inputs used for the current routing decision. |
| 57 | Label.hops | hops | Runtime residual | Runtime components append their own HopRecord; no component edits or removes earlier records. |
| 58 | Label.status | status | Execution state | Lifecycle coordinator performs only the transitions in Section 10. |
| 59 | Label.created_us | created_us | Execution state | Trusted frontend or ingress sets once before admission completes. |
| 60 | Label.queued_us | queued_us | Execution state | Dispatcher sets once on first Queued transition. Requeue does not rewrite the original admission time. |
| 61 | Label.dispatched_us | dispatched_us | Execution state | Scheduler sets for the current dispatch. It may replace it only on a legal pre-execution reschedule. |
| 62 | Label.started_us | started_us | Execution state | Executor sets once immediately before the first external I/O or pipeline stage. |
| 63 | Label.completed_us | completed_us | Execution state | Completion coordinator sets once after all effects covered by the completion boundary have settled. |
| 64 | Label.result | result | Execution state | Executor/completion coordinator sets exactly once with the terminal status. |

### 2.3 Completion fields

| # | FlatBuffers field | Current C++ representation | Partition | Authorized writer and mutation rule |
|---:|---|---|---|---|
| 65 | Completion.label_id | CompletionData.label_id | Execution state | Completion coordinator copies the admitted label ID. Zero is allowed only when a malformed request cannot be safely decoded. |
| 66 | Completion.status | CompletionData.status | Execution state | Completion coordinator sets Complete or Error exactly once. |
| 67 | Completion.error | CompletionData.error | Execution state | Completion coordinator emits an empty string on Complete or CATEGORY: detail on Error. |
| 68 | Completion.data_key | CompletionData.data_key | Execution state | Executor/completion coordinator may provide an opaque internal retrieval handle for returned data. It is empty on failures and is not a user resource address. |

### 2.4 Whole-field mutation rules

Replacing an entire compound field is legal only when every contained field is
owned by the caller and the row permits replacement. In particular:

- a producer may not copy a LabelData object with populated routing or result
  into producer admission;
- a scheduler may not replace a whole label while changing sealed fields;
- a shuffler may not construct a merged Write by copying only a subset of the
  originals' sealed fields;
- recovery may restore valid state but may not use recovery authority to alter
  a sealed program;
- serialization and deserialization never confer mutation authority.

For the legacy flags bitset, version-1 ownership is exact:

- the lifecycle coordinator sets Queued, Scheduled, and Pending to mirror the
  corresponding reached phase and clears Pending at a terminal transition;
- the content/cache manager alone sets or clears Cached and sets Invalidated
  when it proves the cached materialization stale;
- Async and HighPrio are never present after version-0 normalization;
- unknown bits are invalid, and no component may replace the whole bitset in a
  way that changes bits owned by another component.

Producer-declared order and derived hazards have separate version-1
representations. A producer writes the sealed declared_dependencies field
defined in Section 3, not the residual dependencies field. At admission, the
dependency resolver validates each declared predecessor and appends the
corresponding canonical edge to dependencies. A version-0 producer's existing
dependencies vector is an untrusted compatibility candidate: ingress copies
its predecessor IDs into declared_dependencies, verifies or derives the hazard
kind from canonical access records, and then builds the residual edge set.
After admission, every copied producer edge is immutable and the hazard
analyzer may only append new edges. No component may remove, retarget, or
change the kind of an admitted edge.

ttl_seconds is a pre-execution deadline, not a rollback promise. For a nonzero
value, admission computes with checked integer arithmetic:

    expires_at_us = created_us + ttl_seconds * 1,000,000

Overflow is ILLEGAL_COMBINATION. A label that has not entered Executing when
the runtime clock reaches or exceeds expires_at_us becomes Failed with EXPIRED
and performs no external I/O. Once Executing begins before the deadline, TTL
no longer cancels or changes the eventual Complete/Failed result; passing the
deadline during execution never implies rollback.

Priority is a scheduling hint from 0 through 255, with larger values preferred
among otherwise ready and feasible labels. It creates no ordering edge,
deadline, starvation guarantee, or permission to violate dependencies,
isolation, or durability. Optimizers preserve each child's priority even when
they group labels.

## 3. IR versioning and version-0 migration

### 3.1 Normative version fields

Version 1 adds these sealed top-level semantic fields to the existing Label:

- ir_version: unsigned 32-bit semantic IR version;
- operation_version: unsigned 32-bit refinement descriptor version;
- source_resource: optional typed ResourceRef;
- destination_resource: optional typed ResourceRef;
- declared_dependencies: producer-declared predecessor label IDs;
- input_binding: optional StagedInputBinding.

Their absence in a FlatBuffer yields zero or null through normal FlatBuffers
defaults. They are additive parts of the same Label wire format and have this
partition:

| Normative addition | Partition | Authorized writer and rule |
|---|---|---|
| Label.ir_version | Sealed program | Producer or version-0 normalizer sets before admission. |
| Label.operation_version | Sealed program | Producer or alias normalizer sets before admission. |
| Label.source_resource | Sealed program | Producer or address normalizer sets the sole canonical source before admission. |
| Label.destination_resource | Sealed program | Producer or address normalizer sets the sole canonical destination before admission. |
| Label.declared_dependencies | Sealed program | Producer declares predecessor IDs; immutable after admission. |
| Label.input_binding | Sealed program | Trusted staging frontend or version-0 normalizer binds one stable logical content object before admission. |
| StagedInputBinding.provenance | Sealed program | DirectProducer or MaterializedSource, fixed before admission. |
| StagedInputBinding.content_id | Sealed program | Content manager assigns a stable opaque ID; it is not a warehouse key or physical location. |
| StagedInputBinding.logical_length | Sealed program | Staging frontend records the exact staged byte count. |
| StagedInputBinding.digest_algorithm | Sealed program | Empty when no digest is supplied; otherwise names a registered digest algorithm. |
| StagedInputBinding.digest | Sealed program | Optional digest over the exact staged bytes. |
| StagedInputBinding.observed_version | Sealed program | Empty for DirectProducer; for MaterializedSource it records the source version actually read. |

All fields added to the existing Label table for version 1 MUST be appended
after the current final Label.result field. Existing fields MUST NOT be
reordered or deleted, and no new field may be inserted before or between them,
because that would change existing FlatBuffers vtable slot numbers. New
ResourceRef and
StagedInputBinding tables may define their own fields, but their top-level
Label references still occupy only appended slots. This append-only rule is
what makes old stored buffers decode with zero/null defaults.

Version 1 also appends two HazardType enum values without renumbering RAW, WAW,
or WAR:

- Order records a declared or conservative ordering edge with no more specific
  data hazard;
- Barrier records an edge to or from Flush.

Version-0 edges retain their existing numeric meanings. An admission resolver
uses RAW, WAW, or WAR whenever canonical access records establish that hazard,
Order for a pure predecessor constraint, and Barrier for Flush.

StagedInputBinding is serialized with the label and therefore survives queue
delivery and recovery. Its content_id resolves through an authenticated,
durable-for-the-label-lifetime catalog mapping owned by ContentManager. That
mapping from stable content_id to a physical warehouse/cache location is
runtime residual state outside the sealed label and may move without changing
the binding. Recovery MUST restore the mapping or fail the label as
STAGED_CONTENT_UNAVAILABLE; it may not bind different bytes. Durable labels
retain the mapping and content until their completion/recovery guarantee no
longer needs it.

The initial normative ir_version is 1. A newly admitted or newly synthesized
version-1 label MUST carry ir_version 1 internally. Version 0 is accepted only
as compatibility input and MUST NOT survive normalization as the canonical
in-memory version.

The existing Label.version is a resource/data version input. It is distinct
from ir_version and operation_version.

### 3.2 Version bump rule

The IR version MUST increase when a previously conforming consumer could
accept a label and perform observably different I/O because of a specification
change. This includes changes to:

- core operation meaning or required resource roles;
- defaults that affect I/O;
- resource identity, aliasing, scope, or version semantics;
- sealed/residual/state ownership;
- pipeline input, output, or completion semantics;
- ordering, hazard, aggregation, deferral, or cancellation rules;
- validation rules when the newly accepted form has new execution semantics.

The IR version does not change for:

- an implementation optimization that obeys this contract;
- a new residual diagnostic ignored by old consumers;
- a bug fix that restores already specified behavior;
- a newly installed extension descriptor that old consumers reject as unknown.

An incompatible change to one extension descriptor uses a new
operation_version. It does not require a global IR bump unless it also changes
the core contract.

### 3.3 Unknown versions

A consumer supports an explicit finite set of IR versions. It MUST reject any
unknown nonzero version as UNSUPPORTED_IR_VERSION before normalization,
optimization, or backend access. It MUST NOT guess forward compatibility from
FlatBuffers field presence.

### 3.4 Version-0 normalization

The normalizer performs these steps deterministically:

1. Preserve the decoded core LabelType. An absent scalar type decodes to the
   schema default Read; the normalizer does not guess that it meant Write.
2. Resolve operation aliases using Section 4.4. Unknown strings are not
   heuristically mapped.
3. Treat legacy dependencies as untrusted declared-dependency candidates,
   validate predecessor IDs, and clear producer control of the canonical
   residual vector.
4. Normalize each typed, URI, and Pointer address independently using Section
   6.
5. Compare all representations for each role. Equivalent forms coalesce.
6. Apply the version-0 staging rules in Section 8, create and serialize the
   canonical input_binding, and verify its content mapping before deciding whether
   an input is ambiguous.
7. Map nonzero Label.version to the primary resource version constraint:
   source for Read and Observe; destination for Write, Delete, and Flush.
8. Clear legacy Async as a frontend submission-mode hint; all admitted labels
   still have queryable completion under reply_to/continuation semantics. Map
   legacy HighPrio to priority 255 when priority is zero or 255; a different
   explicit priority plus HighPrio is ILLEGAL_COMBINATION. Then give
   Label.flags to the state machine. Unknown flag bits are rejected.
9. Accept known initial legacy state such as Created, created_us, and the
   legacy Queued flag only in version-0 producer admission; recreate canonical
   version-1 lifecycle state rather than trusting contradictions.
10. Produce one version-1 canonical program, then run version-1 admission
    validation.

ADDRESS_CONFLICT and AMBIGUOUS_INPUT are version-1 admission results after
these attempts. Equivalent legacy forms and the recognized version-0 staging
patterns therefore continue to normalize successfully.

## 4. Core operations and namespaced refinements

### 4.1 Core operations

The six LabelType values are the complete semantic core:

| Core operation | Required program resources | Declared effect | Result |
|---|---|---|---|
| Read | Exactly one source; zero or one destination | Read source scope. If destination exists, write the returned value there. | Returned value through destination or completion retrieval handle. |
| Write | Exactly one destination and exactly one semantic input binding | Read the input, run an optional pipeline, write destination scope. | Destination identity and bytes transferred. |
| Delete | Exactly one destination target | Remove the destination scope subject to its version constraint. File and object core Delete require whole-resource scope. | Status and affected scope. |
| Flush | Exactly one destination target | Establish the durability/visibility barrier declared for prior ordered writes to that target. | Status after the barrier is satisfied. |
| Observe | Exactly one source target or registered runtime-observation resource | Read metadata or runtime observation without changing the observed resource. | Structured observation value through completion retrieval. |
| Composite | One or more unique children; no direct source or destination | Execute the child I/O programs subject to their ordering graph. The Composite has no undeclared backend effect. | Aggregate terminal status and child results. |

Core Read and Write express data movement, not generic computation. Delete does
not imply deletion of LABIOS warehouse plumbing. Flush is a barrier and not a
request to flush every backend globally. Observe does not grant direct access
to catalog, queue, or warehouse keys.

Durability has these minimum version-1 meanings:

- Ephemeral mutation completion requires the backend's normal successful
  visibility acknowledgement but no additional stable-media barrier.
- Durable mutation completion requires an acknowledgement that the effect
  survives restart/failure within the backend's registered durability domain.
  The adapter must issue and await the backend's commit, sync, or equivalent
  barrier before reporting Complete.
- For Read or Observe without an external destination, Ephemeral permits the
  completion value to live only for the deployment's documented retrieval
  lifetime, while Durable requires the result binding and bytes to survive the
  documented recovery window.
- Composite durability is the strongest child requirement and completion waits
  for every child's requirement.
- Core Flush requires Durability.Durable and makes all prior ordered aliasing
  writes satisfy the target backend's registered durability domain before it
  completes.

Admission rejects a durability value the addressed backend cannot satisfy as
BACKEND_UNSUPPORTED. A runtime must not silently weaken Durable to Ephemeral.

### 4.2 Refinement identifier

A refinement descriptor is the tuple:

    (qualified_name, operation_version)

qualified_name has exactly two dot-separated segments:

    namespace.verb

Each segment matches the ASCII pattern [a-z][a-z0-9_]{0,62}. The reserved
namespace core identifies the six built-in descriptors. operation_version is
a positive unsigned 32-bit integer.

This is a qualified identifier, not a command string or textual DSL.
Descriptor parameters, when eventually needed, are typed by the registered
descriptor and ResourceRef shapes; arbitrary shell, SQL, JSON-command, or tool
payloads are not implicitly executable.

### 4.3 Descriptor declaration

Before a descriptor may be admitted, a trusted registry declaration MUST
provide:

- qualified name and operation version;
- exactly one compatible core LabelType;
- input and output roles and cardinalities;
- permitted ResourceRef families for every role;
- access mode and access-scope derivation for every resource;
- resource identity and alias/overlap rules not already defined by its family;
- result value type;
- ordering and atomicity behavior;
- whether identical instances are idempotent and retryable;
- the last safe cancellation point;
- the backend or worker capability required;
- a typed parameter schema, if the core fields and resources are insufficient.

A descriptor may narrow core semantics but may not contradict them or declare
undeclared effects. An unregistered name or version is rejected as
UNKNOWN_REFINEMENT.

### 4.4 Minimal version-1 descriptor registry and aliases

The initial normative registry is intentionally minimal:

| Canonical descriptor | Version | Compatible type | Additional semantics |
|---|---:|---|---|
| core.read | 1 | Read | No refinement beyond core Read. |
| core.write | 1 | Write | No refinement beyond core Write. |
| core.delete | 1 | Delete | No refinement beyond core Delete. |
| core.flush | 1 | Flush | No refinement beyond core Flush. |
| core.observe | 1 | Observe | No refinement beyond core Observe. |
| core.composite | 1 | Composite | No refinement beyond core Composite. |

The complete version-0 alias table is:

| Version-0 operation field | Required LabelType | Canonical descriptor |
|---|---|---|
| empty | Any core type | Matching core descriptor above |
| read | Read | core.read version 1 |
| write | Write | core.write version 1 |

These are the legacy operation strings the current production runtime itself
creates. Strings accepted merely because the current LabelParams API stores an
arbitrary value are not automatically aliases. In particular, write_block,
watch_dir, read_block, scan, checkpoint_write, and pipeline_filter remain
structurally decodable test or benchmark values but are not registered
version-1 descriptors.

### 4.5 Non-normative extension illustration

The name vector.search is often useful for explaining the declaration model:
such a future descriptor would need to declare its vector collection, query
input, result output, read scope, result type, and backend capability.
It is not registered in version 1 and a version-1 consumer MUST currently
reject it as UNKNOWN_REFINEMENT. This illustration creates no implementation
commitment.

## 5. Typed ResourceRef model

### 5.1 Common shape

The normative semantic shape is:

    ResourceRef
      backend_id
      logical_id
      version_constraint
      value: ResourceValue union

    VersionConstraint
      relation: Any | Exact | MustNotExist
      token

    ResourceValue union
      FileRangeResource
      MemoryResource
      NetworkResource
      KeyValueResource
      RelationalResource
      ObjectResource
      VectorResource
      GraphResource
      ChannelResource
      WorkspaceResource
      ExtensionResource

backend_id identifies the user's external backend instance or the public
LABIOS namespace for channel/workspace primitives. It MUST NOT expose a
DragonflyDB or NATS endpoint. logical_id is an optional frontend-visible alias;
it must resolve to the same canonical identity as the typed locator before
optimization.

VersionConstraint is sealed. Any means no version precondition. Exact requires
the backend-observed token to match. MustNotExist is a conditional-create
precondition. Tokens are opaque strings whose canonical encoding is defined by
the resource family or backend. A successful operation may report an observed
or new token as execution state; it does not mutate the sealed constraint.

The existing numeric Label.version normalizes as Any when zero and as an Exact
decimal sequence token when nonzero.

There is no generic property map. A family or registered ExtensionResource
schema owns every address field so validation and overlap remain decidable.

### 5.2 Family identity, address, version, and operation properties

| Family and locator shape | Canonical identity | Access scope and overlap | Version property | Core operations permitted by family |
|---|---|---|---|---|
| FileRangeResource: normalized path, offset, length, extent mode | backend_id plus filesystem namespace plus normalized path | Byte interval. Extent is Exact, ToEnd, or Unspecified. Unknown extent overlaps every scope on the same identity. | Backend file generation, stat tuple, or registered object token; legacy numeric sequence is allowed. | Read, Write, Delete, Flush, Observe |
| MemoryResource: owner, allocation ID, offset, length, transfer token or local address | owner plus allocation ID | Byte interval within one allocation. Unknown length overlaps the allocation. | Allocation generation. A local address is never a version. | Read, Write, Observe |
| NetworkResource: transport, normalized host, port, stream/path | backend_id plus transport endpoint plus stream/path | Declared byte stream or message interval. Unknown stream scope aliases the whole endpoint. It cannot represent arbitrary RPC effects. | Protocol sequence or entity token when supported; otherwise Any only. | Read, Write, Flush, Observe |
| KeyValueResource: database, namespace, binary key | backend_id plus database, namespace, and exact key | Exact key. A registered key range is allowed only through a typed extension schema; unknown range overlap is conservative. | CAS revision or value version. | Read, Write, Delete, Flush, Observe |
| RelationalResource: database, schema, relation, typed selector | backend_id plus database, schema, and relation | Whole relation, exact primary key, or typed key range. Selectors that cannot prove disjointness overlap. Arbitrary SQL text is not a selector. | Row/relation MVCC token or transaction-visible version. | Read, Write, Delete, Flush, Observe |
| ObjectResource: bucket, key, offset, length, extent mode | backend_id plus bucket and object key | Whole object or byte interval. Multipart parts alias their covered bytes. | ETag, generation, or version ID. | Read, Write, Delete, Flush, Observe |
| VectorResource: database, collection, optional item ID | backend_id plus database and collection; item ID narrows identity scope | Whole collection or exact item. Similarity search is not implied by core Read and requires a registered refinement. Unknown selectors overlap the collection. | Item or collection revision. | Read, Write, Delete, Flush, Observe |
| GraphResource: database, graph, element kind, optional element ID | backend_id plus database and graph; element identity narrows scope | Whole graph, exact node, or exact edge. Traversal/query selectors require a registered refinement. Unknown selectors overlap the graph. | Element or graph revision. | Read, Write, Delete, Flush, Observe |
| ChannelResource: public namespace, channel name, sequence start/count | LABIOS namespace plus channel name | Exact message or sequence interval. Count zero means an open/tail scope and overlaps later messages. | Channel epoch plus sequence constraint. | Read, Write, Flush, Observe |
| WorkspaceResource: public namespace, workspace name, optional entry key | LABIOS namespace plus workspace name; key narrows scope | Whole workspace or exact entry. Empty key is valid only for whole-workspace Flush or Observe. | Workspace/entry revision used for optimistic coordination. | Read, Write, Delete, Flush, Observe |
| ExtensionResource: namespace, kind, schema version, verified identity bytes, verified address bytes | Registry-defined and deterministically derived from verified identity bytes | Registry-defined access scope and conservative overlap function | Registry-defined canonical token rules | Only operations declared by the registered resource schema and operation descriptor |

### 5.3 Required locator details

File and object extent modes eliminate the legacy ambiguity around length zero:

- Exact requires a positive length, except that a validated zero-byte
  operation may use exact zero without aggregation.
- ToEnd begins at offset and extends to the resource's observed end.
- Unspecified lets admission infer an exact Write extent from a bound input or
  an exact Read extent from data_size. If it cannot infer one, hazard analysis
  treats the scope as the entire identity.

For core Delete, FileRangeResource and ObjectResource MUST select the whole
file or object: offset zero with a whole-resource/ToEnd extent and no
range-limiting length. Version 1 does not assign a meaning to byte-range
deletion. Hole punching, truncation, multipart deletion, and byte shifting
require a future registered refinement whose capability and exact hazards are
declared; none is registered in the minimal version-1 registry.

MemoryResource must contain exactly one usable addressing mechanism:

- a transfer token valid in the executor's address space; or
- a raw local address accepted only when trusted ingress proves producer and
  executor are the same process.

Owner and allocation identity are still required after a raw address is
accepted. A serialized integer address alone is rejected for remote execution
as UNSAFE_MEMORY_REFERENCE.

Relational selectors are limited to WholeRelation, PrimaryKey, KeyRange, or a
registered typed view. Vector and graph core operations similarly address
whole collections/graphs or exact items/elements. Query languages, similarity
search, and traversals require registered refinements and are not inferred
from arbitrary URI query text.

ExtensionResource namespace and kind use the same segment grammar as
refinement identifiers; schema version is positive. Its identity/address bytes
are accepted only after a registered schema verifier decodes them. An unknown
extension is UNKNOWN_RESOURCE, not a generic escape hatch.

The current Observe routes may normalize to the reserved registered resource
kind org_labios.runtime_observation version 1. That resource exposes named
runtime observations, not DragonflyDB keys, NATS subjects, or unrestricted
configuration access. P03 clarification: until that typed observation resource
is carried end to end, an Observe label's `observe://` query is an exempt sealed
runtime-query target and is not passed to user-resource URI normalization.

## 6. URI and Pointer compatibility normalization

### 6.1 URI rules

URI strings are ergonomic ingress/interchange syntax. They are not the
canonical internal representation. Normalization:

1. validates UTF-8 and percent encoding;
2. lowercases the scheme and DNS host where applicable;
3. removes dot path segments without resolving symlinks;
4. applies the family-specific authority, default-port, and path rules;
5. sorts recognized query parameters for comparison;
6. rejects duplicate singleton parameters and unknown semantic parameters;
7. produces one typed ResourceRef and version constraint.

Every deployment exposes one ingress configuration value for
default_file_backend_id and one for default_file_namespace. Legacy FilePath,
bare paths, and authority-less file URIs all use those exact values. A file
URI with an authority must map that authority through the registered file
backend table. The chosen backend ID and namespace are embedded in the
canonical FileRangeResource before sealing, so dispatcher and worker
configuration cannot reinterpret the same path differently.

Bare paths normalize as file resources. Recognized scheme families are:

| URI scheme class | Resource family |
|---|---|
| file or bare path | FileRangeResource |
| memory or registered shared-memory scheme | MemoryResource |
| registered byte-stream network schemes | NetworkResource |
| kv or registered external Redis-compatible scheme | KeyValueResource |
| sqlite, postgres, or another registered relational scheme | RelationalResource |
| object, s3, or another registered object-store scheme | ObjectResource |
| vector or a registered vector-store scheme | VectorResource |
| graph or a registered graph-store scheme | GraphResource |
| channel | ChannelResource |
| workspace | WorkspaceResource |
| observe | Registered runtime-observation ExtensionResource |
| other | Registered ExtensionResource mapping, otherwise UNKNOWN_RESOURCE |

A kv or Redis-compatible URI names the user's backend. A URI that identifies
LABIOS's configured warehouse endpoint is rejected as
FORBIDDEN_INTERNAL_RESOURCE. Likewise, labios://warehouse/... is not
normalized into a storage resource merely because an old fixture contains it.

URI credentials and secret tokens MUST NOT be embedded in ResourceRef.
Authentication uses deployment-managed credential references outside resource
identity.

### 6.2 Legacy Pointer rules

FilePath normalizes to FileRangeResource:

- path is normalized as a file URI path;
- backend_id and filesystem namespace come from the deployment's
  default_file_backend_id and default_file_namespace;
- offset is preserved;
- positive length becomes Exact;
- zero length becomes Unspecified and is inferred from data_size or a staged
  Write input when possible.

MemoryPtr normalizes to MemoryResource only in a trusted admission context:

- size supplies the access extent;
- a staged blob for the same label may establish the version-0 producer-memory
  binding in Section 8;
- a raw nonzero address is usable only in the same process;
- for a version-0 Write with staged bytes and a non-null MemoryPtr source,
  ingress synthesizes owner from the authenticated submission session and
  allocation ID from the label ID, retains one MemoryResource semantic source,
  and records the staged blob as its MaterializedSource transfer binding;
- for a version-0 Read only, a null MemoryPtr destination whose size agrees
  with data_size is a legacy request to return bytes through Completion. It
  normalizes to no destination_resource rather than an unsafe remote memory
  address;
- a null source address never supplies readable bytes without a staged
  binding.

NetworkEndpoint normalizes to a NetworkResource with the registered legacy
byte-stream transport. Host must be nonempty and port nonzero. It does not
authorize arbitrary protocol commands.

A null Pointer contributes no resource candidate.

### 6.3 Equivalence

Each available representation for one role is normalized independently.
Candidates are equivalent only if all of these agree:

- resource family;
- backend identity;
- canonical object identity;
- access scope after legacy inference;
- version constraint;
- family-specific semantic selector.

For file compatibility, a Pointer path with zero length and a URI with no
range are equivalent Unspecified scopes. Query-parameter ordering and URI
spelling differences do not create conflicts after canonicalization.

If candidates are equivalent, the normalizer coalesces them. If they are not,
admission rejects ADDRESS_CONFLICT. file_key never participates as an
authoritative address and cannot make conflicting resources equivalent.

## 7. Canonical dual-addressing resolution and migration

### 7.1 Canonical representation

After admission:

- source_resource is the only source address consulted;
- destination_resource is the only destination address consulted;
- canonical identity and scope derived from those fields drive shuffling,
  hazards, locality, scheduling, catalog updates, worker execution, and
  backend selection;
- source, destination, source_uri, dest_uri, and file_key are compatibility
  projections or residuals, never alternate sources of truth.

A component that routes from destination but executes dest_uri violates this
contract even if both happen to agree in a test.

### 7.2 Rolling migration

The bounded migration sequence is:

1. Add structural verification, ir_version, operation_version, typed
   ResourceRef, declared_dependencies, and StagedInputBinding decoding to
   ingress.
2. Normalize all version-0 Pointer and URI forms and reject only genuine
   conflicts after Section 6 equivalence and Section 8 binding.
3. Require every dispatcher, worker, and persisted-buffer reader to advertise
   its maximum ir_version plus typed-resource and descriptor capabilities to
   the component that selects or feeds it. A version-1-aware ingress and
   routing gate is a prerequisite for version-1 producer traffic; a legacy
   consumer cannot enforce this gate merely by decoding an unknown appended
   FlatBuffers field.
4. Have new producers emit typed resources. When the selected downstream
   consumer is legacy, the producer, ingress, or dispatcher MUST also emit
   equivalent legacy Pointer/URI projections for every required role. Such
   projection is allowed only when the canonical resource family, scope,
   version constraint, and registered operation semantics are losslessly
   representable to that consumer.
5. Do not route a typed-only version-1 label to a version-0 consumer. If the
   selected legacy path cannot represent or enforce the canonical program,
   keep the label deferred while a capable path can appear, or terminate it
   with BACKEND_UNSUPPORTED or UNSATISFIABLE_REQUIREMENTS under Section 9.6.
   Never erase or weaken semantics to make it look like a version-0 program.
6. Move shuffler, scheduler, catalog, worker, and backend adapters to typed
   resources in that order or behind one shared resolver.
7. Validate on every internal receive boundary that any projections still
   equal the canonical resource.
8. Stop consulting legacy fields only after the capability gate proves that
   every selected consumer supports typed resources.
9. Remove legacy emission only in a separately reviewed compatibility change;
   removal from stored wire data is not part of P01.

No migration step may silently prefer one conflicting form. Old stored buffers
remain decodable as version 0 and are normalized when read. The projections
remain fields in the same flat Label table; this migration does not introduce a
second wire format or an envelope.

## 8. Pipeline, staging, and Composite completion semantics

### 8.1 Semantic input binding

A Write has exactly one semantic input:

- a DirectProducer input_binding with no source_resource; or
- a declared source_resource, optionally accompanied by one
  MaterializedSource input_binding for that same source.

A staged-input binding is the serialized StagedInputBinding field defined in
Section 3. It is not a ResourceRef. It contains:

- provenance: DirectProducer or MaterializedSource;
- stable content_id;
- exact logical length and optional digest;
- for MaterializedSource, the source version actually observed.

For MaterializedSource, the source identity and scope are the label's sealed
source_resource; the observed version must satisfy its VersionConstraint.
Users never address content_id or its catalog mapping as a backend. The
ContentManager may move the physical bytes while preserving content_id,
length, digest, and observed version.

The declared source is authoritative. Staged bytes may substitute for a
backend read only when a MaterializedSource binding proves that they represent
that exact source identity, scope, and required version.

Version-0 normalization recognizes:

- staged bytes with no source as DirectProducer input;
- MemoryPtr source plus staged bytes as a materialization of producer memory;
- an external source plus staged bytes only when trusted provenance binds them
  to that source.

After those attempts, an external source plus an unbound staged blob is
AMBIGUOUS_INPUT. The blob never silently overrides the source, and ignoring it
silently is also forbidden.

### 8.2 Pipeline meaning and stage typing

A pipeline-bearing version-1 label is a core Write. Its semantic program is:

    input binding -> ordered transformation stages -> destination_resource

Version 1 stage values are Bytes. Every stage consumes exactly one Bytes value
and produces exactly one Bytes value. A stage implementation must be
registered in the program repository with:

- deterministic Bytes-to-Bytes I/O signature for the same input and arguments;
- no undeclared external I/O or generic tool effects;
- bounded argument schema;
- failure and cancellation boundary.

The Pipeline object is supplied through a structured API. pipeline_data is a
codec-owned serialization of that object. The current tab/newline encoding is
a version-0 implementation detail, not a public syntax or DSL.

### 8.3 Stage graph rules

For a pipeline with N stages:

- N is positive.
- Each stage operation and argument object validates against the program
  repository declaration.
- input_stage is -1 for the label input or an index in [0, i) for stage i.
- output_stage is -1 for the unique destination-producing sink or the index of
  the unique later stage that consumes this output.
- Every non-sink output_stage target must name the inverse input_stage edge.
- Every stage is reachable from the label input and can reach the unique sink.
- Cycles, self/future inputs, dangling stages, multiple sinks, and conflicting
  input/output edges are INVALID_PIPELINE.

These fields currently describe a single-consumer acyclic graph. A
version-0 pipeline whose input_stage edges form one unambiguous chain may have
its legacy output_stage hints canonicalized to that chain. The normalizer does
not guess among multiple possible graphs.

### 8.4 Pipeline completion boundary

A pipeline Write becomes Complete only after:

1. the semantic input has been loaded or its bound materialization verified;
2. every stage has succeeded;
3. the unique sink value has been accepted by the destination backend;
4. the destination satisfies the declared durability/visibility requirement;
5. bytes transferred and result location have been recorded;
6. no stage or backend effect covered by this label can occur afterward.

Warehouse cleanup may be retried after completion, but cleanup failure must not
change external I/O semantics. If cleanup is required to preserve isolation or
confidentiality, completion waits for it.

A stage or backend failure produces Failed and no later stage runs. Backend
atomicity and retry behavior come from the descriptor/backend declaration;
the runtime may not claim rollback it cannot provide.

### 8.5 Composite completion boundary

Composite children are sealed programs with unique IDs and an acyclic ordering
graph. The Composite itself has a new unique ID.

A Composite is Complete if and only if every child is Complete. It is Failed
if any child fails. Under fail-fast execution, no unstarted child begins after
the first failure. Before publishing the Composite terminal completion, the
executor terminalizes every unstarted child: a child transitively dependent on
the failed child becomes Failed with DEPENDENCY_FAILED; any other child
suppressed solely by fail-fast becomes Failed with COMPOSITE_ABORTED. For each
such child the executor records completed_us and result, clears Pending,
persists the terminal catalog state, publishes the child's Completion, and
applies that child's sealed completion behavior. A terminalized child MUST
never be scheduled or executed later.

The Composite terminal completion is not published until those skipped-child
completions have been published and every already-started child has reached a
terminal state. Thus every child has an attributable terminal outcome and no
child effect covered by the Composite occurs after clients observe the
Composite terminal result.

Child completions remain individually attributable to child IDs. A Composite
does not reuse the first child's ID, replace child replies with its own reply,
or make children disappear from lifecycle accounting.

Continuation.kind None and Notify are valid when their other fields obey
Section 10. Chain is valid only when chain_params decodes to a structurally
verified, normalizable label template that will receive a fresh ID at
activation. Version-1 Conditional is not admitted until a typed predicate
representation replaces the current textual expression evaluator.

## 9. Decidable legality rules for current runtime decisions

This section is intentionally bounded to aggregation, dependency/hazard
analysis, reordering, placement deferral, and cancellation decisions faced by
the current shuffler and scheduler. It does not specify additional optimization
passes.

### 9.1 Access records

Admission derives a finite set of access records for every label:

    (canonical identity, access scope, mode)

Modes are:

- R: Read or read side of a Write/pipeline;
- W: Write;
- D: Delete, treated as a destructive write;
- F: Flush barrier;
- O: Observe.

Composite access records are the union of child records. Pipeline stages add no
external records because version-1 stages have no undeclared external I/O.

Two records alias when their identities match and their scopes overlap.
Family-specific overlap is defined in Section 5. If disjointness cannot be
proved, they overlap.

### 9.2 Ordering and hazards

Ordering edges come only from:

- producer-declared dependencies normalized at admission;
- conservative admission edges already recorded in dependencies;
- derived hazards;
- Flush barriers;
- Composite child dependencies.

A frontend that exposes ordered submission MUST emit
declared_dependencies connecting that stream; transport order alone is not a
sealed promise. Label IDs, timestamps from different clocks, app_id alone,
priority, and an unrecorded receive order do not create program order.

For otherwise unordered aliasing accesses with at least one W, D, or F, the
admission hazard resolver MAY choose a conservative execution order. It does
so by serializing admission for that canonical identity, choosing the already
admitted nonterminal access before the new access, and persisting the edge in
dependencies before the new label can be shuffled. From then on, the recorded
edge, not receive order, is authoritative.

For two ordered, aliasing accesses A before B:

| Earlier access | Later access | Required edge |
|---|---|---|
| W or D | R or O | RAW: A -> B |
| W or D | W or D | WAW: A -> B |
| R or O | W or D | WAR: A -> B |
| R or O | R or O | None, unless the descriptor declares observation order |

An F access is a barrier: every earlier ordered aliasing W or D precedes it,
and it precedes every later ordered aliasing R, W, D, F, or O. core.observe
preserves declared order because runtime observations may be time-sensitive.

The analyzer appends missing edges and rejects a cycle as INVALID_DEPENDENCY.
It must analyze canonical resources, not only FilePath, file_key, one batch, or
nonzero byte lengths. The catalog must retain nonterminal access records and
edges across batch boundaries. If that state is temporarily unavailable, the
new potentially aliasing label is deferred until the state is reconstructed;
it is not optimized or dispatched on the assumption that a batch boundary
means independence.

### 9.3 Reordering

A new execution order is legal exactly when:

1. it is a topological ordering of all declared, hazard, Flush, and Composite
   edges;
2. every pair whose relative order changes is either non-aliasing, both
   read-only, or declared commutative by the same registered descriptor;
3. it preserves per-label completion attribution and any declared ordered
   completion stream;
4. it does not move an operation across its TTL expiration or continuation
   prerequisite.

If any identity, scope, access mode, descriptor property, or ordering edge is
unknown, the pair is not reordered. Sorting labels by numeric ID is not a
legality proof.

### 9.4 Write aggregation

The current shuffler may aggregate only core Write labels to FileRangeResource.
All of the following are REQUIRED:

1. Every label is independently valid and has a DirectProducer staged Bytes
   input.
2. No label has a pipeline or external source_resource.
3. All destinations have the same canonical file identity and Exact,
   positive, pairwise non-overlapping scopes.
4. Scopes are adjacent in the same order as the labels' required program
   order.
5. No intervening ordered label reads, observes, writes, deletes, or flushes
   the same identity in a way that aggregation would cross.
6. Core descriptor/version, app_id, intent, isolation,
   durability, resource version constraint, and all backend-visible
   properties are identical.
7. TTL and priority differences cannot cause an original to miss a guarantee.
   In version 1 the bounded rule is that TTL and priority must be identical.
8. Completion, reply, and continuation behavior can be reproduced separately
   for every original ID. A continuation runs once per original completion,
   not once for the synthetic label.
9. The backend's atomicity/failure declaration and runtime fanout can reproduce
   an outcome permitted for each original operation. If partial-outcome
   equivalence cannot be proved, aggregation is illegal.
10. Checked addition proves that merged length and staged byte count equal the
    sum of original exact lengths.

The aggregate is a new sealed Write with a new ID. aggregation.original_ids
records the originals; the originals remain logical programs with their own
results. Bytes are concatenated in ascending scope order. The shuffler may not
reuse the first label, overwrite its destination, discard its sealed fields,
or aggregate before considering an intervening hazard.

The shuffler MUST NOT aggregate Read, Delete, Flush, Observe, Composite,
pipeline-bearing writes, URI/Pointer conflicts, unknown extents, differing
version constraints, or writes to different canonical identities.

### 9.5 Supertask/Composite formation

The shuffler may group labels into a new Composite to enforce an existing
acyclic ordering graph or share placement. It may:

- assign the new Composite ID;
- seal its child list;
- set each child's supertask_id residual;
- choose a topological child order.

It may not alter child resources, descriptors, inputs, durability,
continuations, dependencies, IDs, or results. Co-location does not authorize
serializing otherwise unordered operations. Grouping preserves each child's
readiness and priority. If a scheduler can expose only one Composite priority,
it uses the maximum priority among currently ready children while retaining
every child's sealed priority for later choices. TTL is checked per child; a
Composite-level scheduling value must not hide an earlier child deadline.

### 9.6 Scheduler deferral and cancellation

The scheduler may defer a label before Executing when:

- a dependency is incomplete;
- no feasible worker/backend capability is currently available;
- an isolation, durability, locality, or resource requirement is temporarily
  unsatisfied;
- backpressure policy requires a delay that remains within TTL.

Deferral retains the admitted label and its ordering edges, records a reason,
and requeues it. An empty worker list or empty solver assignment is deferral,
not permission to skip or drop the label.

The scheduler may terminate a not-yet-executing label only for:

- an authenticated producer cancellation request;
- TTL expiration;
- terminal dependency failure;
- a requirement proved permanently unsatisfiable.

Permanent infeasibility has a finite proof set:

- admission finds no registered backend adapter for the declared
  family/descriptor combination: BACKEND_UNSUPPORTED;
- two sealed requirements contradict each other or the selected backend's
  registered immutable capability, including an unsupported durability or
  isolation guarantee: UNSATISFIABLE_REQUIREMENTS;
- an authoritative catalog/administrator record permanently revokes the named
  resource or required capability: UNSATISFIABLE_REQUIREMENTS.

Zero currently registered workers, a busy worker, a stopped elastic pool, a
missing current placement, or a transient backend outage is not such proof and
therefore causes deferral. When the deployment permits dynamic registration,
absence from the current worker list is always temporary unless an
authoritative revocation says otherwise.

The current StatusCode has no Cancelled value, so the compatibility
representation is Failed with CANCELED, EXPIRED, DEPENDENCY_FAILED, or
UNSATISFIABLE_REQUIREMENTS. The scheduler may not cancel merely to improve
cost, throughput, or placement score.

Once Executing begins, the scheduler itself cannot cancel the label. A future
cooperative executor cancellation must obey the descriptor's last safe
cancellation point and the completion rules; that mechanism belongs to the
asynchronous-completion Working Set.

## 10. Deterministic validation rules

### 10.1 Structural verification

Before any generated accessor or union payload is dereferenced, the receiver
MUST:

- reject empty or truncated buffers;
- run the FlatBuffers verifier for the expected root;
- verify union discriminator/payload consistency;
- range-check every enum after decoding;
- enforce configured vector/string/pipeline size limits;
- catch codec and pipeline decode errors and translate them to stable
  categories.

This applies at dispatcher ingress, worker ingress, completion receipt,
embedded continuation-template decode, and packed child decode. Verification
failure never throws through a service callback and never crashes the process.

### 10.2 Common producer-admission rules

A normalized version-1 producer submission is valid only if:

- ir_version is 1;
- id is nonzero and not already admitted;
- type and canonical core descriptor agree;
- operation_version is supported;
- app_id matches the authenticated principal;
- all required ResourceRefs are present and valid;
- every compatibility address is absent or equivalent to its canonical
  resource;
- no user resource addresses internal NATS or DragonflyDB plumbing;
- the resource version constraint is well formed;
- priority, intent, isolation, durability, TTL, and data_size are internally
  consistent;
- input_binding, when present, has a nonempty content_id, an authenticated
  content mapping, matching logical length/digest, and provenance consistent
  with source_resource;
- flags is zero before lifecycle initialization;
- dependencies, routing, supertask_id, aggregation, score_snapshot, and hops
  are empty; the semantic result is absent;
- status is Created; timestamps are zero except that trusted ingress may set
  created_us;
- declared_dependencies are unique, non-self, reference known/admissible
  predecessors, and form an acyclic graph after lowering to residual edges;
- continuation fields satisfy Section 10.4;
- integer range arithmetic does not overflow.

Trusted ingress then sets created_us, canonical initial state, derived
file_key, normalized dependency edges, and ir_version before sealing.

### 10.3 Per-operation admission matrix

| Type | Source | Destination | Staged input | Pipeline | children | data_size |
|---|---|---|---|---|---|---|
| Read | Exactly one | Optional, at most one | Only as a proved materialization of the source | Empty in version 1 | Empty | Zero for unbounded/whole value, otherwise consistent with requested scope |
| Write | Optional | Exactly one | Required when source is absent; otherwise only a proved source materialization | Optional and valid | Empty | Equal to known direct input size before transformation, or zero when source size is unknown |
| Delete | Absent | Exactly one | Absent | Empty | Empty | Zero |
| Flush | Absent | Exactly one; whole target barrier | Absent | Empty | Empty | Zero; durability must be Durable |
| Observe | Exactly one | Absent | Absent | Empty | Empty | Zero |
| Composite | Absent | Absent | Absent | Empty | One or more unique, non-self IDs | Zero or a runtime-derived informational sum, never producer authority |

Additional illegal combinations include:

- source and staged input without the C3 binding proof;
- source/destination compatibility forms that normalize differently;
- MemoryPtr execution across processes without a transfer binding;
- version constraints attached to no applicable resource;
- a Composite carrying direct backend effects;
- a file/object Delete whose scope is less than the whole resource;
- a Delete or Flush carrying payload bytes;
- a pipeline whose sink has no destination;
- a descriptor whose declared resource families do not match the label;
- a Write aggregate whose original labels or byte extents cannot be proved;
- a user URI naming warehouse or queue plumbing.

### 10.4 Continuation validation

| kind | Required fields | Fields that must be empty | Version-1 rule |
|---|---|---|---|
| None | None | target_channel, chain_params, condition | Valid |
| Notify | Valid ChannelResource name in target_channel | chain_params, condition | Valid |
| Chain | chain_params containing a structurally verified and normalizable label template | target_channel, condition | Valid; activated label receives new id/app identity and full admission validation |
| Conditional | A future typed predicate plus valid chain template | target_channel and arbitrary text | Not admitted in version 1 while condition is only an untyped textual expression |

JSON label descriptions and textual expressions in current unit fixtures are
not a second Label I/O language. They remain structurally serializable
version-0 strings but are rejected at executable admission unless an explicit
legacy normalizer recognizes a safe structured representation.

### 10.5 Runtime residual validation

Every residual mutation checks both caller identity and phase:

- dependency/hazard edges are append-only, non-self, and acyclic;
- children never change after Composite sealing;
- file_key equals the canonical file identity key;
- routing names a registered feasible worker and is frozen at Executing;
- score values are finite and correspond to the routing attempt;
- hops are append-only, component names are nonempty, and times are
  nondecreasing;
- aggregation is present only on a synthetic Write satisfying Section 9.4;
- supertask_id names a distinct Composite that contains the child.

Any unauthorized residual mutation is UNAUTHORIZED_MUTATION and must stop the
label before further external I/O.

### 10.6 Lifecycle and result validation

Normal transitions are:

    Created -> Queued -> Shuffled -> Scheduled -> Executing
    Executing -> Complete
    Executing -> Failed

Allowed special transitions are:

- Queued directly to Executing for a registered dispatcher-local executor such
  as core Observe;
- Shuffled or Scheduled back to Queued for documented pre-execution deferral;
- Created, Queued, Shuffled, or Scheduled to Failed for validated rejection,
  cancellation, expiration, dependency failure, or permanent infeasibility.

Complete and Failed are terminal. Complete to Failed, Failed to Complete, and a
second terminal result are INVALID_STATE_TRANSITION.

Required state coherence:

- created_us is nonzero on every admitted label;
- queued_us is nonzero at Queued and later;
- dispatched_us and routing are present at Scheduled and normal worker
  execution;
- started_us is nonzero before any external I/O and at Executing or later;
- completed_us is nonzero only at a terminal status;
- nonzero timestamps are ordered created <= queued <= dispatched <= started <=
  completed, omitting dispatcher-local phases that the registered executor
  does not use;
- Complete has empty result.error;
- Failed has a stable nonempty category and detail;
- Pending is cleared at terminal status; lifecycle flags agree with status;
- result is absent before terminal status and immutable afterward.

Result presence is semantic, not inferred from the physical presence of the
current optional LabelResult table. At a nonterminal status, an all-default
LabelResult means no result. At Complete, the same all-default value is the
valid result of a zero-byte operation. At Failed, nonempty result.error makes
the result explicit. A future codec may preserve table presence, but version-1
correctness does not depend on it.

Recovery snapshots may omit historical timestamps only when the recovery
record explicitly marks them unavailable. Contradictory nonzero ordering,
terminal rewrites, or state/program disagreement remains invalid.

## 11. Stable failure categories and rejection behavior

### 11.1 Categories

The stable version-1 categories are:

| Category | Meaning |
|---|---|
| MALFORMED_BUFFER | FlatBuffer structure, root, union, or codec data is invalid. |
| LIMIT_EXCEEDED | A configured safe size/count/depth limit is exceeded. |
| UNSUPPORTED_IR_VERSION | ir_version is unknown and nonzero. |
| INVALID_ENUM | A decoded enum value is outside its declared set. |
| INVALID_IDENTIFIER | A descriptor, namespace, or resource identifier violates its grammar. |
| AUTHORIZATION_FAILED | The authenticated principal may not submit or address the declared program/resource. |
| UNKNOWN_REFINEMENT | Operation name/version is not in the descriptor registry. |
| UNKNOWN_RESOURCE | URI scheme, family extension, or resource schema is not registered. |
| FORBIDDEN_INTERNAL_RESOURCE | A user address targets LABIOS queue/warehouse plumbing. |
| ADDRESS_CONFLICT | Multiple representations of one role normalize to genuinely different resources/scopes/versions. |
| AMBIGUOUS_INPUT | Source and staged input remain unbound or otherwise identify competing inputs after version-0 rules. |
| STAGED_CONTENT_UNAVAILABLE | A sealed input_binding cannot resolve the exact bound bytes during admission/recovery/execution. |
| MISSING_FIELD | A required operation field or resource is absent. |
| ILLEGAL_COMBINATION | Individually valid fields form a forbidden program combination. |
| INVALID_RESOURCE | A typed resource or scope is malformed. |
| UNSAFE_MEMORY_REFERENCE | A memory address is not usable in the executor's address space. |
| RESOURCE_VERSION_CONFLICT | An Exact or MustNotExist constraint fails. |
| INVALID_PIPELINE | Stage registry, typing, graph, or sink validation fails. |
| INVALID_DEPENDENCY | An edge is invalid, unresolved, self-referential, or cyclic. |
| DUPLICATE_ID | The label ID is zero where prohibited or already admitted. |
| UNAUTHORIZED_MUTATION | A component changed a field it does not own. |
| INVALID_STATE_TRANSITION | Lifecycle or result state is incoherent or non-monotonic. |
| BACKEND_UNSUPPORTED | No feasible backend/worker supports an otherwise valid program. |
| UNSATISFIABLE_REQUIREMENTS | Requirements are proved permanently infeasible. |
| DEPENDENCY_FAILED | A required predecessor terminated unsuccessfully. |
| COMPOSITE_ABORTED | An otherwise runnable child was never started because its Composite applied fail-fast after a different child failed. |
| CANCELED | An authenticated pre-execution cancellation succeeded. |
| EXPIRED | The latest-start TTL elapsed before the label entered Executing. |
| EXECUTION_FAILED | A registered stage or backend failed after valid admission. |

Implementations may add diagnostic detail but must not change the category's
meaning within IR version 1.

### 11.2 Primary-category precedence

Validation runs in this deterministic order, and the first failed check is the
primary category returned in Completion.error:

1. FlatBuffer/root/union verification: MALFORMED_BUFFER.
2. Safe resource limits: LIMIT_EXCEEDED.
3. IR support: UNSUPPORTED_IR_VERSION.
4. Enum and identifier syntax: INVALID_ENUM, then INVALID_IDENTIFIER.
5. Principal and label identity: AUTHORIZATION_FAILED, then DUPLICATE_ID.
6. Descriptor registry: UNKNOWN_REFINEMENT.
7. Resource normalization: UNKNOWN_RESOURCE, FORBIDDEN_INTERNAL_RESOURCE,
   INVALID_RESOURCE, UNSAFE_MEMORY_REFERENCE, ADDRESS_CONFLICT,
   AMBIGUOUS_INPUT, STAGED_CONTENT_UNAVAILABLE, then
   RESOURCE_VERSION_CONFLICT.
8. Required operation roles: MISSING_FIELD.
9. Cross-field and capability rules: ILLEGAL_COMBINATION,
   BACKEND_UNSUPPORTED, then UNSATISFIABLE_REQUIREMENTS.
10. Pipeline and ordering: INVALID_PIPELINE, then INVALID_DEPENDENCY.
11. Ownership and initial lifecycle: UNAUTHORIZED_MUTATION, then
    INVALID_STATE_TRANSITION.

An implementation may include secondary diagnostics after the primary detail,
but it sorts them by this same category order and then by qualified field path.
For example, the resource-less watch_dir fixture has primary
UNKNOWN_REFINEMENT and secondary MISSING_FIELD; clients can rely on the first
category.

### 11.3 Exact rejection behavior

On structural or admission rejection:

1. Do not enqueue, shuffle, schedule, catalog as admitted, stage new data, or
   call a backend.
2. If a transport reply route exists, send Completion with status Error,
   safely decoded label_id or zero, empty data_key, and:

       CATEGORY: diagnostic detail

3. If no reply route exists, record the category in an invalid-input metric
   and quarantine or discard the unadmitted payload according to deployment
   policy. This is rejection of invalid input, not silent loss of an admitted
   label.
4. Do not throw through the service callback, terminate a thread, or crash.

On an invalid runtime transition before external I/O, mark the admitted label
Failed with the stable category and publish its completion. If an internal
contract violation is detected after an external effect may have occurred,
record EXECUTION_FAILED plus the internal category in detail and do not claim
rollback or successful cancellation.

On invalid Completion input, the client reports a typed protocol failure. It
must not dereference the payload or wait indefinitely.

## 12. Implementation status, compatibility fixtures, and migration evidence

### 12.1 Normative contract versus current capability

The following are normative additions or requirements, not claims about the
current branch:

- ir_version and operation_version do not exist in the current schema or
  LabelData.
- typed ResourceRef, source_resource, destination_resource,
  declared_dependencies, serialized StagedInputBinding/input_binding, and
  HazardType Order/Barrier are implemented by the P03 ingress/codec slice;
  downstream consumers still intentionally read compatibility mirrors until
  their migration slices.
- no current ingress path runs a FlatBuffers verifier.
- no current component enforces sealed-field ownership or the validation
  contexts in this document.
- current clients may stage data and create catalog state before dispatcher
  admission; version-1 preflight/admission ordering and cleanup of rejected
  pre-staged content are not implemented.
- the current operation string is not interpreted through a descriptor
  registry.
- Delete and Flush exist in LabelType but are not wired through the worker
  execution path.
- the dispatcher and worker use different legacy address representations.
- current pipeline Write execution retrieves warehouse bytes and does not load
  the declared source URI.
- current pipeline_data uses a tab/newline codec and current output_stage
  execution does not enforce the graph contract.
- current aggregation and hazard detection are FilePath/file_key and
  batch-local; current aggregation does not preserve all sealed fields and can
  cross hazards.
- the current dispatcher can abandon a batch when no workers are available;
  the contract requires deferral or explicit failure.
- current Composite formation reuses a child ID; the contract requires a new
  identity.

Current File, SQLite, and optional external-KV adapters do not imply
implementation of every ResourceRef family. The resource union specifies the
semantic shape later Working Sets validate and implement incrementally.

### 12.2 Current production label normalization

The intended version-0 compatibility outcomes are:

| Current producer shape | Version-1 normalization |
|---|---|
| LabelManager Write with operation write, staged bytes, MemoryPtr source, and FilePath destination | core.write version 1; one synthesized MemoryResource source owned by the authenticated session; staged MaterializedSource transfer binding; canonical file destination |
| LabelManager Read with operation read, FilePath source, and null MemoryPtr destination | core.read version 1; canonical file source; no destination_resource; bytes returned through Completion retrieval |
| URI Write with staged bytes and no source | core.write version 1; DirectProducer staged input; typed URI destination |
| URI Read with source URI | core.read version 1; typed source |
| write_with_intent with FilePath destination, staged bytes, and no source | core.write version 1; DirectProducer staged input; canonical file destination; sealed intent and priority |
| Pipeline Write with source URI, destination URI, and no staged bytes | core.write version 1; source loaded through its backend before stages; typed destination |
| Observe with observe URI and empty operation | core.observe version 1 with registered runtime-observation resource |
| Equivalent Pointer and URI forms for one role | One canonical ResourceRef; no ADDRESS_CONFLICT |

These outcomes depend on the URI scheme/resource backend being registered and
the submission otherwise passing version-1 validation.

### 12.3 Unit-fixture classification

This classification is documentation and test-plan input for later prompts. It
does not require P02 to rewrite the fixtures.

| Existing unit fixture | Contract classification |
|---|---|
| Full Write with write_block, dependencies, children, and producer-set runtime flags | Structurally valid codec fixture. write_block is not an alias; children are illegal on Write; residual/state ownership requires normalization or rejection. Not executable version 1. |
| Read from serialized stack MemoryPtr to FilePath | Structurally valid. Executable only in a proved same-process context; otherwise UNSAFE_MEMORY_REFERENCE. |
| Observe with watch_dir and no source resource | Structurally valid codec fixture; UNKNOWN_REFINEMENT and MISSING_FIELD at admission. |
| Intent, isolation, durability, version, or continuation tests built as resource-less default Writes | Structurally valid field roundtrips; MISSING_FIELD at executable admission. |
| Notify continuation on a resource-less Write | Continuation shape may be valid, but the enclosing Write is not. |
| Chain JSON and textual Conditional fixtures | Structurally serializable legacy strings; not valid version-1 structured continuations. |
| Routing/hops while status remains Created | Structurally valid residual roundtrip; invalid producer admission and incoherent as a version-1 runtime snapshot. |
| Executing without started_us, or Created with queued/dispatched/started/result fields | Structurally valid state roundtrip; INVALID_STATE_TRANSITION as version-1 state. |
| version 42 on a resource-less Write | Proves resource version serialization and separation from ir_version; MISSING_FIELD at admission. |
| labios://warehouse URI fixture | Structurally valid string roundtrip; FORBIDDEN_INTERNAL_RESOURCE at admission. |
| Minimal resource-less Write | Structurally valid codec fixture; MISSING_FIELD at admission. |
| Literal all-default labels with id zero | Structurally valid default/codec fixtures; DUPLICATE_ID and MISSING_FIELD at admission. |
| Scheduler lifecycle helper on a resource-less Write | The helper's Created through Scheduled mutations are independently testable, but the enclosing program was never admissible because its input and destination are missing. |
| Worker lifecycle helper on a resource-less Write | Codec/helper evidence only: the program is missing resources, Created directly to Executing is invalid for a worker Write, Pending remains set at Complete, and the later Complete to Failed rewrite is a second illegal terminal transition. |
| CompletionData Error with unprefixed disk full and nonempty warehouse data_key | Structurally valid completion codec fixture; intentionally invalid version-1 Completion because Error requires a stable category prefix and empty data_key. Completion semantic validation remains separate from generic decode. |

### 12.4 Evidence expected from implementation prompts

Later implementation work should use this contract to add:

- verifier rejection tests for corrupt labels and completions;
- version-0 normalization tests for current production shapes;
- typed-resource equivalence and ADDRESS_CONFLICT tests;
- staged-input binding and AMBIGUOUS_INPUT tests;
- descriptor alias and unknown-refinement tests;
- per-operation admission matrices;
- mutation-authority and lifecycle-transition tests;
- shuffler legality tests that prove no merge crosses RAW, WAW, WAR, or Flush
  edges;
- no-worker deferral rather than silent loss;
- source -> pipeline -> destination execution evidence.

Those changes are outside P01. This document is the semantic authority against
which P02, P03, and P04 are evaluated.
