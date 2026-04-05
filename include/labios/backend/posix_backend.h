#pragma once
#include <labios/backend/backend.h>
#include <filesystem>
#include <string_view>

namespace labios {

class PosixBackend {
public:
    explicit PosixBackend(std::filesystem::path storage_root);

    BackendResult put(std::string_view path, uint64_t offset,
                      std::span<const std::byte> data);
    BackendDataResult get(std::string_view path, uint64_t offset,
                          uint64_t length);
    BackendResult del(std::string_view path);
    std::string_view scheme() const { return "file"; }

private:
    std::filesystem::path root_;
};

static_assert(BackendStore<PosixBackend>);

} // namespace labios
