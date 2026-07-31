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

This section preserves the original 68-field inventory: 30 fields in baseline
support tables, 34 in Label, and the original 4 in Completion. All 34 baseline
Label fields map to the corresponding LabelData members. The current C++
header is include/labios/label.h.

Append-only additions are classified where their contracts are introduced.
The ir_version, operation_version, typed ResourceRef,
declared_dependencies, and StagedInputBinding fields are classified in
Sections 3 through 5 and Section 8. Scheduling evidence is classified in
Section 9.7.8. Completion execution observations are rows 69 through 77 below.

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
| 69 | Completion.observation_version | CompletionData.observation_version | Execution state | Worker completion coordinator sets version 1 only when all timestamps for one worker attempt are coherent. Zero means no worker execution observation is present. |
| 70 | Completion.worker_id | CompletionData.worker_id | Execution state | Worker completion coordinator records the actual executor. The default -1 is valid only when observation_version is zero. |
| 71 | Completion.attempt | CompletionData.attempt | Execution state | Worker completion coordinator copies the scheduling attempt represented by the delivered label. It is positive when observation_version is 1. |
| 72 | Completion.queued_us | CompletionData.queued_us | Execution state | Worker completion coordinator copies the label's first queued-admission timestamp without rewriting it. |
| 73 | Completion.dispatched_us | CompletionData.dispatched_us | Execution state | Worker completion coordinator copies the timestamp of this scheduling assignment. |
| 74 | Completion.started_us | CompletionData.started_us | Execution state | Worker completion coordinator copies the timestamp recorded immediately before covered worker execution begins. |
| 75 | Completion.completed_us | CompletionData.completed_us | Execution state | Worker completion coordinator copies the terminal timestamp after covered execution settles. |
| 76 | Completion.queue_delay_us | CompletionData.queue_delay_us | Execution state | Version 1 records exactly started_us minus queued_us. It is an actual per-label queue delay, not a worker EWMA or queue-depth input. |
| 77 | Completion.service_time_us | CompletionData.service_time_us | Execution state | Version 1 records exactly completed_us minus started_us. It is an actual per-label worker service observation, not a scheduling-time trace input. |

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

### 9.7 Resource-aware scheduling and placement

This subsection defines the normative WS3 solver-input and placement contract.
It is a contract for a future implementation slice, not a claim that the
current dispatcher, solver interfaces, worker registry, or score residual
already implement it. The scheduler replaces opaque label-byte inputs with
typed runtime descriptors, prepares one complete post-shuffler batch, applies
one policy-independent legality and feasibility gate, and then asks the
selected policy to choose workers. Every unit receives an explicit assigned or
deferred decision.

#### 9.7.1 Batch boundary and placement objects

The conceptual internal interfaces are:

```text
SchedulingBatch
  batch_id
  registry_generation
  scheduling_units[]       // stable P01-legal order

SchedulingUnitDescriptor
  unit_id
  job_ordinal
  members[]                // one JobDescriptor, or ordered Composite children
  readiness
  external_dependencies[]

PreparedSchedulingBatch
  scheduling_batch
  worker_snapshot
  feasibility_matrix       // every unit/worker evaluation
  per_worker_capacity_budget

PlacementPlan
  decisions[]              // exactly one, in input order, per scheduling unit

PlacementDecision
  unit_id
  Assigned(worker_id) | Deferred(park_reason)
  policy_evidence
```

A raw dispatcher collection boundary closes when the configured size threshold
wakes the collector or when the configured timeout expires. Every label moved
from the input buffer at that instant belongs to that raw batch; a label that
arrives afterward belongs to the next raw batch. Dispatcher-local core Observe
labels remain outside worker scheduling. Every other admitted label that
survives shuffling participates in exactly one post-shuffler
`SchedulingBatch`.

The post-shuffler batch contains all placement units represented by the current
`direct_route`, `supertasks`, and `independent` outputs. Read-locality and
write-locality results become typed preferred or hard locality inputs to the
common path; neither is permission to bypass availability, tier, operation,
backend-attachment, locality, or capacity checks.

Placement-unit rules are:

- A non-aggregated label is one scheduling unit with one `JobDescriptor`.
- A valid aggregate is one unit described by its new synthetic label. Its
  original labels remain independently attributable as required by Section
  9.4.
- A Composite/supertask is one co-location unit whose `members` are the ordered
  descriptors of all children. One worker must be feasible for every member,
  and a policy MUST NOT split or reorder the children.
- Dependency-blocked units remain represented in the batch. Only the P01-ready
  frontier may be assigned; a blocked unit receives a deferred decision and
  remains on the P05 queued/parked path rather than disappearing.
- A solver is invoked once for the complete prepared batch, not once per label,
  aggregate, Composite, intent, or locality class. Intent remains a per-job
  policy input inside that invocation.
- A solver chooses workers only. It does not receive ownership of `LabelData`,
  move or rewrite serialized labels, mutate dependencies, change child order,
  or return a replacement execution order.
- `PlacementPlan.decisions` is aligned one-to-one with the input units and is
  in exactly the same order. A missing, duplicate, unknown, out-of-order,
  infeasible, or over-budget assignment fails common plan validation. No such
  assignment is dispatched; the corresponding admitted unit is deferred
  through P05 parking.
- The dispatcher retains the corresponding `LabelData` objects, applies only
  validated decisions, appends the scheduling residual, persists the lifecycle
  transition, and only then serializes worker deliveries.

`batch_id`, `job_ordinal`, readiness, external dependency status, registry
generation, and physical catalog residence are runtime scheduling context.
They are not new producer-visible fields in the sealed I/O program.

