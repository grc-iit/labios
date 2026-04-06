#include <labios/label.h>

#include <flatbuffers/flatbuffers.h>
#include <label_generated.h>

#include <atomic>
#include <chrono>
#include <random>
#include <thread>

namespace labios {

// ---------------------------------------------------------------------------
// Factory functions
// ---------------------------------------------------------------------------

Pointer memory_ptr(const void* addr, uint64_t size) {
    return MemoryPtr{reinterpret_cast<uint64_t>(addr), size};
}

Pointer file_path(std::string_view path) {
    return FilePath{std::string(path), 0, 0};
}

Pointer file_path(std::string_view path, uint64_t offset, uint64_t length) {
    return FilePath{std::string(path), offset, length};
}

Pointer network_endpoint(std::string_view host, uint16_t port) {
    return NetworkEndpoint{std::string(host), port};
}

// ---------------------------------------------------------------------------
// ID generation
// ---------------------------------------------------------------------------

// Snowflake-style 64-bit ID generator.
//   Bits 63-22: millisecond timestamp (41 bits, ~69 years from epoch)
//   Bits 21-12: node/process ID (10 bits, 1024 unique sources)
//   Bits 11-0:  sequence counter (12 bits, 4096 per ms per node)
//
// The node ID combines app_id with a process-lifetime random component
// to disambiguate across machines sharing the same PID.

namespace {

struct SnowflakeState {
    std::atomic<uint64_t> last_ms{0};
    std::atomic<uint32_t> sequence{0};
    uint16_t node_id = 0;

    explicit SnowflakeState(uint32_t app_id) {
        std::random_device rd;
        uint32_t rand_bits = rd();
        node_id = static_cast<uint16_t>((app_id ^ rand_bits) & 0x3FF);
    }
};

} // namespace

uint64_t generate_label_id(uint32_t app_id) {
    // Use a process-global state for consistency across threads.
    static SnowflakeState state(app_id);

    auto now_ms = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());

    uint64_t prev_ms = state.last_ms.load(std::memory_order_acquire);
    uint32_t seq;

    for (;;) {
        if (now_ms > prev_ms) {
            // New millisecond: reset counter and claim this timestamp.
            if (state.last_ms.compare_exchange_strong(
                    prev_ms, now_ms, std::memory_order_acq_rel)) {
                state.sequence.store(1, std::memory_order_relaxed);
                seq = 0;
                break;
            }
            // CAS failed; prev_ms reloaded, retry.
            now_ms = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count());
            continue;
        }

        // Same millisecond: increment sequence.
        seq = state.sequence.fetch_add(1, std::memory_order_relaxed);
        if (seq < 4096) {
            now_ms = prev_ms; // Use the timestamp from last_ms for consistency.
            break;
        }

        // Sequence overflow: spin-wait for the next millisecond.
        std::this_thread::yield();
        now_ms = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count());
        prev_ms = state.last_ms.load(std::memory_order_acquire);
    }

    return ((now_ms & 0x1FFFFFFFFFFULL) << 22)
         | (static_cast<uint64_t>(state.node_id & 0x3FF) << 12)
         | (static_cast<uint64_t>(seq & 0xFFF));
}

uint64_t label_timestamp_now_us() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

namespace {

uint64_t normalize_timestamp(uint64_t timestamp_us) {
    return timestamp_us != 0 ? timestamp_us : label_timestamp_now_us();
}

} // namespace

void append_label_hop(LabelData& label, std::string_view component,
                      uint64_t timestamp_us) {
    if (component.empty()) return;
    label.hops.push_back({std::string(component), normalize_timestamp(timestamp_us)});
}

void mark_label_created(LabelData& label, uint64_t timestamp_us) {
    auto ts = normalize_timestamp(timestamp_us);
    if (label.created_us == 0) {
        label.created_us = ts;
    }
    label.status = StatusCode::Created;
}

void mark_label_queued(LabelData& label, uint64_t timestamp_us) {
    auto ts = normalize_timestamp(timestamp_us);
    if (label.created_us == 0) {
        label.created_us = ts;
    }
    label.flags |= LabelFlags::Queued;
    label.queued_us = ts;
    label.status = StatusCode::Queued;
}

void mark_label_shuffled(LabelData& label, uint64_t timestamp_us) {
    auto ts = normalize_timestamp(timestamp_us);
    if (label.created_us == 0) {
        label.created_us = ts;
    }
    label.status = StatusCode::Shuffled;
    append_label_hop(label, "shuffler", ts);
}

