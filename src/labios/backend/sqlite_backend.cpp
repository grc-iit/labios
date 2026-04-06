#include <labios/backend/sqlite_backend.h>
#include <labios/uri.h>

#include <sqlite3.h>

#include <cstring>
#include <stdexcept>

namespace labios {

struct SQLiteBackend::Impl {
    sqlite3* db = nullptr;

    ~Impl() {
        if (db) sqlite3_close(db);
    }
};

SQLiteBackend::SQLiteBackend(const std::string& db_path)
    : impl_(std::make_unique<Impl>()) {
    int rc = sqlite3_open(db_path.c_str(), &impl_->db);
    if (rc != SQLITE_OK) {
        std::string err = sqlite3_errmsg(impl_->db);
        sqlite3_close(impl_->db);
        impl_->db = nullptr;
        throw std::runtime_error("sqlite open failed: " + err);
    }

    const char* create_sql =
        "CREATE TABLE IF NOT EXISTS labios_store ("
        "  key TEXT PRIMARY KEY,"
        "  data BLOB,"
        "  intent INTEGER DEFAULT 0,"
        "  isolation INTEGER DEFAULT 0,"
        "  priority INTEGER DEFAULT 0,"
        "  ttl_seconds INTEGER DEFAULT 0,"
        "  app_id INTEGER DEFAULT 0,"
        "  created_at INTEGER DEFAULT (strftime('%s','now')),"
        "  version INTEGER DEFAULT 1"
        ");";

    char* errmsg = nullptr;
    rc = sqlite3_exec(impl_->db, create_sql, nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        std::string err = errmsg ? errmsg : "unknown error";
        sqlite3_free(errmsg);
        throw std::runtime_error("sqlite table creation failed: " + err);
    }
}

SQLiteBackend::~SQLiteBackend() = default;
SQLiteBackend::SQLiteBackend(SQLiteBackend&&) noexcept = default;
SQLiteBackend& SQLiteBackend::operator=(SQLiteBackend&&) noexcept = default;

std::string SQLiteBackend::extract_key(const LabelData& label) const {
    std::string uri = !label.dest_uri.empty() ? label.dest_uri : label.source_uri;
    if (!uri.empty()) {
        auto parsed = parse_uri(uri);
        // Strip leading slash for cleaner keys.
        std::string path = parsed.path;
        if (!path.empty() && path.front() == '/') {
            path = path.substr(1);
        }
        return path;
    }
    return std::to_string(label.id);
}

BackendResult SQLiteBackend::put(const LabelData& label,
                                  std::span<const std::byte> data) {
    auto key = extract_key(label);

    const char* sql =
        "INSERT OR REPLACE INTO labios_store"
        " (key, data, intent, isolation, priority, ttl_seconds, app_id)"
        " VALUES (?, ?, ?, ?, ?, ?, ?)";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return {false, sqlite3_errmsg(impl_->db)};
    }

    sqlite3_bind_text(stmt, 1, key.c_str(), static_cast<int>(key.size()), SQLITE_TRANSIENT);
    sqlite3_bind_blob(stmt, 2, data.data(), static_cast<int>(data.size()), SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, static_cast<int>(label.intent));
    sqlite3_bind_int(stmt, 4, static_cast<int>(label.isolation));
    sqlite3_bind_int(stmt, 5, label.priority);
    sqlite3_bind_int(stmt, 6, static_cast<int>(label.ttl_seconds));
    sqlite3_bind_int(stmt, 7, static_cast<int>(label.app_id));

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return {false, sqlite3_errmsg(impl_->db)};
    }
    return {};
}

BackendDataResult SQLiteBackend::get(const LabelData& label) {
    auto key = extract_key(label);

    const char* sql = "SELECT data FROM labios_store WHERE key = ?";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return {false, sqlite3_errmsg(impl_->db), {}};
    }

    sqlite3_bind_text(stmt, 1, key.c_str(), static_cast<int>(key.size()), SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        int size = sqlite3_column_bytes(stmt, 0);
        std::vector<std::byte> result(static_cast<size_t>(size));
        if (size > 0) {
            auto* blob = static_cast<const std::byte*>(sqlite3_column_blob(stmt, 0));
            if (blob == nullptr) {
                sqlite3_finalize(stmt);
                return {false, "sqlite returned null blob pointer", {}};
            }
            std::memcpy(result.data(), blob, static_cast<size_t>(size));
        }
        sqlite3_finalize(stmt);
        return {true, {}, std::move(result)};
    }

    sqlite3_finalize(stmt);
    return {false, "key not found: " + key, {}};
}

BackendResult SQLiteBackend::del(const LabelData& label) {
    auto key = extract_key(label);

    const char* sql = "DELETE FROM labios_store WHERE key = ?";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return {false, sqlite3_errmsg(impl_->db)};
    }

    sqlite3_bind_text(stmt, 1, key.c_str(), static_cast<int>(key.size()), SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        return {false, sqlite3_errmsg(impl_->db)};
    }
    return {};
}

BackendQueryResult SQLiteBackend::query(const LabelData& label) {
    // Parse query params from source_uri for filtering.
    std::string uri = !label.source_uri.empty() ? label.source_uri : label.dest_uri;
    auto parsed = parse_uri(uri);

    std::string sql = "SELECT key, intent, isolation, priority, length(data) as size"
                      " FROM labios_store WHERE 1=1";
    std::vector<std::pair<int, std::string>> bindings;

    // Parse query string for filters: intent=N&isolation=N&priority=N
    if (!parsed.query.empty()) {
        std::string_view q = parsed.query;
        while (!q.empty()) {
            auto amp = q.find('&');
            auto pair = q.substr(0, amp);
            auto eq = pair.find('=');
            if (eq != std::string_view::npos) {
                auto key = pair.substr(0, eq);
                auto val = pair.substr(eq + 1);
                if (key == "intent") {
                    sql += " AND intent = ?";
                    bindings.emplace_back(bindings.size() + 1, std::string(val));
                } else if (key == "isolation") {
                    sql += " AND isolation = ?";
                    bindings.emplace_back(bindings.size() + 1, std::string(val));
                } else if (key == "priority") {
                    sql += " AND priority = ?";
                    bindings.emplace_back(bindings.size() + 1, std::string(val));
                }
            }
            if (amp == std::string_view::npos) break;
            q = q.substr(amp + 1);
        }
    }

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return {false, sqlite3_errmsg(impl_->db), {}};
    }

    for (auto& [idx, val] : bindings) {
        sqlite3_bind_text(stmt, idx, val.c_str(), static_cast<int>(val.size()), SQLITE_TRANSIENT);
    }

    std::string json = "{\"entries\":[";
    bool first = true;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (!first) json += ",";
        first = false;
        auto* k = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        int intent = sqlite3_column_int(stmt, 1);
        int isolation = sqlite3_column_int(stmt, 2);
        int priority = sqlite3_column_int(stmt, 3);
        int size = sqlite3_column_int(stmt, 4);
        json += "{\"key\":\"";
        json += k ? k : "";
        json += "\",\"intent\":";
        json += std::to_string(intent);
        json += ",\"isolation\":";
        json += std::to_string(isolation);
        json += ",\"priority\":";
        json += std::to_string(priority);
        json += ",\"size\":";
        json += std::to_string(size);
        json += "}";
    }
    json += "]}";

    sqlite3_finalize(stmt);
    return {true, {}, json};
}

} // namespace labios
