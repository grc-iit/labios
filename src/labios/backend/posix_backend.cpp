#include <labios/backend/posix_backend.h>

#include <fstream>

namespace labios {

PosixBackend::PosixBackend(std::filesystem::path storage_root)
    : root_(std::move(storage_root)) {}

BackendResult PosixBackend::put(std::string_view path, uint64_t offset,
                                std::span<const std::byte> data) {
    auto full_path = root_ / std::string(path);
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

BackendDataResult PosixBackend::get(std::string_view path, uint64_t offset,
                                    uint64_t length) {
    auto full_path = root_ / std::string(path);
    std::ifstream ifs(full_path, std::ios::binary);
    if (!ifs) {
        return {false, "data not found: " + full_path.string(), {}};
    }
    if (offset > 0) {
        ifs.seekg(static_cast<std::streamoff>(offset));
    }
    std::vector<std::byte> buf(length);
    ifs.read(reinterpret_cast<char*>(buf.data()),
             static_cast<std::streamsize>(length));
    buf.resize(static_cast<size_t>(ifs.gcount()));
    return {true, {}, std::move(buf)};
}

BackendResult PosixBackend::del(std::string_view path) {
    auto full_path = root_ / std::string(path);
    std::error_code ec;
    std::filesystem::remove(full_path, ec);
    if (ec) {
        return {false, ec.message()};
    }
    return {};
}

} // namespace labios
