#pragma once
#include <labios/backend/backend.h>
#include <memory>
#include <string>

struct sqlite3;

namespace labios {

/// SQLite backend for structured agent memory (sqlite:// scheme).
/// Stores key-value data with metadata columns (intent, isolation, priority, ttl).
/// Supports query() for SQL-style filtering on metadata.
class SQLiteBackend {
public:
    explicit SQLiteBackend(const std::string& db_path);
    ~SQLiteBackend();

    SQLiteBackend(SQLiteBackend&&) noexcept;
    SQLiteBackend& operator=(SQLiteBackend&&) noexcept;

    BackendResult put(const LabelData& label, std::span<const std::byte> data);
    BackendDataResult get(const LabelData& label);
    BackendResult del(const LabelData& label);
    BackendQueryResult query(const LabelData& label);
    std::string_view scheme() const { return "sqlite"; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::string extract_key(const LabelData& label) const;
};

static_assert(BackendStore<SQLiteBackend>);

} // namespace labios
