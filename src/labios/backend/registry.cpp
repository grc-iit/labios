#include <labios/backend/registry.h>

#include <algorithm>

namespace labios {

AnyBackend* BackendRegistry::resolve(std::string_view scheme) const {
    auto it = backends_.find(std::string(scheme));
    if (it == backends_.end()) return nullptr;
    return it->second.get();
}

bool BackendRegistry::has_scheme(std::string_view scheme) const {
    return backends_.find(std::string(scheme)) != backends_.end();
}

std::vector<std::string> BackendRegistry::schemes() const {
    std::vector<std::string> result;
    result.reserve(backends_.size());
    for (const auto& [scheme, _] : backends_) result.push_back(scheme);
    std::sort(result.begin(), result.end());
    return result;
}

} // namespace labios
