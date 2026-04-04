#include <labios/label.h>

#include <flatbuffers/flatbuffers.h>
#include <label_generated.h>

#include <atomic>
#include <chrono>

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

uint64_t generate_label_id(uint32_t app_id) {
    static std::atomic<uint32_t> counter{0};
    auto now = std::chrono::high_resolution_clock::now();
    auto nanos = static_cast<uint64_t>(
        now.time_since_epoch().count());
    uint32_t seq = counter.fetch_add(1, std::memory_order_relaxed);
    // Pack: upper 32 bits from nanosecond timestamp, lower 32 bits combine
    // app_id (upper 16 bits) and atomic counter (lower 16 bits).
    return (nanos & 0xFFFFFFFF00000000ULL)
         | (static_cast<uint64_t>(app_id & 0xFFFF) << 16)
         | (seq & 0xFFFF);
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
    flatbuffers::FlatBufferBuilder fbb(512);

    // Create all strings and vectors before building tables.
    auto operation_off = fbb.CreateString(label.operation);
    auto reply_to_off  = fbb.CreateString(label.reply_to);
    flatbuffers::Offset<flatbuffers::Vector<uint64_t>> deps_off = 0;
    if (!label.dependencies.empty()) {
        deps_off = fbb.CreateVector(label.dependencies);
    }

    // Build pointer sub-tables.
    PointerSerializer ser{fbb};
    auto src_off = std::visit(ser, label.source);
    auto dst_off = std::visit(ser, label.destination);

    // Build the label table.
    auto label_off = schema::CreateLabel(
        fbb,
        label.id,
        static_cast<schema::LabelType>(label.type),
        src_off,
        dst_off,
        operation_off,
        label.flags,
        label.priority,
        label.app_id,
        deps_off,
        label.data_size,
        static_cast<schema::Intent>(label.intent),
        label.ttl_seconds,
        static_cast<schema::Isolation>(label.isolation),
        reply_to_off);

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

    if (fb->dependencies()) {
        auto* deps = fb->dependencies();
        out.dependencies.assign(deps->begin(), deps->end());
    }

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
