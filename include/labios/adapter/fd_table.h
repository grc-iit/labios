#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace labios {

struct FileState {
    std::string filepath;
    uint64_t    offset = 0;
    int         open_flags = 0;
    bool        sync_mode = false;
    std::mutex  mu;
    std::atomic<int> ref_count{1};
};

class FdTable {
public:
    FdTable();
    ~FdTable();

    FdTable(const FdTable&) = delete;
    FdTable& operator=(const FdTable&) = delete;

    int allocate(const std::string& filepath, int flags);
    FileState* get(int fd);
    bool is_labios_fd(int fd) const;
    bool release(int fd);
    int duplicate(int old_fd);
    size_t size() const;

private:
    mutable std::shared_mutex mu_;
    std::unordered_map<int, std::shared_ptr<FileState>> table_;

    static constexpr int BITSET_SIZE = 1048576;
    std::vector<std::atomic<bool>> bitset_;

    using close_fn = int(*)(int);
    close_fn real_close_ = nullptr;
};

} // namespace labios
