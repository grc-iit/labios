#include <labios/backend/posix_backend.h>
#include <labios/uri.h>

#include <fstream>
#include <optional>

namespace labios {

PosixBackend::PosixBackend(std::filesystem::path storage_root)
    : root_(std::move(storage_root)) {}

static std::string_view strip_leading_slash(std::string_view p) {
    if (!p.empty() && p.front() == '/') p.remove_prefix(1);
    return p;
}

std::optional<std::filesystem::path> sanitize_relative_path(
    std::filesystem::path raw_path) {
    if (raw_path.empty()) {
        return std::nullopt;
    }

    if (raw_path.is_absolute()) {
        raw_path = raw_path.relative_path();
    }

    auto normalized = raw_path.lexically_normal();
    for (const auto& part : normalized) {
        if (part == "..") {
            return std::nullopt;
        }
    }

    return normalized;
}

std::optional<std::pair<std::filesystem::path, uint64_t>>
PosixBackend::resolve_dest(const LabelData& label) const {
    if (!label.dest_uri.empty()) {
        auto uri = parse_uri(label.dest_uri);
        auto relative = sanitize_relative_path(
            std::filesystem::path(std::string(strip_leading_slash(uri.path))));
        if (!relative.has_value()) {
            return std::nullopt;
        }
        return std::make_pair((root_ / *relative).lexically_normal(), uint64_t{0});
    }
    auto* fp = std::get_if<FilePath>(&label.destination);
    if (!fp) {
        return std::nullopt;
    }

    auto relative = sanitize_relative_path(std::filesystem::path(fp->path));
    if (!relative.has_value()) {
        return std::nullopt;
    }
    return std::make_pair((root_ / *relative).lexically_normal(), fp->offset);
}

std::optional<std::pair<std::filesystem::path, uint64_t>>
PosixBackend::resolve_source(const LabelData& label) const {
    if (!label.source_uri.empty()) {
        auto uri = parse_uri(label.source_uri);
        auto relative = sanitize_relative_path(
            std::filesystem::path(std::string(strip_leading_slash(uri.path))));
        if (!relative.has_value()) {
            return std::nullopt;
        }
        return std::make_pair((root_ / *relative).lexically_normal(), uint64_t{0});
    }
    auto* fp = std::get_if<FilePath>(&label.source);
    if (!fp) {
        return std::nullopt;
    }

    auto relative = sanitize_relative_path(std::filesystem::path(fp->path));
    if (!relative.has_value()) {
        return std::nullopt;
    }
    return std::make_pair((root_ / *relative).lexically_normal(), fp->offset);
}

BackendResult PosixBackend::put(const LabelData& label,
                                std::span<const std::byte> data) {
    auto resolved = resolve_dest(label);
    if (!resolved.has_value()) {
        return {false, "invalid destination path"};
    }
    auto [full_path, offset] = *resolved;
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
        if (!ofs) {
            return {false, "failed to write " + full_path.string()};
        }
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
        if (!ofs) {
            return {false, "failed to write " + full_path.string()};
        }
    }
    return {};
}

BackendDataResult PosixBackend::get(const LabelData& label) {
    auto resolved = resolve_source(label);
    if (!resolved.has_value()) {
        return {false, "invalid source path", {}};
    }
    auto [full_path, offset] = *resolved;
    std::ifstream ifs(full_path, std::ios::binary);
    if (!ifs) {
        return {false, "data not found: " + full_path.string(), {}};
    }
    if (offset > 0) {
        ifs.seekg(static_cast<std::streamoff>(offset));
        if (!ifs) {
            return {false, "failed to seek " + full_path.string(), {}};
        }
    }

    uint64_t length = label.data_size;
    if (length == 0) {
        // Read entire file if no size specified.
        ifs.seekg(0, std::ios::end);
        auto end = ifs.tellg();
        if (end < 0) {
            return {false, "failed to read size for " + full_path.string(), {}};
        }
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
    auto resolved = resolve_dest(label);
    if (!resolved.has_value()) {
        return {false, "invalid destination path"};
    }
    auto [full_path, offset] = *resolved;
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
