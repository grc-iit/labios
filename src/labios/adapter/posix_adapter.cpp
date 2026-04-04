#include <labios/adapter/posix_adapter.h>

#include <cerrno>
#include <cstring>

namespace labios {

POSIXAdapter::POSIXAdapter(Session& session, FdTable& fd_table)
    : session_(session), fd_table_(fd_table) {}

int POSIXAdapter::open(const char* path, int flags, mode_t /*mode*/) {
    int fd = fd_table_.allocate(path, flags);
    if (fd < 0) { errno = ENOMEM; return -1; }
    session_.catalog_manager().track_open(path, flags);
    return fd;
}

int POSIXAdapter::close(int fd) {
    auto* state = fd_table_.get(fd);
    if (!state) { errno = EBADF; return -1; }

    bool was_last = fd_table_.release(fd);
    if (was_last) {
        try {
            flush_and_publish(fd);
        } catch (...) {}
        session_.content_manager().evict(fd);
    }
    return 0;
}

ssize_t POSIXAdapter::write(int fd, const void* buf, size_t count) {
    auto* state = fd_table_.get(fd);
    if (!state) { errno = EBADF; return -1; }
    std::lock_guard lock(state->mu);
    return do_write(state, fd, buf, count,
                    static_cast<off_t>(state->offset), true);
}

ssize_t POSIXAdapter::pwrite(int fd, const void* buf, size_t count,
                              off_t offset) {
    auto* state = fd_table_.get(fd);
    if (!state) { errno = EBADF; return -1; }
    std::lock_guard lock(state->mu);
    return do_write(state, fd, buf, count, offset, false);
}

ssize_t POSIXAdapter::do_write(FileState* state, int fd, const void* buf,
                                size_t count, off_t off, bool update_offset) {
    if (!buf && count > 0) { errno = EFAULT; return -1; }
    if (count == 0) return 0;
    auto data = std::span<const std::byte>(
        static_cast<const std::byte*>(buf), count);
    auto& cfg = session_.config();
    auto& label_mgr = session_.label_manager();
    auto& content_mgr = session_.content_manager();
    auto& catalog_mgr = session_.catalog_manager();

    try {
        if (count < cfg.label_min_size) {
            auto flush_regions = content_mgr.cache_write(
                fd, state->filepath, static_cast<uint64_t>(off), data);
            for (auto& region : flush_regions) {
                auto pending = label_mgr.publish_write(
                    region.filepath, region.offset, region.data);
                if (state->sync_mode) label_mgr.wait(pending);
                catalog_mgr.track_write(region.filepath, region.offset,
                                         region.data.size());
            }
        } else {
            auto pending = label_mgr.publish_write(
                state->filepath, static_cast<uint64_t>(off), data);
            if (state->sync_mode) label_mgr.wait(pending);
            catalog_mgr.track_write(state->filepath,
                                     static_cast<uint64_t>(off), count);
        }

        if (update_offset) state->offset = static_cast<uint64_t>(off) + count;
        return static_cast<ssize_t>(count);
    } catch (const std::exception&) {
        errno = EIO;
        return -1;
    }
}

ssize_t POSIXAdapter::read(int fd, void* buf, size_t count) {
    auto* state = fd_table_.get(fd);
    if (!state) { errno = EBADF; return -1; }
    std::lock_guard lock(state->mu);
    return do_read(state, fd, buf, count,
                   static_cast<off_t>(state->offset), true);
}

ssize_t POSIXAdapter::pread(int fd, void* buf, size_t count, off_t offset) {
    auto* state = fd_table_.get(fd);
    if (!state) { errno = EBADF; return -1; }
    std::lock_guard lock(state->mu);
    return do_read(state, fd, buf, count, offset, false);
}

ssize_t POSIXAdapter::do_read(FileState* state, int fd, void* buf,
                               size_t count, off_t off, bool update_offset) {
    auto& label_mgr = session_.label_manager();
    auto& content_mgr = session_.content_manager();

    try {
        auto cached = content_mgr.cache_read(
            fd, static_cast<uint64_t>(off), count);
        if (cached.has_value() && cached->size() == count) {
            std::memcpy(buf, cached->data(), count);
            if (update_offset) state->offset = static_cast<uint64_t>(off) + count;
            return static_cast<ssize_t>(count);
        }

        // Preserve read-after-write consistency: if the requested range is not
        // fully available from the small-I/O cache, flush staged writes first
        // so the READ labels observe the latest data.
        flush_and_publish(fd);

        auto pending = label_mgr.publish_read(
            state->filepath, static_cast<uint64_t>(off), count);
        auto data = label_mgr.wait_read(pending);

        size_t bytes = std::min(data.size(), count);
        std::memcpy(buf, data.data(), bytes);
        if (update_offset) state->offset = static_cast<uint64_t>(off) + bytes;
        return static_cast<ssize_t>(bytes);
    } catch (const std::exception&) {
        errno = EIO;
        return -1;
    }
}

off_t POSIXAdapter::lseek(int fd, off_t offset, int whence) {
    auto* state = fd_table_.get(fd);
    if (!state) { errno = EBADF; return -1; }
    std::lock_guard lock(state->mu);

    switch (whence) {
        case SEEK_SET:
            if (offset < 0) { errno = EINVAL; return -1; }
            state->offset = static_cast<uint64_t>(offset);
            break;
        case SEEK_CUR: {
            int64_t new_pos = static_cast<int64_t>(state->offset) + offset;
            if (new_pos < 0) { errno = EINVAL; return -1; }
            state->offset = static_cast<uint64_t>(new_pos);
            break;
        }
        case SEEK_END: {
            auto info = session_.catalog_manager().get_file_info(state->filepath);
            int64_t file_size = info.has_value() ? static_cast<int64_t>(info->size) : 0;
            int64_t new_pos = file_size + offset;
            if (new_pos < 0) { errno = EINVAL; return -1; }
            state->offset = static_cast<uint64_t>(new_pos);
            break;
        }
        default:
            errno = EINVAL;
            return -1;
    }
    return static_cast<off_t>(state->offset);
}

int POSIXAdapter::fsync(int fd) {
    auto* state = fd_table_.get(fd);
    if (!state) { errno = EBADF; return -1; }
    std::lock_guard lock(state->mu);

    try {
        flush_and_publish(fd);
        return 0;
    } catch (const std::exception&) {
        errno = EIO;
        return -1;
    }
}

void POSIXAdapter::flush_and_publish(int fd) {
    auto regions = session_.content_manager().flush(fd);
    for (auto& region : regions) {
        auto pending = session_.label_manager().publish_write(
            region.filepath, region.offset, region.data);
        session_.label_manager().wait(pending);
        session_.catalog_manager().track_write(
            region.filepath, region.offset, region.data.size());
    }
}

int POSIXAdapter::populate_stat(std::string_view filepath, struct stat* st) {
    auto info = session_.catalog_manager().get_file_info(filepath);
    if (!info.has_value() || !info->exists) {
        errno = ENOENT;
        return -1;
    }
    std::memset(st, 0, sizeof(*st));
    st->st_mode = S_IFREG | 0644;
    st->st_size = static_cast<off_t>(info->size);
    st->st_mtim.tv_sec = static_cast<time_t>(info->mtime_ms / 1000);
    st->st_mtim.tv_nsec = static_cast<long>((info->mtime_ms % 1000) * 1000000);
    st->st_blksize = 4096;
    st->st_blocks = static_cast<blkcnt_t>((info->size + 511) / 512);
    return 0;
}

int POSIXAdapter::stat(const char* path, struct stat* st) {
    return populate_stat(path, st);
}

int POSIXAdapter::fstat(int fd, struct stat* st) {
    auto* state = fd_table_.get(fd);
    if (!state) { errno = EBADF; return -1; }
    return populate_stat(state->filepath, st);
}

int POSIXAdapter::unlink(const char* path) {
    session_.catalog_manager().track_unlink(path);
    return 0;
}

int POSIXAdapter::access(const char* path, int /*mode*/) {
    auto info = session_.catalog_manager().get_file_info(path);
    if (!info.has_value() || !info->exists) {
        errno = ENOENT;
        return -1;
    }
    return 0;
}

int POSIXAdapter::mkdir(const char* /*path*/, mode_t /*mode*/) {
    return 0;
}

int POSIXAdapter::ftruncate(int fd, off_t length) {
    auto* state = fd_table_.get(fd);
    if (!state) { errno = EBADF; return -1; }
    session_.catalog_manager().track_truncate(state->filepath,
                                               static_cast<uint64_t>(length));
    return 0;
}

} // namespace labios
