#pragma once
#include <labios/backend/backend.h>
#include <labios/uri.h>

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace labios {

/// Type-erased wrapper around any BackendStore.
class AnyBackend {
public:
    virtual ~AnyBackend() = default;
    virtual BackendResult put(const LabelData& label,
                              std::span<const std::byte> data) = 0;
    virtual BackendDataResult get(const LabelData& label) = 0;
    virtual BackendResult del(const LabelData& label) = 0;
    virtual BackendQueryResult query(const LabelData& label) = 0;
};

template<BackendStore B>
class BackendWrapper : public AnyBackend {
public:
    explicit BackendWrapper(B backend) : backend_(std::move(backend)) {}
    BackendResult put(const LabelData& label,
                      std::span<const std::byte> data) override {
        return backend_.put(label, data);
    }
    BackendDataResult get(const LabelData& label) override {
        return backend_.get(label);
    }
    BackendResult del(const LabelData& label) override {
        return backend_.del(label);
    }
    BackendQueryResult query(const LabelData& label) override {
        return backend_.query(label);
    }
private:
    B backend_;
};

/// Registry mapping URI schemes to backend implementations.
class BackendRegistry {
public:
    template<BackendStore B>
    void register_backend(B backend) {
        auto scheme = std::string(backend.scheme());
        backends_[scheme] = std::make_unique<BackendWrapper<B>>(std::move(backend));
    }

    AnyBackend* resolve(std::string_view scheme) const;
    bool has_scheme(std::string_view scheme) const;

private:
    std::unordered_map<std::string, std::unique_ptr<AnyBackend>> backends_;
};

} // namespace labios