#### 9.7.2 JobDescriptor derivation and byte demand

Every program property in a `JobDescriptor` is derived from an existing sealed
Label/P03 field, a registered operation descriptor, or authenticated runtime
catalog state:

| Descriptor area | Required contents | Existing source |
|---|---|---|
| Identity | Stable label ID and scheduling-unit membership | `Label.id`, `children`, `supertask_id`, and aggregation residual |
| Operation | `LabelType`, canonical operation name, and operation version | `type`, `operation`, and `operation_version`; empty/read/write aliases are canonicalized by the single Section 4 helper |
| Byte demand | Known reservation bytes, whether complete size is exact, and the derivation basis | `input_binding.logical_length`, nonzero `data_size`, exact typed-resource extents, aggregation length, or a checked Composite-member sum |
| Resource requirements | Role, access mode, family, backend ID, resolved adapter scheme, canonical identity, scope, and version constraint | P03 `source_resource`/`destination_resource`, the registered operation descriptor, and Section 9.1 access-record derivation |
| Locality | Preferred locality domains and any semantic hard-locality domain | Typed resource identity plus authenticated catalog residence; raw local-memory and worker-private attachments may impose hard locality |
| Pipeline and tier | Minimum tier and every required pipeline operation/version | Decoded pipeline stages; a nonempty pipeline requires at least Tier 1, and no current core program inherently requires Tier 2 |
| Policy inputs | Intent and priority | `intent` and `priority` |
| Legality inputs | Predecessor IDs and hazard kinds | Canonical append-only `dependencies`, after declared dependencies and shuffler hazards have been lowered |
| Other hard requirements | IR version, isolation, durability, and checked latest-start deadline | `ir_version`, `isolation`, `durability`, and checked `created_us + ttl_seconds` |

No row requires another producer-visible Label field. The only Label schema
change selected by this contract is append-only runtime-residual scheduling
explainability under Section 9.7.8; it does not add a second wire format or a
new sealed job input.

Descriptor derivation is usable only for typed locators that survive the queue
round trip losslessly. Full C++ decoding for Network, Object, Vector, Graph,
Channel, Workspace, and Extension locators remains a P03 follow-up; P09 MUST
NOT advertise one of those families until its identity and locator are
preserved. File, SQLite, and optional external KV are the initial executable
capability set. Internal DragonflyDB is warehouse/catalog plumbing and MUST
NOT appear as a user-backend requirement or worker attachment.

Byte-demand derivation obeys all of these rules:

1. Known inputs and outputs contribute a checked byte lower bound. The
   reservation for one label is the maximum concurrently relevant known
   footprint under its registered operation contract. The reservation for an
   atomic Composite unit is the checked sum of its member reservations.
2. `data_size == 0` means unknown or operation-defined, not a proved zero-byte
   demand.
3. A pipeline output remains unknown unless the registered stage declarations
   derive a bound. A known input remains a reservable lower bound even when the
   output is unknown.
4. Unknown residual size does not fabricate a byte count and does not by itself
   make every worker infeasible. Capacity feasibility enforces the known
   reservation and records `complete_size_known=false` in decision evidence.
5. Delete, Flush, and other metadata-only effects have zero payload reservation
   only when their registered operation contract declares that they carry no
   bytes.
6. Overflow, underflow, or any other checked-arithmetic failure is rejected
   before policy evaluation.

#### 9.7.3 P01 legality, readiness, order, and priority

The scheduler consumes the canonical residual dependency graph. It MUST NOT
infer semantic order from transport arrival, label ID, application ID,
priority, timestamp, or input-vector position.

A stable runtime ordinal is captured before shuffling. It is a deterministic
tie-break and preserves relative order where P01 does not authorize
reordering; it is not a new program-order edge. The scheduling coordinator
computes or validates a P01 topological order before policy selection. If
aliasing, scope, descriptor commutativity, or an ordering edge is unknown, the
original legal relative order is retained.

Priority may prefer a higher-priority unit only among simultaneously ready
units whose relative order is legally interchangeable under Section 9.3. A
policy may place multiple independent ready units on different workers, but it
MUST NOT dispatch a successor before its predecessor completes unless a valid
Composite execution unit enforces that order internally. If a predecessor is
parked, its successor remains deferred; unrelated ready units may still be
placed. Composite child order is fixed by a validated P01 topological order
before policy selection and is immutable inside the solver.

Policy therefore chooses placement, not legal execution order. Partial success
is allowed only across independent scheduling units: feasible ready units may
be assigned while unrelated blocked or currently infeasible units park. A
Composite is all-or-nothing.

#### 9.7.4 WorkerDescriptor and metric meanings

One immutable worker snapshot is captured for a scheduling attempt. Every
`WorkerDescriptor` contains:

- identity and snapshot context: worker ID, registration/descriptor epoch,
  registry generation, and manager capture time;
- dynamic resource state: available flag, normalized remaining-capacity ratio,
  absolute total and available capacity bytes, load ratio, speed class,
  energy-cost class, and the existing optional skills, compute, and reasoning
  values;
- execution capability: worker tier, maximum supported IR version, supported
  core/refinement operation versions, and advertised pipeline operations;
- backend capability: an exact set of attachments, each containing
  `ResourceFamily`, `backend_id`, compatible adapter scheme, locality kind, and
  locality domain; and
- locality domains exposed independently of individual backend attachments.

