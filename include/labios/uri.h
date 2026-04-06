#pragma once
#include <string>
#include <string_view>

namespace labios {

struct URI {
    std::string scheme;    // "file", "s3", "vector", "graph", "kv", "memory"
    std::string authority; // host:port or empty
    std::string path;      // /data/output.dat
    std::string query;     // key=value pairs or empty

    bool empty() const { return scheme.empty() && path.empty(); }
};

/// Parse a URI string. Bare paths default to file:// scheme.
URI parse_uri(std::string_view raw);

/// Reconstruct a URI string from components.
std::string to_string(const URI& uri);

} // namespace labios
