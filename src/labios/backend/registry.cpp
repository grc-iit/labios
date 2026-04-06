#include <labios/backend/registry.h>

namespace labios {

AnyBackend* BackendRegistry::resolve(std::string_view scheme) const {
    auto it = backends_.find(std::string(scheme));
    if (it == backends_.end()) return nullptr;
    return it->second.get();
}

bool BackendRegistry::has_scheme(std::string_view scheme) const {
    return backends_.find(std::string(scheme)) != backends_.end();
}

} // namespace labios