Static capability values are replaced only by a registration carrying a new
registration epoch. Dynamic availability, load, and capacity updates merge
into the descriptor for that epoch. A stale update from another epoch is
rejected. The manager increments `registry_generation` whenever registration,
deregistration, availability, load, capacity, or static capability state
changes. One scheduling attempt uses exactly one immutable generation; a
refresh affects only a subsequent attempt or legal pre-execution reschedule.

Metric meanings are fixed as follows:

- Capacity and load are finite and normalized to `[0,1]`.
  `available_capacity_bytes` is the hard comparable unit for byte demand;
  normalized capacity remains a policy input.
- Speed is a class from 1 through 5, with higher values meaning faster
  service. Its standard normalized desirability is `speed / 5`.
- Energy is a cost class from 1 through 5, with lower values preferred. A
  positive Constraint weight applies to inverse desirability
  `1 - (energy - 1) / 4`, never directly to the cost class. MinMax retains its
  lower-energy preference in its profit function.
- Tier is a hard minimum before it may be a scoring component. Tier 0 is
  Databot, Tier 1 is Pipeline, and Tier 2 is Agentic.
- `WorkerInfo.score` is not registry truth. A composite score is derived per
  job, policy profile, and scheduling attempt from the captured raw values.

#### 9.7.5 Verified worker-registry protocol v2

The three CSV/text registration, resource-update, and snapshot exchanges plus
the plain-text deregistration control are replaced together by a generated
`worker_registry.fbs` protocol with one envelope:

```text
WorkerRegistryMessage
  protocol_version = 2
  payload union:
    WorkerRegistration
    WorkerResourceUpdate
    WorkerDeregistration
    WorkerRegistrySnapshot
```

The FlatBuffers file identifier is `LWR2`. Every receiver verifies the buffer
before generated access, checks the file identifier and
`protocol_version == 2`, checks union
discriminator/payload consistency, and rejects an unexpected payload kind.
Malformed values never partially update the manager registry.

Required field flow is:

| Descriptor information | `WorkerRegistration` on `labios.worker.register` | `WorkerResourceUpdate` on `labios.worker.score_update` | `WorkerDeregistration` on `labios.worker.deregister` | `WorkerRegistrySnapshot` returned by `labios.manager.workers` |
|---|---|---|---|---|
| Protocol version and payload kind | Required | Required | Required | Required |
| Worker ID and registration epoch | Required | Required and must match the live epoch | Required and must match the worker being removed | Required for every worker row |
| Registry generation and manager capture time | Assigned/advanced by manager after merge | Assigned/advanced by manager after merge | Advanced by manager after successful removal | Required once for the immutable snapshot |
| Availability | Required initial value | Required current value | Omitted; removal is the operation | Required merged value |
| Total bytes, available bytes, normalized capacity, and load | Required initial values | Required current values | Omitted | Required merged values |
| Speed class, energy-cost class, tier, and maximum IR version | Required static values | Omitted; retained from matching registration epoch | Omitted | Required merged values |
| Core/refinement operation versions and pipeline operations | Required structured sets | Omitted; retained from matching registration epoch | Omitted | Required structured sets |
| Backend attachments and independent locality domains | Required structured rows/sets | Omitted; retained from matching registration epoch | Omitted | Required structured rows/sets |
| Optional skills, compute, and reasoning values | Advertised when supported | Resource update changes only fields declared dynamic by the v2 schema | Omitted | Preserved in merged form |

The manager removes a worker only when `WorkerDeregistration.worker_id` and
`registration_epoch` match the live descriptor. A malformed, unknown, or stale
deregistration does not remove a newer registration and does not advance
`registry_generation`; a successful removal advances the generation exactly as
any other registry-state change. Every subject rejects a verified v2 payload
whose union kind is not the kind assigned to that subject.

Attachments are structured rows, not delimiter-encoded strings, so a
family/backend/scheme/locality tuple cannot be split or conflated by CSV
parsing. Duplicate worker IDs, invalid epochs, invalid numeric ranges,
inconsistent attachments, and duplicate/conflicting capability rows make the
message invalid.

There is no text/CSV fallback. Manager, workers, and dispatcher require one
coordinated runtime cutover in P09; mixed text/v2 operation is unsupported. The
former Python MCP CSV parser for `labios.manager.workers` is incompatible after
this cutover. Prompt 10 supplied generated Python FlatBuffers bindings and the
shared verified registry-v2 parser. Prompt 11 removes the MCP CSV path: normal
worker-score/count tools use public Observe labels, and any supplied registry
snapshot is accepted only through that shared `LWR2` protocol-v2 parser. The
prior P09 compatibility gap is closed without a text fallback.

#### 9.7.6 Common feasibility and plan validation

Before any policy optimization, one common engine evaluates every
scheduling-unit/worker pair. For a Composite it evaluates every member, and the
pair is feasible only when all members pass. The engine evaluates all checks
and records every failed reason rather than stopping after the first:

1. The worker descriptor is structurally valid and the worker is currently
   available.
2. The worker supports every member's IR version and canonical
   operation/version.
3. The worker tier meets the maximum member requirement; Tier 0 always rejects
   a nonempty pipeline.
4. The worker advertises every pipeline operation/version required by the unit.
5. Every source and destination access has an exact supported backend
   attachment. A one-worker pipeline must support both its source and
   destination attachments.
6. Every hard locality requirement is satisfied. Preferred/shared locality is
   retained only as a policy input and never overrides a hard check.
7. The unit's known byte reservation fits the worker's captured absolute
   available capacity.
8. During assignment, the checked sum of known reservations already selected
   for the worker plus the candidate reservation fits that worker's captured
   per-batch available-byte budget.

