#include <labios/backend/posix_backend.h>
#include <labios/uri.h>

#include <fstream>

namespace labios {

PosixBackend::PosixBackend(std::filesystem::path storage_root)
    : root_(std::move(storage_root)) {}

static std::string_view strip_leading_slash(std::string_view p) {
    if (!p.empty() && p.front() == '/') p.remove_prefix(1);
    return p;
}

std::pair<std::filesystem::path, uint64_t>
PosixBackend::resolve_dest(const LabelData& label) const {
    if (!label.dest_uri.empty()) {
        auto uri = parse_uri(label.dest_uri);
        return {root_ / std::string(strip_leading_slash(uri.path)), 0};
    }
    auto* fp = std::get_if<FilePath>(&label.destination);
    if (fp) return {root_ / fp->path, fp->offset};
    return {root_ / "unknown", 0};
}

std::pair<std::filesystem::path, uint64_t>
PosixBackend::resolve_source(const LabelData& label) const {
    if (!label.source_uri.empty()) {
        auto uri = parse_uri(label.source_uri);
        return {root_ / std::string(strip_leading_slash(uri.path)), 0};
    }
    auto* fp = std::get_if<FilePath>(&label.source);
    if (fp) return {root_ / fp->path, fp->offset};
    return {root_ / "unknown", 0};
}

BackendResult PosixBackend::put(const LabelData& label,
                                std::span<const std::byte> data) {
    auto [full_path, offset] = resolve_dest(label);
    std::filesystem::create_directories(full_path.parent_path());

    if (offset > 0 && std::filesystem::exists(full_path)) {
        std::ofstream ofs(full_path,
            std::ios::binary | std::ios::in | std::ios::out);
        if (!ofs) {
            return {false, "failed to open " + full_path.string()};
        }
        ofs.seekp(static_cast<std::streamoff>(offset));
        ofs.write(reinterpret_cast<const char*>(data.data()),
                  static_cast<std::streamsize>(data.size()));
    } else {
        std::ofstream ofs(full_path, std::ios::binary | std::ios::out);
        if (!ofs) {
            return {false, "failed to open " + full_path.string()};
        }
        if (offset > 0) {
            ofs.seekp(static_cast<std::streamoff>(offset));
        }
        ofs.write(reinterpret_cast<const char*>(data.data()),
                  static_cast<std::streamsize>(data.size()));
    }
    return {};
}

BackendDataResult PosixBackend::get(const LabelData& label) {
    auto [full_path, offset] = resolve_source(label);
    std::ifstream ifs(full_path, std::ios::binary);
    if (!ifs) {
        return {false, "data not found: " + full_path.string(), {}};
    }
    if (offset > 0) {
        ifs.seekg(static_cast<std::streamoff>(offset));
    }

    uint64_t length = label.data_size;
    if (length == 0) {
        // Read entire file if no size specified.
        ifs.seekg(0, std::ios::end);
        auto end = ifs.tellg();
        ifs.seekg(static_cast<std::streamoff>(offset));
        length = static_cast<uint64_t>(end) - offset;
    }

    std::vector<std::byte> buf(length);
    ifs.read(reinterpret_cast<char*>(buf.data()),
             static_cast<std::streamsize>(length));
    buf.resize(static_cast<size_t>(ifs.gcount()));
    return {true, {}, std::move(buf)};
}

BackendResult PosixBackend::del(const LabelData& label) {
    auto [full_path, offset] = resolve_dest(label);
    std::error_code ec;
    std::filesystem::remove(full_path, ec);
    if (ec) {
        return {false, ec.message()};
    }
    return {};
}

BackendQueryResult PosixBackend::query(const LabelData& /*label*/) {
    return {false, "query not supported on file backend", {}};
}

} // namespace labios
