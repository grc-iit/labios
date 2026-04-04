#include <labios/adapter/fd_table.h>

#include <dlfcn.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace labios {

FdTable::FdTable() : bitset_(BITSET_SIZE) {
    real_close_ = reinterpret_cast<close_fn>(dlsym(RTLD_NEXT, "close"));
    if (!real_close_) {
        real_close_ = ::close;
    }
}

FdTable::~FdTable() = default;

int FdTable::allocate(const std::string& filepath, int flags) {
    int fd = memfd_create("labios", MFD_CLOEXEC);
    if (fd < 0) {
        fd = ::open("/dev/null", O_RDWR | O_CLOEXEC);
        if (fd < 0) return -1;
    }

    auto state = std::make_shared<FileState>();
    state->filepath = filepath;
    state->open_flags = flags;
    state->sync_mode = (flags & O_SYNC) != 0 || (flags & O_DSYNC) != 0;

    {
        std::unique_lock lock(mu_);
        table_[fd] = state;
    }

    if (fd >= 0 && fd < BITSET_SIZE) {
        bitset_[fd].store(true, std::memory_order_release);
    }

    return fd;
}

FileState* FdTable::get(int fd) {
    std::shared_lock lock(mu_);
    auto it = table_.find(fd);
    if (it == table_.end()) return nullptr;
    return it->second.get();
}

bool FdTable::is_labios_fd(int fd) const {
    if (fd < 0 || fd >= BITSET_SIZE) return false;
    return bitset_[fd].load(std::memory_order_acquire);
}

bool FdTable::release(int fd) {
    std::shared_ptr<FileState> state;
    {
        std::unique_lock lock(mu_);
        auto it = table_.find(fd);
        if (it == table_.end()) return false;
        state = it->second;
        table_.erase(it);
    }

    if (fd >= 0 && fd < BITSET_SIZE) {
        bitset_[fd].store(false, std::memory_order_release);
    }

    if (real_close_) real_close_(fd);

    int remaining = state->ref_count.fetch_sub(1, std::memory_order_acq_rel);
    return remaining == 1;
}

int FdTable::duplicate(int old_fd) {
    std::shared_ptr<FileState> state;
    {
        std::shared_lock lock(mu_);
        auto it = table_.find(old_fd);
        if (it == table_.end()) return -1;
        state = it->second;
    }

    int new_fd = ::dup(old_fd);
    if (new_fd < 0) return -1;

    state->ref_count.fetch_add(1, std::memory_order_relaxed);

    {
        std::unique_lock lock(mu_);
        table_[new_fd] = state;
    }

    if (new_fd >= 0 && new_fd < BITSET_SIZE) {
        bitset_[new_fd].store(true, std::memory_order_release);
    }

    return new_fd;
}

size_t FdTable::size() const {
    std::shared_lock lock(mu_);
    return table_.size();
}

} // namespace labios