The feasibility matrix and decision-time candidate rows use stable reason codes
including at least:

- `UNAVAILABLE`;
- `UNSUPPORTED_IR`;
- `UNSUPPORTED_OPERATION`;
- `INSUFFICIENT_TIER`;
- `MISSING_PIPELINE_OPERATION`;
- `MISSING_BACKEND_ATTACHMENT`;
- `HARD_LOCALITY_MISMATCH`;
- `INSUFFICIENT_SINGLE_JOB_CAPACITY`; and
- `EXHAUSTED_BATCH_CAPACITY`.

The matrix captures pair feasibility before policy optimization. The
decision-time row additionally captures capacity before and after the unit so
cumulative budget exhaustion is reproducible. A policy may choose only a
matrix-feasible worker whose remaining batch budget still fits the unit.

Placement outcomes map to P05 as follows:

- no registered workers produces `NO_WORKERS`;
- registered workers with none currently available produces
  `NO_AVAILABLE_WORKER`; and
- a ready unit with no current feasible candidate produces
  `NO_FEASIBLE_CURRENT_PLACEMENT`.

These are transient parking outcomes unless the finite Section 9.6 proof set
establishes permanent `BACKEND_UNSUPPORTED` or
`UNSATISFIABLE_REQUIREMENTS`. Missing capability in one current snapshot is not
permanent proof when dynamic registration is allowed. Catalog locality never
forces a bad placement. Every unassigned admitted unit, including a dependency-
deferred unit or a unit rejected by common plan validation, remains in the P05
queued/parked lifecycle with its reason and complete failed-attempt snapshot;
an empty assignment is never a `continue` that loses the unit.

#### 9.7.7 Preserved policy families

All policies consume the same prepared batch, candidate matrix, stable P01
unit order, and mutable per-worker batch budgets. Every policy returns one
explicit deferred decision when no candidate remains and MUST NOT omit a unit
or select outside the common feasible set.

**Round Robin.** Process units in P01-valid batch order. Sort workers by
ascending worker ID, begin at a persistent cursor, and scan circularly for the
first feasible worker whose remaining batch budget fits. Advance the cursor to
the next worker ID after a selection. Size affects feasibility and budget but
does not otherwise change Round Robin order. Evidence records the cursor before
selection and the selected scan rank.

**Random.** Process units in batch order. For each unit, form the sorted list of
feasible worker IDs with sufficient residual budget and select uniformly from
that list. Use a recorded batch seed and record the raw draw, candidate count,
and selected index. Historical assignment to suspended or otherwise infeasible
workers is superseded by the common filter.

**Constraint.** Retain per-job intent-adjusted weighted scoring. Score every
feasible candidate from availability, remaining-capacity ratio, inverse load,
normalized speed, inverse energy cost, tier, and the existing optional skills,
compute, and reasoning values. Standard normalizations are availability as
`1` for a feasible candidate, capacity unchanged in `[0,1]`, `1 - load`,
`speed / 5`, `1 - (energy - 1) / 4`, numeric tier divided by 2, skills and
compute unchanged in `[0,1]`, and reasoning divided by 5. The final score is
the sum of effective per-job weight times normalized value. Evidence records
the actual profile name and version and, for every candidate, each raw value,
normalization, effective per-job weight, and contribution. Soft locality is
the first equal-score tie-break; ascending worker ID is final.

**MinMax.** Retain the performance/energy profit and proportional batch-
distribution policy. The current profit family maximizes normalized speed,
remaining capacity, and inverse load while dividing by normalized energy cost:

```text
profit(w) = (speed / 5) * remaining_capacity_ratio * (1 - load)
            / max(energy / 5, 0.01)
```

When every relevant unit demand is known, target shares and consumption use
reservation bytes rather than label count; unknown-size units use unit counts
without fabricating byte demand. Skip infeasible or exhausted workers and spill
to the next profit-ranked feasible candidate. Equal profit resolves by soft
locality and then worker ID. Evidence records worker profit, target share,
known/unknown demand basis, capacity before/after, spill rank, and final batch
objective.

#### 9.7.8 Append-only explainable scheduling residual

The six existing `ScoreSnapshot` scalar slots retain their current order and
meaning. They are never reordered or reinterpreted. The schema appends a
version and decision history conceptually shaped as follows:

```text
ScoreSnapshot
  availability             // legacy mirror of latest selected worker
  capacity
  load
  speed
  energy
  tier
  decision_version = 1
  decisions[]              // append-only scheduling attempt history

SchedulingDecisionSnapshot
  decision_id
  batch_id
  scheduling_unit_id
  attempt
  registry_generation
  job_ordinal
  outcome                   // Assigned or Parked
  chosen_worker_id
  park_reason
  reservation_bytes
  complete_size_known
  candidates[]
  policy_evidence union

CandidateEvaluation
  worker_id
  feasible
  reason_codes[]
  available_capacity_before
  available_capacity_after
  locality_match
  score_components[]
  final_objective
  policy_rank
  selected

ScoreComponent
  metric
  raw_value
  normalized_value
  weight
  contribution

PolicyEvidence union
  RoundRobinEvidence
  RandomEvidence
  ConstraintEvidence
  MinMaxEvidence
```

One `SchedulingDecisionSnapshot` is appended for every assignment, failed
placement attempt, parking decision, and legal pre-execution reschedule. An
earlier attempt is never overwritten. Candidate rows are ordered by ascending
worker ID.

Policy evidence is sufficient to replay the particular policy:

- Round Robin carries the cursor before selection and selected scan rank.
- Random carries the seed, raw draw, candidate count, and selected index.
- Constraint carries the actual profile name/version; candidate components
  carry the effective weights and contributions used for that job.
