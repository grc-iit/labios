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

std::vector<std::byte> serialize_label(const LabelData& label) {
    flatbuffers::FlatBufferBuilder fbb(1024);

    // Create all strings and nested objects before building the Label table.
    auto operation_off  = fbb.CreateString(label.operation);
    auto reply_to_off   = fbb.CreateString(label.reply_to);
    auto file_key_off   = fbb.CreateString(label.file_key);
    auto source_uri_off    = fbb.CreateString(label.source_uri);
    auto dest_uri_off      = fbb.CreateString(label.dest_uri);
    auto pipeline_data_off = fbb.CreateString(
        sds::serialize_pipeline(label.pipeline));

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
        auto tc_off = fbb.CreateString(label.continuation.target_channel);
        auto cp_off = fbb.CreateString(label.continuation.chain_params);
        auto cd_off = fbb.CreateString(label.continuation.condition);
        cont_off = schema::CreateContinuation(
            fbb,
            static_cast<schema::ContinuationKind>(label.continuation.kind),
            tc_off, cp_off, cd_off);
    }

    // RoutingDecision sub-table.
    flatbuffers::Offset<schema::RoutingDecision> routing_off = 0;
    if (label.routing.worker_id != 0 || !label.routing.policy.empty()) {
        auto policy_off = fbb.CreateString(label.routing.policy);
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

    // Build the Label table using the builder pattern.
    schema::LabelBuilder builder(fbb);
    builder.add_id(label.id);
    builder.add_type(static_cast<schema::LabelType>(label.type));
    builder.add_source(src_off);
    builder.add_destination(dst_off);
    builder.add_operation(operation_off);
    builder.add_flags(label.flags);
    builder.add_priority(label.priority);
    builder.add_app_id(label.app_id);
    builder.add_data_size(label.data_size);
    builder.add_intent(static_cast<schema::Intent>(label.intent));
    builder.add_ttl_seconds(label.ttl_seconds);
    builder.add_isolation(static_cast<schema::Isolation>(label.isolation));
    builder.add_reply_to(reply_to_off);
    builder.add_file_key(file_key_off);
    builder.add_version(label.version);
    builder.add_durability(static_cast<schema::Durability>(label.durability));
    if (cont_off.o != 0) builder.add_continuation(cont_off);
    builder.add_source_uri(source_uri_off);
    builder.add_dest_uri(dest_uri_off);
    builder.add_pipeline_data(pipeline_data_off);
    if (deps_off.o != 0) builder.add_dependencies(deps_off);
    if (children_off.o != 0) builder.add_children(children_off);
    if (routing_off.o != 0) builder.add_routing(routing_off);
    if (hops_off.o != 0) builder.add_hops(hops_off);
    builder.add_status(static_cast<schema::StatusCode>(label.status));
    builder.add_created_us(label.created_us);
    builder.add_completed_us(label.completed_us);

    auto label_off = builder.Finish();
    schema::FinishLabelBuffer(fbb, label_off);

    auto* ptr = fbb.GetBufferPointer();
    auto  sz  = fbb.GetSize();
    return {reinterpret_cast<const std::byte*>(ptr),
            reinterpret_cast<const std::byte*>(ptr) + sz};
}

LabelData deserialize_label(std::span<const std::byte> buf) {
    auto fb = schema::GetLabel(buf.data());

    LabelData out;
    out.id          = fb->id();
    out.type        = static_cast<LabelType>(fb->type());
    out.source      = deserialize_pointer(fb->source());
    out.destination = deserialize_pointer(fb->destination());
    out.operation   = fb->operation() ? fb->operation()->str() : std::string{};
    out.flags       = fb->flags();
    out.priority    = fb->priority();
    out.app_id      = fb->app_id();
    out.data_size   = fb->data_size();
    out.intent      = static_cast<Intent>(fb->intent());
    out.ttl_seconds = fb->ttl_seconds();
    out.isolation   = static_cast<Isolation>(fb->isolation());
    out.reply_to    = fb->reply_to() ? fb->reply_to()->str() : std::string{};
    out.file_key    = fb->file_key() ? fb->file_key()->str() : std::string{};

    out.version     = fb->version();
    out.durability  = static_cast<Durability>(fb->durability());
    out.source_uri  = fb->source_uri() ? fb->source_uri()->str() : std::string{};
    out.dest_uri    = fb->dest_uri() ? fb->dest_uri()->str() : std::string{};
    if (fb->pipeline_data() && fb->pipeline_data()->size() > 0)
        out.pipeline = sds::deserialize_pipeline(fb->pipeline_data()->string_view());

    // Continuation
    if (auto* cont = fb->continuation()) {
        out.continuation.kind = static_cast<ContinuationKind>(cont->kind());
        out.continuation.target_channel =
            cont->target_channel() ? cont->target_channel()->str() : std::string{};
        out.continuation.chain_params =
            cont->chain_params() ? cont->chain_params()->str() : std::string{};
        out.continuation.condition =
            cont->condition() ? cont->condition()->str() : std::string{};
    }

    // Dependencies
    if (fb->dependencies()) {
        for (auto* dep : *fb->dependencies()) {
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
        out.routing.policy = rt->policy() ? rt->policy()->str() : std::string{};
    }

    // Hops
    if (fb->hops()) {
        for (auto* hop : *fb->hops()) {
            out.hops.push_back({
                hop->component() ? hop->component()->str() : std::string{},
                hop->timestamp_us()
            });
        }
    }

    // State
    out.status       = static_cast<StatusCode>(fb->status());
    out.created_us   = fb->created_us();
    out.completed_us = fb->completed_us();

    return out;
}

// ---------------------------------------------------------------------------
// Completion serialization
// ---------------------------------------------------------------------------

std::vector<std::byte> serialize_completion(const CompletionData& comp) {
    flatbuffers::FlatBufferBuilder fbb(256);

    // Create strings before building the table.
    auto error_off    = fbb.CreateString(comp.error);
    auto data_key_off = fbb.CreateString(comp.data_key);

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
    out.error    = fb->error() ? fb->error()->str() : std::string{};
    out.data_key = fb->data_key() ? fb->data_key()->str() : std::string{};
    return out;
}

} // namespace labios
