#include <labios/workspace.h>

#include <chrono>
#include <stdexcept>

namespace labios {

// ---------------------------------------------------------------------------
// Workspace
// ---------------------------------------------------------------------------

Workspace::Workspace(std::string name, uint32_t owner_app_id,
                     transport::RedisConnection& redis,
                     uint32_t ttl_seconds)
    : name_(std::move(name))
    , owner_app_id_(owner_app_id)
    , redis_(redis)
    , ttl_seconds_(ttl_seconds) {
    acl_.insert(owner_app_id);
}

std::string Workspace::data_key(std::string_view key) const {
    return "labios:ws:" + name_ + ":" + std::string(key);
}

std::string Workspace::version_key(std::string_view key, uint64_t version) const {
    return "labios:ws:" + name_ + ":" + std::string(key)
         + ":v" + std::to_string(version);
}

std::string Workspace::meta_key(std::string_view key) const {
    return "labios:ws:" + name_ + ":_meta:" + std::string(key);
}

std::string Workspace::index_key() const {
    return "labios:ws:" + name_ + ":_index";
}

void Workspace::check_access(uint32_t app_id) const {
    if (acl_.find(app_id) == acl_.end()) {
        throw std::runtime_error("workspace '" + name_
                                 + "': access denied for app_id "
                                 + std::to_string(app_id));
    }
}

void Workspace::grant_access(uint32_t app_id) {
    std::lock_guard lock(mu_);
    acl_.insert(app_id);
}

void Workspace::revoke_access(uint32_t app_id) {
    std::lock_guard lock(mu_);
    if (app_id == owner_app_id_) return;  // Cannot revoke owner
    acl_.erase(app_id);
}

bool Workspace::has_access(uint32_t app_id) const {
    std::shared_lock lock(mu_);
    return acl_.count(app_id) != 0;
}

uint64_t Workspace::put(std::string_view key, std::span<const std::byte> data,
                         uint32_t app_id) {
    std::lock_guard lock(mu_);
    check_access(app_id);

    // Increment version atomically in Redis
    auto mk = meta_key(key);
    int64_t version = redis_.hincrby(mk, "version", 1);

    // Store current data
    auto dk = data_key(key);
    redis_.set_binary(dk, data);

    // Store versioned copy
    auto vk = version_key(key, static_cast<uint64_t>(version));
    redis_.set_binary(vk, data);

    // Update metadata: size and timestamp
    auto now = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    redis_.hset(mk, "size", std::to_string(data.size()));
    redis_.hset(mk, "updated_us", std::to_string(now));

    // Apply TTL if configured
    if (ttl_seconds_ > 0) {
        redis_.expire(dk, ttl_seconds_);
        redis_.expire(vk, ttl_seconds_);
        redis_.expire(mk, ttl_seconds_);
    }

    // Add key to workspace index
    redis_.sadd(index_key(), key);

    return static_cast<uint64_t>(version);
}

std::optional<std::vector<std::byte>> Workspace::get(std::string_view key,
                                                      uint32_t app_id) {
    std::shared_lock lock(mu_);
    check_access(app_id);

    auto dk = data_key(key);
    if (!redis_.get(dk).has_value()) {
        return std::nullopt;
    }
    return redis_.get_binary(dk);
}

std::optional<std::vector<std::byte>> Workspace::get_version(std::string_view key,
                                                              uint64_t version,
                                                              uint32_t app_id) {
    std::shared_lock lock(mu_);
    check_access(app_id);

    auto vk = version_key(key, version);
    if (!redis_.get(vk).has_value()) {
        return std::nullopt;
    }
    return redis_.get_binary(vk);
}

bool Workspace::del(std::string_view key, uint32_t app_id) {
    std::lock_guard lock(mu_);
    check_access(app_id);

    // Check if key exists by reading metadata
    auto mk = meta_key(key);
    auto ver_str = redis_.hget(mk, "version");
    if (!ver_str.has_value()) return false;

    // Delete current data
    redis_.del(data_key(key));

    // Delete all versioned copies
    uint64_t max_ver = std::stoull(ver_str.value());
    for (uint64_t v = 1; v <= max_ver; ++v) {
        redis_.del(version_key(key, v));
    }

    // Delete metadata
    redis_.del(mk);

    // Remove from index
    redis_.srem(index_key(), key);

    return true;
}

std::vector<WorkspaceEntry> Workspace::list(uint32_t app_id) {
    std::shared_lock lock(mu_);
    check_access(app_id);

    auto keys = redis_.smembers(index_key());
    std::vector<WorkspaceEntry> entries;
    entries.reserve(keys.size());

    for (const auto& k : keys) {
        auto mk = meta_key(k);
        auto ver_str = redis_.hget(mk, "version");
        auto size_str = redis_.hget(mk, "size");
        auto ts_str = redis_.hget(mk, "updated_us");

        // Skip keys whose metadata has been evicted (TTL)
        if (!ver_str.has_value()) continue;

        WorkspaceEntry entry;
        entry.key = k;
        entry.version = std::stoull(ver_str.value());
        entry.size = size_str.has_value() ? std::stoull(size_str.value()) : 0;
        entry.updated_us = ts_str.has_value() ? std::stoull(ts_str.value()) : 0;
        entries.push_back(std::move(entry));
    }

    return entries;
}

std::vector<WorkspaceEntry> Workspace::list(std::string_view prefix,
                                             uint32_t app_id) {
    auto all = list(app_id);
    std::vector<WorkspaceEntry> filtered;
    for (auto& entry : all) {
        if (entry.key.compare(0, prefix.size(), prefix) == 0) {
            filtered.push_back(std::move(entry));
        }
    }
    return filtered;
}

void Workspace::destroy() {
    std::lock_guard lock(mu_);
    if (destroyed_.load(std::memory_order_acquire)) return;

    auto keys = redis_.scan_keys("labios:ws:" + name_ + ":*");
    for (const auto& k : keys) {
        redis_.del(k);
    }

    destroyed_.store(true, std::memory_order_release);
}

// ---------------------------------------------------------------------------
// WorkspaceRegistry
// ---------------------------------------------------------------------------

WorkspaceRegistry::WorkspaceRegistry(transport::RedisConnection& redis)
    : redis_(redis) {}

Workspace* WorkspaceRegistry::create(std::string_view name,
                                      uint32_t owner_app_id,
                                      uint32_t ttl_seconds) {
    std::lock_guard lock(mu_);
    auto key = std::string(name);
    if (workspaces_.find(key) != workspaces_.end()) {
        return nullptr;
    }
    auto ws = std::make_unique<Workspace>(key, owner_app_id, redis_, ttl_seconds);
    auto* ptr = ws.get();
    workspaces_[key] = std::move(ws);
    return ptr;
}

Workspace* WorkspaceRegistry::get(std::string_view name) {
    std::shared_lock lock(mu_);
    auto it = workspaces_.find(name);
    if (it == workspaces_.end()) {
        return nullptr;
    }
    return it->second.get();
}

void WorkspaceRegistry::remove(std::string_view name) {
    std::lock_guard lock(mu_);
    auto it = workspaces_.find(name);
    if (it != workspaces_.end()) {
        workspaces_.erase(it);
    }
}

std::vector<std::string> WorkspaceRegistry::list() const {
    std::shared_lock lock(mu_);
    std::vector<std::string> names;
    names.reserve(workspaces_.size());
    for (const auto& [name, _] : workspaces_) {
        names.push_back(name);
    }
    return names;
}

} // namespace labios
