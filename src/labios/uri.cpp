#include <labios/uri.h>

namespace labios {

URI parse_uri(std::string_view raw) {
    if (raw.empty()) return {};

    URI uri;
    auto pos = raw.find("://");
    if (pos == std::string_view::npos) {
        // Bare path defaults to file scheme; avoid allocation for known literal.
        uri.scheme = "file";
        auto q = raw.find('?');
        if (q != std::string_view::npos) {
            uri.path.assign(raw.data(), q);
            uri.query.assign(raw.data() + q + 1, raw.size() - q - 1);
        } else {
            uri.path.assign(raw.data(), raw.size());
        }
        return uri;
    }

    uri.scheme.assign(raw.data(), pos);
    auto rest = raw.substr(pos + 3);

    if (rest.empty()) return uri;

    if (rest.front() == '/') {
        auto q = rest.find('?');
        if (q != std::string_view::npos) {
            uri.path.assign(rest.data(), q);
            uri.query.assign(rest.data() + q + 1, rest.size() - q - 1);
        } else {
            uri.path.assign(rest.data(), rest.size());
        }
    } else {
        auto slash = rest.find('/');
        if (slash == std::string_view::npos) {
            uri.authority.assign(rest.data(), rest.size());
        } else {
            uri.authority.assign(rest.data(), slash);
            auto tail = rest.substr(slash);
            auto q = tail.find('?');
            if (q != std::string_view::npos) {
                uri.path.assign(tail.data(), q);
                uri.query.assign(tail.data() + q + 1, tail.size() - q - 1);
            } else {
                uri.path.assign(tail.data(), tail.size());
            }
        }
    }
    return uri;
}

std::string to_string(const URI& uri) {
    if (uri.empty()) return {};

    std::string result;
    result.reserve(uri.scheme.size() + 3 + uri.authority.size()
                   + uri.path.size() + 1 + uri.query.size());
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