- MinMax carries target shares, known/unknown demand basis, spill decisions,
  and the final batch objective; candidate rows carry capacity before and after
  the unit decision.

The latest successful decision mirrors the chosen worker's six raw legacy
metrics into the existing scalar fields. A parked attempt does not fabricate a
chosen vector. An all-zero chosen vector or a parked attempt MUST still
serialize `ScoreSnapshot` whenever decision history is nonempty; serialization
presence may no longer depend only on the six scalar values.

The completed residual must reconstruct hard exclusions, feasible
alternatives, cumulative capacity state, deterministic tie-breaking, policy
objective, selected worker, and the reason a scheduling attempt parked. These
are append-only runtime-residual additions under the mutation authority in
Section 2, not new producer requirements and not a second Label wire format.

#### 9.7.9 Required implementation evidence

The P09 correctness slice MUST add tests for all of these scenarios:

1. A mixed post-shuffler batch invokes the selected solver once and produces
   exactly one decision per scheduling unit.
2. Direct-read and write locality use the common feasibility path and cannot
   target an unavailable or incompatible worker.
3. Tier 0 rejects every pipeline unit; Tier 1 and Tier 2 remain eligible only
   if they also support all required pipeline and backend capabilities.
4. A worker missing either the source or destination attachment is infeasible
   for a one-worker pipeline.
5. A known job larger than a worker's absolute available bytes is infeasible.
6. Multiple individually fitting jobs cannot cumulatively exceed one worker's
   captured batch budget.
7. Unknown-size jobs do not fabricate capacity and record their uncertainty.
8. A parked predecessor prevents successor dispatch; unrelated ready jobs may
   still schedule.
9. Priority changes dispatch preference only within a P01-legally
   interchangeable ready frontier.
10. Round Robin remains Round Robin when feasibility is unchanged; Random
    samples only feasible workers; Constraint records exact weighted
    contributions; and MinMax responds to byte demand and residual capacity.
11. Empty feasible sets persist P05 parking state rather than producing an
    empty-assignment `continue`.
12. Registry v2 rejects malformed buffers, unknown versions, duplicate worker
    IDs, invalid ranges, and inconsistent capability attachments.
13. Every assigned and parked attempt can be replayed from the completed
    label's decision history, including equal-score tie-breaking, Round Robin
    cursor, Random draw, and MinMax batch context.

The required paper trace is one ready pipeline Write whose completed
dependencies leave an 8 MiB known reservation. Its source attachment is
`File/posix-main/file`, its destination attachment is
`Relational/sqlite-main/sqlite`, and it requires
`builtin://identity`. The pipeline output is treated as unknown for this trace,
so the reservation is 8 MiB and `complete_size_known=false`.

Candidate rows, in worker-ID order, are:

| Worker | Relevant descriptor | Result |
|---|---|---|
| 10 | Tier 0; both attachments; 64 MiB available | Infeasible: `INSUFFICIENT_TIER` |
| 20 | Tier 1; source attachment only; 64 MiB available | Infeasible: `MISSING_BACKEND_ATTACHMENT` for the destination |
| 30 | Tier 1; both attachments; 4 MiB available | Infeasible: `INSUFFICIENT_SINGLE_JOB_CAPACITY` |
| 40 | Tier 1; both attachments; 12/16 MiB available; load 0.25; speed 4; energy 2 | Feasible |
| 50 | Tier 1; both attachments; 18/20 MiB available; load 0.10; speed 3; energy 1 | Feasible |

For a trace profile `paper-trace/v1` with weight 0.20 each on availability,
capacity, inverse load, speed, and inverse energy, and zero on other metrics,
worker 40 records:

```text
0.20*1.00 + 0.20*0.75 + 0.20*0.75 + 0.20*0.80 + 0.20*0.75
= 0.81
```

Worker 50 records:

```text
0.20*1.00 + 0.20*0.90 + 0.20*0.90 + 0.20*0.60 + 0.20*1.00
= 0.88
```

Constraint therefore selects worker 50. The choice is reproducible from the
raw metrics, normalizations, weights, contributions, candidate order, and
tie-break metadata, and the latest successful decision mirrors worker 50 into
the six legacy scalar slots. Removing workers 40 and 50 leaves the first three
rejection rows, produces `NO_FEASIBLE_CURRENT_PLACEMENT`, and appends an equally
reconstructable parked snapshot with no fabricated chosen worker.

#### 9.7.10 Current capability and implementation gate

P09 implements the Section 9.7 runtime slice: one complete typed post-shuffler
batch reaches one selected policy invocation; every unit receives one validated
assigned/deferred decision; common feasibility checks cover capabilities,
attachments, locality, absolute demand, and cumulative capacity; and the
six-scalar residual now carries replayable append-only decision history.
Registry v2 is verified and coordinated across manager, worker, and dispatcher
with no runtime CSV/text fallback. The 13 hermetic P09 scheduling-feasibility cases, existing solver cases,
registry codec tests, and exact 8 MiB paper trace pass in the native unit lane.

The live mixed-batch scheduling integration path was not run because Docker
Compose was unavailable on the verification host; no live end-to-end claim is
made. Prompt 10 supplies Python FlatBuffers support, generated bindings, and a shared
verified registry-v2 parser. Prompt 11 routes MCP worker score/count through
public Observe labels and removes direct manager-subject and CSV access;
verified snapshots use only the shared parser. Automatic provisioning,
elasticity triggers, leader election, new solver algorithms, and distributed
pipeline-stage placement remain out of scope.

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

