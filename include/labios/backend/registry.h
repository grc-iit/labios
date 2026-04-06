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
    virtual BackendResult put(std::string_view path, uint64_t offset,
                              std::span<const std::byte> data) = 0;
    virtual BackendDataResult get(std::string_view path, uint64_t offset,
                                  uint64_t length) = 0;
    virtual BackendResult del(std::string_view path) = 0;
};

template<BackendStore B>
class BackendWrapper : public AnyBackend {
public:
    explicit BackendWrapper(B backend) : backend_(std::move(backend)) {}
    BackendResult put(std::string_view p, uint64_t o,
                      std::span<const std::byte> d) override {
        return backend_.put(p, o, d);
    }
    BackendDataResult get(std::string_view p, uint64_t o, uint64_t l) override {
        return backend_.get(p, o, l);
    }
    BackendResult del(std::string_view p) override {
        return backend_.del(p);
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
