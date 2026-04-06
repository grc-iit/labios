#pragma once

#include <labios/transport/redis.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace labios {

struct WorkspaceEntry {
    std::string key;
    uint64_t version;
    uint64_t size;
    uint64_t updated_us;     // Microsecond timestamp of last write
};

/// A named, access-controlled shared data region in the warehouse.
/// Multiple agents read and write to the same workspace over time.
/// Supports per-key versioning, ACLs, TTL, and prefix-filtered listing.
class Workspace {
public:
    Workspace(std::string name, uint32_t owner_app_id,
              transport::RedisConnection& redis,
              uint32_t ttl_seconds = 0);

    const std::string& name() const { return name_; }
    uint32_t owner() const { return owner_app_id_; }

    /// Grant access to an agent/app by ID.
    void grant_access(uint32_t app_id);

    /// Revoke access. Cannot revoke owner.
    void revoke_access(uint32_t app_id);

    /// Check if an app has access.
    bool has_access(uint32_t app_id) const;

    /// Write data to a key in the workspace. Increments version.
    /// Returns the new version number.
    uint64_t put(std::string_view key, std::span<const std::byte> data,
                 uint32_t app_id);

    /// Read data from a key. Returns empty optional if key not found.
    std::optional<std::vector<std::byte>> get(std::string_view key,
                                               uint32_t app_id);

    /// Read a specific version of a key.
    std::optional<std::vector<std::byte>> get_version(std::string_view key,
                                                       uint64_t version,
                                                       uint32_t app_id);

    /// Delete a key from the workspace.
    bool del(std::string_view key, uint32_t app_id);

    /// List all keys in the workspace.
    std::vector<WorkspaceEntry> list(uint32_t app_id);

    /// List keys matching a prefix.
    std::vector<WorkspaceEntry> list(std::string_view prefix, uint32_t app_id);

    /// Destroy the workspace and all its data.
    void destroy();

    bool is_destroyed() const { return destroyed_; }

private:
    std::string name_;
    uint32_t owner_app_id_;
    transport::RedisConnection& redis_;
    uint32_t ttl_seconds_;
    bool destroyed_ = false;

    mutable std::mutex mu_;
    std::set<uint32_t> acl_;

    std::string data_key(std::string_view key) const;       // "labios:ws:<name>:<key>"
    std::string version_key(std::string_view key,
                            uint64_t version) const;         // "labios:ws:<name>:<key>:v<ver>"
    std::string meta_key(std::string_view key) const;        // "labios:ws:<name>:_meta:<key>"
    std::string index_key() const;                           // "labios:ws:<name>:_index"
    void check_access(uint32_t app_id) const;                // Throws if no access
};

/// Registry of active workspaces. Thread-safe.
class WorkspaceRegistry {
public:
    explicit WorkspaceRegistry(transport::RedisConnection& redis);

    Workspace* create(std::string_view name, uint32_t owner_app_id,
                      uint32_t ttl_seconds = 0);
    Workspace* get(std::string_view name);
    void remove(std::string_view name);
    std::vector<std::string> list() const;

private:
    transport::RedisConnection& redis_;
    mutable std::mutex mu_;
    std::unordered_map<std::string, std::unique_ptr<Workspace>> workspaces_;
};

} // namespace labios
