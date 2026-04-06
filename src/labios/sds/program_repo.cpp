#include <labios/sds/program_repo.h>

#include <algorithm>

namespace labios::sds {

// Forward declaration: builtins are registered in builtins.cpp.
void register_all_builtins(ProgramRepository& repo);

ProgramRepository::ProgramRepository() {
    register_builtins();
}

void ProgramRepository::register_builtins() {
    register_all_builtins(*this);
}

void ProgramRepository::register_function(std::string name, SdsFunction fn) {
    std::lock_guard lock(mu_);
    functions_.insert_or_assign(std::move(name), std::move(fn));
}

const SdsFunction* ProgramRepository::lookup(std::string_view name) const {
    std::lock_guard lock(mu_);
    auto it = functions_.find(std::string(name));
    if (it == functions_.end()) return nullptr;
    return &it->second;
}

bool ProgramRepository::has(std::string_view name) const {
    std::lock_guard lock(mu_);
    return functions_.contains(std::string(name));
}

std::vector<std::string> ProgramRepository::list() const {
    std::lock_guard lock(mu_);
    std::vector<std::string> names;
    names.reserve(functions_.size());
    for (auto& [k, _] : functions_) {
        names.push_back(k);
    }
    std::sort(names.begin(), names.end());
    return names;
}

} // namespace labios::sds
