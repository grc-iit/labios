#pragma once

#include <concepts>
#include <sys/stat.h>
#include <sys/types.h>

namespace labios {

template<typename T>
concept IOAdapter = requires(T a, const char* path, int fd, int flags,
                             mode_t mode, void* buf, size_t count,
                             off_t offset, struct stat* st) {
    { a.open(path, flags, mode) } -> std::same_as<int>;
    { a.close(fd) } -> std::same_as<int>;
    { a.write(fd, buf, count) } -> std::same_as<ssize_t>;
    { a.read(fd, buf, count) } -> std::same_as<ssize_t>;
    { a.lseek(fd, offset, flags) } -> std::same_as<off_t>;
    { a.fsync(fd) } -> std::same_as<int>;
};

} // namespace labios
