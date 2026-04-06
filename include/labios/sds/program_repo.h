#pragma once
#include <labios/sds/types.h>

#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace labios::sds {

/// Shared program repository. Workers look up SDS functions by name.
/// Builtins are registered at startup. User functions can be added at runtime.
class ProgramRepository {
public:
    ProgramRepository();

    /// Register a function by name. Overwrites if name already exists.
    void register_function(std::string name, SdsFunction fn);

    /// Look up a function. Returns nullptr if not found.
    const SdsFunction* lookup(std::string_view name) const;

    /// Check if a function is registered.
    bool has(std::string_view name) const;

    /// List all registered function names.
    std::vector<std::string> list() const;

private:
    mutable std::mutex mu_;
    std::unordered_map<std::string, SdsFunction> functions_;

    void register_builtins();
};

} // namespace labios::sds
