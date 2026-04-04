#pragma once

#include <labios/adapter/adapter.h>
#include <labios/adapter/fd_table.h>
#include <labios/session.h>

#include <sys/stat.h>
#include <sys/types.h>

namespace labios {

class POSIXAdapter {
public:
    POSIXAdapter(Session& session, FdTable& fd_table);

    int open(const char* path, int flags, mode_t mode);
    int close(int fd);
    ssize_t write(int fd, const void* buf, size_t count);
    ssize_t pwrite(int fd, const void* buf, size_t count, off_t offset);
    ssize_t read(int fd, void* buf, size_t count);
    ssize_t pread(int fd, void* buf, size_t count, off_t offset);
    off_t lseek(int fd, off_t offset, int whence);
    int fsync(int fd);
    int stat(const char* path, struct stat* st);
    int fstat(int fd, struct stat* st);
    int unlink(const char* path);
    int access(const char* path, int mode);
    int mkdir(const char* path, mode_t mode);
    int ftruncate(int fd, off_t length);

private:
    Session& session_;
    FdTable& fd_table_;

    ssize_t do_write(FileState* state, int fd, const void* buf, size_t count,
                     off_t off, bool update_offset);
    ssize_t do_read(FileState* state, int fd, void* buf, size_t count,
                    off_t off, bool update_offset);
    int populate_stat(std::string_view filepath, struct stat* st);
    void flush_and_publish(int fd);
};

static_assert(IOAdapter<POSIXAdapter>);

} // namespace labios