## 13. Asynchronous completion contract

This section defines completion as an IR/runtime property. A transport reply is
only a notification of a persisted completion record; it is not the record and
its loss does not make an admitted label disappear. The contract deliberately
does not select a NATS API, consumer type, or scheduler policy.

### 13.1 Lifecycle states and ownership

The semantic lifecycle is:

    Submitted -> Admitted -> Queued/Parked -> Shuffled -> Scheduled -> Executing
                                                                    |          |
                                                                    v          v
                                                               Cancelled   Completed/Failed

`Submitted` is a client-side state before the runtime accepts the label. It is
not an admitted runtime state and has no completion guarantee. `Admitted` means
the canonical, sealed label and its catalog record have been durably accepted.
`Parked` is a substate of `Queued`: the label is admitted but intentionally
waiting for a transient condition such as an empty worker pool. It is not a
failure and is not a second scheduling policy.

`Cancelled` is a semantic terminal state. The current flat wire status has no
Cancelled value, so a version-1 compatibility projection stores `Failed` and
emits `CANCELED: ...` in the result error; the catalog additionally records
`lifecycle=cancelled`. `Completed` and `Failed` project to the existing
Complete and Failed status values. A terminal label never leaves its terminal
state.

Each legal transition has one owner:

| Transition | Owner | Rule |
|---|---|---|
| Submitted -> Admitted | Dispatcher admission coordinator | Verify, normalize, validate, seal, persist the canonical label snapshot and catalog record. |
| Admitted -> Queued | Dispatcher admission coordinator | Publish or enqueue the admitted record only after its durable catalog record exists. |
| Queued/Parked -> Shuffled | Dispatcher shuffler | Apply only the legal P01 shuffle/aggregation decisions and persist the residual snapshot. |
| Shuffled -> Scheduled | Dispatcher scheduler | Record one feasible placement and its lease; scheduling policy remains WS3. |
| Shuffled -> Queued/Parked | Dispatcher scheduler | Defer before execution when placement or another transient prerequisite is unavailable. |
| Scheduled -> Queued/Parked | Dispatcher scheduler | Requeue a scheduled label when a pre-execution delivery/placement attempt is deferred. |
| Scheduled -> Queued/Parked after lease recovery | Dispatcher recovery coordinator | Reclaim a scheduled record whose dispatcher/worker delivery lease expired before execution. |
| Scheduled -> Executing | Worker executor | Atomically claim the delivery, record the worker lease and start time, then begin covered I/O. |
| Executing -> Completed | Worker completion coordinator | Persist the successful result only after the declared completion boundary, including durability, is satisfied. |
| Executing -> Failed | Worker completion coordinator | Persist the categorized failure; no rollback is implied. |
| Admitted/Queued/Parked/Shuffled/Scheduled -> Cancelled | Dispatcher cancellation coordinator | Win the pre-execution cancellation compare-and-set and prevent worker execution. |
| Admitted/Queued/Parked/Shuffled/Scheduled -> Failed | Dispatcher cancellation/expiry coordinator | Terminalize only for validated rejection, expiration, dependency failure, permanent infeasibility, or an equivalent pre-execution failure. |

The owner writes the state and its timestamps atomically with the catalog
version/lease check. A competing owner that loses that check must reread the
record and must not publish a second terminal transition. Recovery does not
invent a new transition from `Executing`: an execution record with an expired
worker lease is terminalized as `EXECUTION_FAILED: WORKER_LOST` unless a
persisted terminal completion already exists. This conservative rule avoids
claiming that an external effect was rolled back or safely repeating a possibly
non-idempotent operation.

The state representation is compatible with P01 and the current catalog:

| Semantic state | Label status | Required catalog meaning |
|---|---|---|
| Admitted | Created | `lifecycle=admitted`; canonical label snapshot exists. |
| Queued | Queued | Ready for dispatch; `parked=0`. |
| Parked | Queued | `parked=1`, reason, retry count, and next retry time are recorded. |
| Shuffled | Shuffled | Shuffler residual is persisted. |
| Scheduled | Scheduled | Worker, dispatch lease, and delivery attempt are recorded. |
| Executing | Executing | Worker claim and execution lease are recorded. |
| Completed | Complete | Terminal completion record is persisted. |
| Failed | Failed | Terminal category/detail and no successful data handle are persisted. |
| Cancelled | Failed | `lifecycle=cancelled`, category `CANCELED`, and no external I/O was started. |

A rejected submission that never reaches `Admitted` receives an error reply but
is not represented as a successful runtime label. An admitted label is never
silently discarded. `Created -> Queued -> Shuffled -> Scheduled -> Executing`
and the terminal transitions above are the only normal paths; terminal states
cannot transition again. The P01 TTL rule remains in force: expiration before
`Executing` is `EXPIRED`, while expiration during execution does not cancel or
rewrite the result.

### 13.2 Client observation and waiting

Client operations use a conceptual `CompletionView` containing the label ID,
current semantic state, terminal category/detail, result metadata, and (for a
successful read/observe) a retrieval handle or value. A worker-terminal view
also exposes the versioned executor, attempt, lifecycle timestamps, actual
queued-to-start delay, and actual start-to-terminal service time carried by the
Completion. Dispatcher-local rejection, cancellation, expiry, and Observe
completions may have no worker execution observation. This is an API contract,
not a textual IR or a parallel wire format.

- **`test(id)`** is nonblocking. It reads the catalog/completion record and
  returns `Pending` with the current nonterminal state, or the terminal view.
  `Parked` is reported as pending with its park reason and next retry time.
  It never waits for a transport reply and never cancels the label.
