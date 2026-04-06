#pragma once
#include <labios/backend/backend.h>
#include <filesystem>
#include <string_view>
#include <utility>

namespace labios {

class PosixBackend {
public:
    explicit PosixBackend(std::filesystem::path storage_root);

    BackendResult put(const LabelData& label, std::span<const std::byte> data);
    BackendDataResult get(const LabelData& label);
    BackendResult del(const LabelData& label);
    BackendQueryResult query(const LabelData& label);
    std::string_view scheme() const { return "file"; }

private:
    std::filesystem::path root_;
    std::pair<std::filesystem::path, uint64_t> resolve_dest(const LabelData& label) const;
    std::pair<std::filesystem::path, uint64_t> resolve_source(const LabelData& label) const;
};

static_assert(BackendStore<PosixBackend>);

} // namespace labios