void mark_label_scheduled(LabelData& label, uint32_t worker_id,
                          std::string_view policy,
                          const ScoreSnapshot& snapshot,
                          uint64_t timestamp_us) {
    auto ts = normalize_timestamp(timestamp_us);
    if (label.created_us == 0) {
        label.created_us = ts;
    }
    label.flags |= LabelFlags::Scheduled;
    label.routing.worker_id = worker_id;
    label.routing.policy.assign(policy);
    label.score_snapshot = snapshot;
    label.dispatched_us = ts;
    label.status = StatusCode::Scheduled;
    append_label_hop(label, "scheduler", ts);
}

void mark_label_executing(LabelData& label, std::string_view worker_component,
                          uint64_t timestamp_us) {
    auto ts = normalize_timestamp(timestamp_us);
    if (label.created_us == 0) {
        label.created_us = ts;
    }
    label.flags |= LabelFlags::Pending;
    label.started_us = ts;
    label.status = StatusCode::Executing;
    append_label_hop(label, worker_component, ts);
}

void mark_label_finished(LabelData& label, CompletionStatus status,
                         std::string_view data_location,
                         uint64_t bytes_transferred,
                         std::string_view error,
                         uint64_t timestamp_us) {
    auto ts = normalize_timestamp(timestamp_us);
    if (label.created_us == 0) {
        label.created_us = ts;
    }
    label.completed_us = ts;
    label.result.data_location.assign(data_location.begin(), data_location.end());
    label.result.bytes_transferred = bytes_transferred;
    label.result.error.assign(error.begin(), error.end());
    label.status = (status == CompletionStatus::Complete)
        ? StatusCode::Complete
        : StatusCode::Failed;
}

// ---------------------------------------------------------------------------
// Pointer serialization helpers
// ---------------------------------------------------------------------------

namespace {

struct PointerSerializer {
    flatbuffers::FlatBufferBuilder& fbb;

    flatbuffers::Offset<schema::Pointer> operator()(std::monostate) const {
        return schema::CreatePointer(fbb);
    }

    flatbuffers::Offset<schema::Pointer> operator()(const MemoryPtr& mp) const {
        auto inner = schema::CreateMemoryPtr(fbb, mp.address, mp.size);
        return schema::CreatePointer(
            fbb, schema::PointerVariant_MemoryPtr, inner.Union());
    }

    flatbuffers::Offset<schema::Pointer> operator()(const FilePath& fp) const {
        auto path_str = fbb.CreateString(fp.path);
        auto inner = schema::CreateFilePath(fbb, path_str, fp.offset, fp.length);
        return schema::CreatePointer(
            fbb, schema::PointerVariant_FilePath, inner.Union());
    }