- **`wait(id, timeout)`** waits for that one label to become terminal. On
  `Completed` it returns success and the result. On `Failed` it returns a
  typed failure whose primary category is the stable P01 category. On
  `Cancelled` it returns `CANCELED`. If the deadline expires first it returns
  `Timeout` plus the last observed nonterminal state; the handle remains valid
  and the label continues running. A timeout is never a cancellation.
- **`wait_any(ids, timeout)`** returns one terminal view as soon as any member
  is terminal, including a failure or cancellation. If several are already
  terminal, selection is deterministic: earliest persisted terminal time,
  then lowest label ID. On timeout it returns `Timeout` and no selected label;
  all handles remain valid.
- **`wait_all(ids, timeout)`** waits until every member is terminal. It returns
  one result per input ID in input order, including successful, failed, and
  cancelled results. On timeout it returns the terminal results already known
  and `Pending` views for the remainder. It does not turn one failure into
  cancellation of unrelated labels.

An empty `wait_any`/`wait_all` set returns immediately with an empty result.
An unknown or never-admitted ID is a client lookup/submission error, not a
label failure. Repeating any wait or test is idempotent while the catalog
record is retained. Retrieval of a completed value is likewise repeatable
within its declared retention window; reading it does not erase the completion
record. A separate explicit release/retention operation may remove result
bytes after the client no longer needs them.

For a `PendingIO` containing chunks, the one-label operations apply to each
label ID. Existing convenience `wait`/`wait_read` behavior is defined as
`wait_all` in input order; a successful read concatenates the successful chunk
values in that order. A failed or cancelled member makes the aggregate
operation unsuccessful and identifies every member result rather than hiding
which chunk failed.

### 13.3 Cancellation boundary

Cancellation is an authenticated request addressed to a label ID and is
idempotent. The cancellation coordinator linearizes it against the lifecycle
compare-and-set:

1. Before `Executing` is recorded, cancellation is guaranteed to prevent
   external I/O if the cancellation transition wins. It produces
   `Cancelled/CANCELED` and persists that terminal record.
2. During the `Scheduled -> Executing` delivery race, cancellation is
   best-effort. The winner is observable: a successful cancellation is
   `Cancelled`; if the worker claim wins first, cancellation returns `TooLate`
   and the label follows its execution result.
3. Once `Executing` is recorded, version 1 cannot interrupt the worker or
   promise rollback. Cancellation is impossible and returns `TooLate`; the
   label completes or fails normally. A worker MUST NOT rewrite a later
   completion to `Cancelled`.

A repeated cancellation of a cancelled label returns the same cancelled
completion. A repeated cancellation of a completed or failed label returns
that existing terminal outcome and performs no transition. Concurrent
cancellation requests therefore produce one logical terminal record, not
multiple completions. Cancellation of a Composite follows the same boundary
for the Composite; its children retain their own terminal outcomes and are
not silently erased.

### 13.4 Failure taxonomy and retryability

A parked label is not a failure. It remains retryable until it is cancelled,
expires, or a permanent requirement failure is proved. Terminal errors use the
P01 category as the primary prefix in `Completion.error`; retryability is a
catalog/completion attribute and is not inferred from an arbitrary detail
string.

| Failure group | P01 categories | Automatic retry |
|---|---|---|
| Malformed or unsupported program | MALFORMED_BUFFER, LIMIT_EXCEEDED, UNSUPPORTED_IR_VERSION, INVALID_ENUM, INVALID_IDENTIFIER, UNKNOWN_REFINEMENT, UNKNOWN_RESOURCE, FORBIDDEN_INTERNAL_RESOURCE, ADDRESS_CONFLICT, AMBIGUOUS_INPUT, MISSING_FIELD, ILLEGAL_COMBINATION, INVALID_PIPELINE, INVALID_DEPENDENCY, DUPLICATE_ID | No. Correct the producer or submit a new label. |
| Authorization or unsafe input | AUTHORIZATION_FAILED, UNSAFE_MEMORY_REFERENCE, UNAUTHORIZED_MUTATION | No. Resubmission without changing the cause is not useful. |
| Permanent capability/constraint failure | BACKEND_UNSUPPORTED when no registered adapter exists, UNSATISFIABLE_REQUIREMENTS | No. A newly registered capability may justify a new submission, not blind replay. |
| Version or dependency outcome | RESOURCE_VERSION_CONFLICT, DEPENDENCY_FAILED, COMPOSITE_ABORTED | No automatic replay; the producer must choose a new version/dependency intent. |
| Explicit lifecycle outcome | CANCELED, EXPIRED, INVALID_STATE_TRANSITION | No automatic replay. Cancellation and TTL are producer-visible decisions. |
| Recoverable staged content | STAGED_CONTENT_UNAVAILABLE caused by temporary catalog/warehouse unavailability | Bounded recovery retry while the binding and retention guarantee remain valid; otherwise fail with the same category. |
| Recoverable execution/transport loss | EXECUTION_FAILED with a transient backend outage, delivery timeout, or worker loss | Only when the registered descriptor/backend declares the operation idempotent or supplies the label ID as an idempotency key. |
| Unknown execution outcome | EXECUTION_FAILED after an effect may have occurred and idempotency is not proved | No automatic replay; preserve the uncertain outcome for reconciliation. |

`BACKEND_UNSUPPORTED` is not used merely because the current worker list is
empty; that condition is parking. Descriptor and backend declarations decide
whether an execution retry is safe. At-least-once delivery never upgrades a
non-idempotent effect into exactly-once execution.

