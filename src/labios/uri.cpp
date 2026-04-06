#include <labios/uri.h>

namespace labios {

URI parse_uri(std::string_view raw) {
    if (raw.empty()) return {};

    URI uri;
    auto pos = raw.find("://");
    if (pos == std::string_view::npos) {
        // Bare path defaults to file scheme.
        uri.scheme = "file";
        // Split off query string if present.
        auto q = raw.find('?');
        if (q != std::string_view::npos) {
            uri.path = std::string(raw.substr(0, q));
            uri.query = std::string(raw.substr(q + 1));
        } else {
            uri.path = std::string(raw);
        }
        return uri;
    }

    uri.scheme = std::string(raw.substr(0, pos));
    auto rest = raw.substr(pos + 3); // after "://"

    // For file:// with no authority, rest starts with '/'.
    if (rest.empty()) return uri;

    if (rest.front() == '/') {
        // No authority (e.g., file:///data/output.dat).
        auto q = rest.find('?');
        if (q != std::string_view::npos) {
            uri.path = std::string(rest.substr(0, q));
            uri.query = std::string(rest.substr(q + 1));
        } else {
            uri.path = std::string(rest);
        }
    } else {
        // Authority present (e.g., s3://bucket/key).
        auto slash = rest.find('/');
        if (slash == std::string_view::npos) {
            uri.authority = std::string(rest);
        } else {
            uri.authority = std::string(rest.substr(0, slash));
            auto tail = rest.substr(slash);
            auto q = tail.find('?');
            if (q != std::string_view::npos) {
                uri.path = std::string(tail.substr(0, q));
                uri.query = std::string(tail.substr(q + 1));
            } else {
                uri.path = std::string(tail);
            }
        }
    }
    return uri;
}

std::string to_string(const URI& uri) {
    if (uri.empty()) return {};

    std::string result;
    result += uri.scheme;
    result += "://";
    result += uri.authority;
    result += uri.path;
    if (!uri.query.empty()) {
        result += '?';
        result += uri.query;
    }
    return result;
}

} // namespace labios