    flatbuffers::Offset<schema::Pointer> operator()(const NetworkEndpoint& ne) const {
        auto host_str = fbb.CreateString(ne.host);
        auto inner = schema::CreateNetworkEndpoint(fbb, host_str, ne.port);
        return schema::CreatePointer(
            fbb, schema::PointerVariant_NetworkEndpoint, inner.Union());
    }
};

Pointer deserialize_pointer(const schema::Pointer* ptr) {
    if (!ptr) return std::monostate{};

    switch (ptr->ptr_type()) {
        case schema::PointerVariant_MemoryPtr: {
            auto mp = ptr->ptr_as_MemoryPtr();
            return MemoryPtr{mp->address(), mp->size()};
        }
        case schema::PointerVariant_FilePath: {
            auto fp = ptr->ptr_as_FilePath();
            return FilePath{
                fp->path() ? fp->path()->str() : std::string{},
                fp->offset(),
                fp->length()
            };
        }
        case schema::PointerVariant_NetworkEndpoint: {
            auto ne = ptr->ptr_as_NetworkEndpoint();
            return NetworkEndpoint{
                ne->host() ? ne->host()->str() : std::string{},
                ne->port()
            };
        }
        default:
            return std::monostate{};
    }
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Label serialization
// ---------------------------------------------------------------------------

namespace {

// Helper: create a FlatBuffer string only for non-empty strings.
inline flatbuffers::Offset<flatbuffers::String>
maybe_string(flatbuffers::FlatBufferBuilder& fbb, const std::string& s) {
    if (s.empty()) return 0;
    return fbb.CreateString(s);
}

} // namespace

// Core serialization into thread-local FBB. Returns pointer and size.
static std::pair<const std::byte*, size_t> serialize_label_core(
    const LabelData& label, flatbuffers::FlatBufferBuilder& fbb) {
    fbb.Clear();
    fbb.ForceDefaults(false);

    // Only serialize non-empty strings.
    auto operation_off  = maybe_string(fbb, label.operation);
    auto reply_to_off   = maybe_string(fbb, label.reply_to);
    auto file_key_off   = maybe_string(fbb, label.file_key);
    auto source_uri_off = maybe_string(fbb, label.source_uri);
    auto dest_uri_off   = maybe_string(fbb, label.dest_uri);

    // Only serialize pipeline when non-empty.
    flatbuffers::Offset<flatbuffers::String> pipeline_data_off = 0;
    if (!label.pipeline.empty()) {
        pipeline_data_off = fbb.CreateString(
            sds::serialize_pipeline(label.pipeline));
    }

    // Dependencies vector.
    flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<schema::LabelDependency>>> deps_off = 0;
    if (!label.dependencies.empty()) {
        std::vector<flatbuffers::Offset<schema::LabelDependency>> dep_offsets;
        dep_offsets.reserve(label.dependencies.size());
        for (auto& dep : label.dependencies) {
            dep_offsets.push_back(schema::CreateLabelDependency(
                fbb, dep.label_id,
                static_cast<schema::HazardType>(dep.hazard_type)));
        }
        deps_off = fbb.CreateVector(dep_offsets);
    }

    // Children vector.
    flatbuffers::Offset<flatbuffers::Vector<uint64_t>> children_off = 0;
    if (!label.children.empty()) {
        children_off = fbb.CreateVector(label.children);
    }

    // Pointer sub-tables.
    PointerSerializer ser{fbb};
    auto src_off = std::visit(ser, label.source);
    auto dst_off = std::visit(ser, label.destination);

    // Continuation sub-table.
    flatbuffers::Offset<schema::Continuation> cont_off = 0;
    if (label.continuation.kind != ContinuationKind::None) {
        auto tc_off = maybe_string(fbb, label.continuation.target_channel);
        auto cp_off = maybe_string(fbb, label.continuation.chain_params);
        auto cd_off = maybe_string(fbb, label.continuation.condition);
        cont_off = schema::CreateContinuation(
            fbb,
            static_cast<schema::ContinuationKind>(label.continuation.kind),
            tc_off, cp_off, cd_off);
    }

    // RoutingDecision sub-table.
    flatbuffers::Offset<schema::RoutingDecision> routing_off = 0;
    if (label.routing.worker_id != 0 || !label.routing.policy.empty()) {
        auto policy_off = maybe_string(fbb, label.routing.policy);
        routing_off = schema::CreateRoutingDecision(fbb, label.routing.worker_id, policy_off);
    }

    // Hops vector.
    flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<schema::HopRecord>>> hops_off = 0;
    if (!label.hops.empty()) {
        std::vector<flatbuffers::Offset<schema::HopRecord>> hop_offsets;
        hop_offsets.reserve(label.hops.size());
        for (auto& hop : label.hops) {
            auto comp_off = fbb.CreateString(hop.component);
            hop_offsets.push_back(schema::CreateHopRecord(fbb, comp_off, hop.timestamp_us));
        }
        hops_off = fbb.CreateVector(hop_offsets);
    }

    // AggregationInfo sub-table.
    flatbuffers::Offset<schema::AggregationInfo> aggregation_off = 0;
    if (!label.aggregation.original_ids.empty()) {
        auto ids_off = fbb.CreateVector(label.aggregation.original_ids);
        aggregation_off = schema::CreateAggregationInfo(
            fbb, ids_off, label.aggregation.merged_offset, label.aggregation.merged_length);
    }

    // ScoreSnapshot sub-table.
    flatbuffers::Offset<schema::ScoreSnapshot> score_off = 0;
    if (label.score_snapshot.availability != 0.0 || label.score_snapshot.capacity != 0.0 ||
        label.score_snapshot.load != 0.0 || label.score_snapshot.speed != 0.0 ||
        label.score_snapshot.energy != 0.0 || label.score_snapshot.tier != 0.0) {
        score_off = schema::CreateScoreSnapshot(
            fbb,
            label.score_snapshot.availability, label.score_snapshot.capacity,
            label.score_snapshot.load, label.score_snapshot.speed,
            label.score_snapshot.energy, label.score_snapshot.tier);
    }

    // LabelResult sub-table.
    flatbuffers::Offset<schema::LabelResult> result_off = 0;
    if (!label.result.data_location.empty() || !label.result.error.empty() ||
        label.result.bytes_transferred != 0) {
        auto dl_off = maybe_string(fbb, label.result.data_location);
        auto err_off = maybe_string(fbb, label.result.error);
        result_off = schema::CreateLabelResult(
            fbb, dl_off, err_off, label.result.bytes_transferred);
    }

    // Build the Label table.
    schema::LabelBuilder builder(fbb);
    builder.add_id(label.id);
    builder.add_type(static_cast<schema::LabelType>(label.type));
    builder.add_source(src_off);
    builder.add_destination(dst_off);
    if (operation_off.o != 0) builder.add_operation(operation_off);
    builder.add_flags(label.flags);
    builder.add_priority(label.priority);
    builder.add_app_id(label.app_id);
    builder.add_data_size(label.data_size);
    builder.add_intent(static_cast<schema::Intent>(label.intent));
    builder.add_ttl_seconds(label.ttl_seconds);
    builder.add_isolation(static_cast<schema::Isolation>(label.isolation));
    if (reply_to_off.o != 0)  builder.add_reply_to(reply_to_off);
    if (file_key_off.o != 0)  builder.add_file_key(file_key_off);
    builder.add_version(label.version);
    builder.add_durability(static_cast<schema::Durability>(label.durability));
    if (cont_off.o != 0) builder.add_continuation(cont_off);
    if (source_uri_off.o != 0)    builder.add_source_uri(source_uri_off);
    if (dest_uri_off.o != 0)      builder.add_dest_uri(dest_uri_off);
    if (pipeline_data_off.o != 0) builder.add_pipeline_data(pipeline_data_off);
    if (deps_off.o != 0) builder.add_dependencies(deps_off);
    if (children_off.o != 0) builder.add_children(children_off);
    if (routing_off.o != 0) builder.add_routing(routing_off);
    builder.add_supertask_id(label.supertask_id);
    if (aggregation_off.o != 0) builder.add_aggregation(aggregation_off);
    if (score_off.o != 0) builder.add_score_snapshot(score_off);
    if (hops_off.o != 0) builder.add_hops(hops_off);
    builder.add_status(static_cast<schema::StatusCode>(label.status));
    builder.add_created_us(label.created_us);
    builder.add_queued_us(label.queued_us);
    builder.add_dispatched_us(label.dispatched_us);
    builder.add_started_us(label.started_us);
    builder.add_completed_us(label.completed_us);
    if (result_off.o != 0) builder.add_result(result_off);

    auto label_off = builder.Finish();
    schema::FinishLabelBuffer(fbb, label_off);

    auto* ptr = fbb.GetBufferPointer();
    auto  sz  = fbb.GetSize();
    return {reinterpret_cast<const std::byte*>(ptr), sz};
}

std::vector<std::byte> serialize_label(const LabelData& label) {
    thread_local flatbuffers::FlatBufferBuilder fbb(1024);
    auto [ptr, sz] = serialize_label_core(label, fbb);
    return {ptr, ptr + sz};
}

std::span<const std::byte> serialize_label_view(const LabelData& label) {
    thread_local flatbuffers::FlatBufferBuilder fbb(1024);
    auto [ptr, sz] = serialize_label_core(label, fbb);
    return {ptr, sz};
}

namespace {

// Helper: extract string from FlatBuffer field only when non-null and non-empty.
inline void fb_read_string(std::string& dst, const flatbuffers::String* src) {
    if (src && src->size() > 0) {
        dst.assign(src->data(), src->size());
    }
}

} // namespace

LabelData deserialize_label(std::span<const std::byte> buf) {
    auto fb = schema::GetLabel(buf.data());

    LabelData out;
    out.id          = fb->id();
    out.type        = static_cast<LabelType>(fb->type());
    out.source      = deserialize_pointer(fb->source());
    out.destination = deserialize_pointer(fb->destination());
    fb_read_string(out.operation, fb->operation());
    out.flags       = fb->flags();
    out.priority    = fb->priority();
    out.app_id      = fb->app_id();
    out.data_size   = fb->data_size();
    out.intent      = static_cast<Intent>(fb->intent());
    out.ttl_seconds = fb->ttl_seconds();
    out.isolation   = static_cast<Isolation>(fb->isolation());
    fb_read_string(out.reply_to, fb->reply_to());
    fb_read_string(out.file_key, fb->file_key());

    out.version     = fb->version();
    out.durability  = static_cast<Durability>(fb->durability());
    fb_read_string(out.source_uri, fb->source_uri());
    fb_read_string(out.dest_uri, fb->dest_uri());
    if (fb->pipeline_data() && fb->pipeline_data()->size() > 0)
        out.pipeline = sds::deserialize_pipeline(fb->pipeline_data()->string_view());

    // Continuation
    if (auto* cont = fb->continuation()) {
        out.continuation.kind = static_cast<ContinuationKind>(cont->kind());
        fb_read_string(out.continuation.target_channel, cont->target_channel());
        fb_read_string(out.continuation.chain_params, cont->chain_params());
        fb_read_string(out.continuation.condition, cont->condition());
    }

    // Dependencies
    if (auto* deps = fb->dependencies()) {
        out.dependencies.reserve(deps->size());
        for (auto* dep : *deps) {
            out.dependencies.push_back({
                dep->label_id(),
                static_cast<HazardType>(dep->hazard_type())
            });
        }
    }

    // Children
    if (fb->children()) {
        auto* ch = fb->children();
        out.children.assign(ch->begin(), ch->end());
    }

    // RoutingDecision
    if (auto* rt = fb->routing()) {
        out.routing.worker_id = rt->worker_id();
        fb_read_string(out.routing.policy, rt->policy());
    }

    // Hops
    if (auto* hops_vec = fb->hops()) {
        out.hops.reserve(hops_vec->size());
        for (auto* hop : *hops_vec) {
            HopRecord rec;
            fb_read_string(rec.component, hop->component());
            rec.timestamp_us = hop->timestamp_us();
            out.hops.push_back(std::move(rec));
        }
    }

    // Accumulation: supertask, aggregation, score snapshot
    out.supertask_id = fb->supertask_id();

    if (auto* agg = fb->aggregation()) {
        if (agg->original_ids()) {
            auto* ids = agg->original_ids();
            out.aggregation.original_ids.assign(ids->begin(), ids->end());
        }
        out.aggregation.merged_offset = agg->merged_offset();
        out.aggregation.merged_length = agg->merged_length();
    }

    if (auto* ss = fb->score_snapshot()) {
        out.score_snapshot.availability = ss->availability();
        out.score_snapshot.capacity     = ss->capacity();
        out.score_snapshot.load         = ss->load();
        out.score_snapshot.speed        = ss->speed();
        out.score_snapshot.energy       = ss->energy();
        out.score_snapshot.tier         = ss->tier();
    }

    // State
    out.status        = static_cast<StatusCode>(fb->status());
    out.created_us    = fb->created_us();
    out.queued_us     = fb->queued_us();
    out.dispatched_us = fb->dispatched_us();
    out.started_us    = fb->started_us();
    out.completed_us  = fb->completed_us();

    if (auto* res = fb->result()) {
        fb_read_string(out.result.data_location, res->data_location());
        fb_read_string(out.result.error, res->error());
        out.result.bytes_transferred = res->bytes_transferred();
    }

    return out;
}

// ---------------------------------------------------------------------------
// Completion serialization
// ---------------------------------------------------------------------------

std::vector<std::byte> serialize_completion(const CompletionData& comp) {
    thread_local flatbuffers::FlatBufferBuilder fbb(256);
    fbb.Clear();
    fbb.ForceDefaults(false);

    auto error_off    = maybe_string(fbb, comp.error);
    auto data_key_off = maybe_string(fbb, comp.data_key);

    auto comp_off = schema::CreateCompletion(
        fbb,
        comp.label_id,
        static_cast<schema::CompletionStatus>(comp.status),
        error_off,
        data_key_off);

    fbb.Finish(comp_off);

    auto* ptr = fbb.GetBufferPointer();
    auto  sz  = fbb.GetSize();
    return {reinterpret_cast<const std::byte*>(ptr),
            reinterpret_cast<const std::byte*>(ptr) + sz};
}

CompletionData deserialize_completion(std::span<const std::byte> buf) {
    auto fb = flatbuffers::GetRoot<schema::Completion>(buf.data());

    CompletionData out;
    out.label_id = fb->label_id();
    out.status   = static_cast<CompletionStatus>(fb->status());
    fb_read_string(out.error, fb->error());
    fb_read_string(out.data_key, fb->data_key());
    return out;
}

} // namespace labios