### 13.5 Admission and no-worker parking

Admission is durable before dispatch. The dispatcher MUST NOT remove a label
from its input batch merely because no worker is registered, all workers are
unavailable, or the solver produces no current feasible assignment. It returns
the label to `Queued/Parked`, persists the canonical snapshot and reason, and
keeps it eligible for recovery.

The bounded parking policy is:

- park with reason `NO_WORKERS`, `NO_AVAILABLE_WORKER`, or
  `NO_FEASIBLE_CURRENT_PLACEMENT`;
- retry with bounded exponential backoff (implementation defaults MUST have a
  finite maximum delay) and reset/wake the retry when a capable worker
  registers or becomes available;
- keep the parked record in the catalog-backed queue rather than in dispatcher
  memory, so the number of retries and queue memory are bounded without a
  loss-inducing attempt limit;
- if the label has a TTL, expire it as `EXPIRED` when its latest-start deadline
  passes; without a TTL it remains parked until execution, cancellation, or a
  separately proved permanent infeasibility;
- preserve the original admission and queued timestamps across requeues.

Parked labels are observable through both `test` and the catalog's observation
surface. The catalog record contains at least `parked`, `park_reason`,
`park_attempts`, `parked_since`, `next_retry_at`, and the last transition time.
The queue observation reports parked count, oldest parked label, counts by
reason, retry rate, and the next retry time. Per-label status queries expose
those fields subject to authorization. A worker registration wake-up and a
periodic recovery scan are both allowed to make a parked label runnable; they
must use the same catalog compare-and-set and may not dispatch it twice
without at-least-once deduplication.

### 13.6 Delivery, deduplication, and recovery

The transport contract required by this section is **at least once** for each
admitted label and each terminal completion notification:

1. The admission coordinator persists the canonical label snapshot, input
   binding, and initial catalog state before acknowledging admission.
2. A dispatcher-to-worker delivery is acknowledged only after the durable
   queue/lease record exists. A dispatcher or worker may redeliver after a
   lost acknowledgement or expired lease.
3. A worker records its claim/attempt before external I/O and persists the
   terminal result before acknowledging the delivery.
4. The completion publisher may repeat a terminal notification until the
   transport or catalog query path confirms delivery. Clients treat duplicate
   completions for one label ID as the same completion.

Workers MUST deduplicate by stable label ID and operation identity, not by
transport message identity. A duplicate before execution returns the persisted
result or resumes the existing claim. A duplicate after terminalization
replays the persisted completion and performs no backend effect. Aggregated
labels retain their original IDs and require per-original completion
idempotency; the synthetic aggregate ID is not a substitute for those IDs.

The worker/backend boundary MUST use a label-derived idempotency key, a native
conditional operation, or a descriptor declaration proving that replay is
safe. For a non-idempotent operation whose prior effect is uncertain after a
crash, the worker records `EXECUTION_FAILED` with an uncertain-outcome detail
and does not blindly retry. This is an at-least-once contract, not an
exactly-once distributed transaction.

The existing DragonflyDB catalog is the completion authority; no new storage
system is required. The catalog implementation stores, under the existing
`labios:catalog:{label_id}` record (or an equivalent catalog-owned key):

- canonical lifecycle state, transition version, timestamps, lease/attempt,
  worker, and park fields;
- the terminal state, primary category, detail, completion time, and serialized
  completion metadata/result handle;
- delivery and deduplication markers sufficient to reject stale transitions
  and replay a completion.

A catalog-owned durable label snapshot keyed by label ID stores the admitted
canonical label and runtime residual needed for recovery. The existing
ContentManager/catalog binding for staged input and completed retrieval data
is retained with the label's completion guarantee. Physical warehouse
locations may move, but the stable binding cannot identify different bytes.

Therefore:

- **Dispatcher restart:** an admitted label whose dispatcher stopped after
  admission but before scheduling remains `Admitted` or `Queued/Parked` in the
  catalog. Recovery scans those records and re-enqueues them. A label that was
  `Scheduled` is reclaimed after its lease expires; a persisted terminal
  record wins over any stale delivery.
- **Worker restart:** an unacknowledged delivery is redelivered. A persisted
  terminal record is replayed without re-execution. If execution was recorded
  but its external effect is uncertain, the worker-loss rule and idempotency
  declaration determine whether it is safely retried or terminalized as
  uncertain `EXECUTION_FAILED`.
- **Client restart:** the local pending handle is disposable. A client that
  retained the label ID reconstructs a handle by querying the catalog and can
  use `test`, `wait`, `wait_any`, or `wait_all`; completion does not depend on
  the original NATS inbox subscription. A lost client process does not cancel
  the label.
- **Catalog loss:** this contract does not promise recovery after loss or
  corruption of the catalog itself. Dragonfly persistence/replication and
  backup policy are deployment guarantees outside the Label I/O completion
  semantics.

Completion and deduplication records remain available for the configured
completion-retention window; the deployment MUST expose that window and MUST
not delete a record before it expires. Durable labels and any durable result
binding remain until explicit release or the stronger backend/deployment
retention rule. After retention expires, a client may receive
`COMPLETION_EXPIRED` as a lookup result rather than a fabricated label
failure; that lookup condition is outside the label's historical terminal
state.

This section is the completion contract consumed by P06 and P07. P06 adds
client operations over these persisted records. P07 selects acknowledged,
durable transport machinery and recovery scans that satisfy these guarantees;
it must not redefine the states, timeout meaning, cancellation boundary, or
no-worker outcome.
