#include <labios/adapter/fd_table.h>
#include <labios/adapter/posix_adapter.h>
#include <labios/config.h>
#include <labios/session.h>

#include <dlfcn.h>
#include <fcntl.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <chrono>
#include <memory>
#include <mutex>
#include <string_view>
#include <thread>

namespace {

using open_fn      = int(*)(const char*, int, ...);
using close_fn     = int(*)(int);
using write_fn     = ssize_t(*)(int, const void*, size_t);
using read_fn      = ssize_t(*)(int, void*, size_t);
using pwrite_fn    = ssize_t(*)(int, const void*, size_t, off_t);
using pread_fn     = ssize_t(*)(int, void*, size_t, off_t);
using lseek_fn     = off_t(*)(int, off_t, int);
using fsync_fn     = int(*)(int);
using unlink_fn    = int(*)(const char*);
using access_fn    = int(*)(const char*, int);
using mkdir_fn     = int(*)(const char*, mode_t);
using rmdir_fn     = int(*)(const char*);
using rename_fn    = int(*)(const char*, const char*);
using ftruncate_fn = int(*)(int, off_t);
using dup_fn       = int(*)(int);
using dup2_fn      = int(*)(int, int);

// glibc stat interception uses __xstat/__fxstat/__lxstat
using xstat_fn     = int(*)(int, const char*, struct stat*);
using fxstat_fn    = int(*)(int, int, struct stat*);
using lxstat_fn    = int(*)(int, const char*, struct stat*);

open_fn      real_open = nullptr;
close_fn     real_close = nullptr;
write_fn     real_write = nullptr;
read_fn      real_read = nullptr;
pwrite_fn    real_pwrite = nullptr;
pread_fn     real_pread = nullptr;
lseek_fn     real_lseek = nullptr;
fsync_fn     real_fsync = nullptr;
unlink_fn    real_unlink = nullptr;
access_fn    real_access = nullptr;
mkdir_fn     real_mkdir = nullptr;
rmdir_fn     real_rmdir = nullptr;
rename_fn    real_rename = nullptr;
ftruncate_fn real_ftruncate = nullptr;
dup_fn       real_dup = nullptr;
dup2_fn      real_dup2 = nullptr;
xstat_fn     real_xstat = nullptr;
fxstat_fn    real_fxstat = nullptr;
lxstat_fn    real_lxstat = nullptr;

labios::Config g_config;
std::unique_ptr<labios::Session> g_session;
std::unique_ptr<labios::FdTable> g_fd_table;
std::unique_ptr<labios::POSIXAdapter> g_adapter;
std::once_flag g_symbols_flag;
std::once_flag g_config_flag;
std::once_flag g_session_flag;
bool g_symbols_loaded = false;
bool g_initialized = false;

// When true, all intercepted calls fall through to the real filesystem.
// Set when LABIOS infrastructure is persistently unavailable.
bool g_fallback_mode = false;

// Prevents deadlock when config loading or session creation triggers
// intercepted I/O calls (e.g., TOML parsing calls open() internally).
thread_local bool g_in_init = false;

constexpr int MAX_SESSION_RETRIES = 3;
constexpr int RETRY_DELAY_MS = 500;

template<typename T>
T load_sym(const char* name) {
    return reinterpret_cast<T>(dlsym(RTLD_NEXT, name));
}

void init_real_symbols() {
    std::call_once(g_symbols_flag, []() {
        real_open      = load_sym<open_fn>("open");
        real_close     = load_sym<close_fn>("close");
        real_write     = load_sym<write_fn>("write");
        real_read      = load_sym<read_fn>("read");
        real_pwrite    = load_sym<pwrite_fn>("pwrite");
        real_pread     = load_sym<pread_fn>("pread");
        real_lseek     = load_sym<lseek_fn>("lseek");
        real_fsync     = load_sym<fsync_fn>("fsync");
        real_unlink    = load_sym<unlink_fn>("unlink");
        real_access    = load_sym<access_fn>("access");
        real_mkdir     = load_sym<mkdir_fn>("mkdir");
        real_rmdir     = load_sym<rmdir_fn>("rmdir");
        real_rename    = load_sym<rename_fn>("rename");
        real_ftruncate = load_sym<ftruncate_fn>("ftruncate");
        real_dup       = load_sym<dup_fn>("dup");
        real_dup2      = load_sym<dup2_fn>("dup2");
        real_xstat     = load_sym<xstat_fn>("__xstat");
        real_fxstat    = load_sym<fxstat_fn>("__fxstat");
        real_lxstat    = load_sym<lxstat_fn>("__lxstat");
        g_symbols_loaded = true;
    });
}

void init_config() {
    std::call_once(g_config_flag, []() {
        g_in_init = true;
        const char* config_path = std::getenv("LABIOS_CONFIG_PATH");
        g_config = labios::load_config(
            config_path ? config_path : "/etc/labios/labios.toml");
        g_fd_table = std::make_unique<labios::FdTable>();
        g_in_init = false;
        g_initialized = true;
    });
}

void init_session() {
    std::call_once(g_session_flag, []() {
        g_in_init = true;

        for (int attempt = 0; attempt < MAX_SESSION_RETRIES; ++attempt) {
            try {
                g_session = std::make_unique<labios::Session>(g_config);
                g_adapter = std::make_unique<labios::POSIXAdapter>(
                    *g_session, *g_fd_table);

                auto& cm = g_session->content_manager();
                cm.set_flush_callback([](int /*fd*/, std::vector<labios::FlushRegion> regions) {
                    if (!g_session) return;
                    for (auto& region : regions) {
                        try {
                            auto pending = g_session->label_manager().publish_write(
                                region.filepath, region.offset, region.data);
                            g_session->label_manager().wait(pending);
                            g_session->catalog_manager().track_write(
                                region.filepath, region.offset, region.data.size());
                        } catch (...) {}
                    }
                });
                cm.start_flush_timer();

                g_in_init = false;
                return;
            } catch (...) {
                g_session.reset();
                g_adapter.reset();
                if (attempt + 1 < MAX_SESSION_RETRIES) {
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(RETRY_DELAY_MS));
                }
            }
        }

        // All retries exhausted. Fall through to real FS.
        g_fallback_mode = true;
        g_in_init = false;
    });
}

bool is_labios_path(const char* path) {
    if (!path || !g_initialized || g_in_init || g_fallback_mode) return false;
    std::string_view sv(path);
    for (auto& prefix : g_config.intercept_prefixes) {
        if (sv.starts_with(prefix)) return true;
    }
    return false;
}

bool is_labios_fd(int fd) {
    if (!g_fd_table || g_in_init || g_fallback_mode) return false;
    return g_fd_table->is_labios_fd(fd);
}

} // anonymous namespace

__attribute__((constructor))
static void labios_intercept_init() {
    init_real_symbols();
    init_config();
}

__attribute__((destructor))
static void labios_intercept_fini() {
    // Flush all cached data before teardown.
    if (g_session) {
        try {
            auto flushed = g_session->content_manager().flush_all();
            for (auto& [fd, regions] : flushed) {
                for (auto& region : regions) {
                    auto pending = g_session->label_manager().publish_write(
                        region.filepath, region.offset, region.data);
                    g_session->label_manager().wait(pending);
                    g_session->catalog_manager().track_write(
                        region.filepath, region.offset, region.data.size());
                }
            }
        } catch (...) {}
    }
    g_adapter.reset();
    g_session.reset();
    g_fd_table.reset();
}

// Each intercepted function loads real symbols first, then checks the
// re-entrancy guard before touching any LABIOS state.  During init
// (config parsing, session creation) all calls fall through to libc.

extern "C" int open(const char* path, int flags, ...) {
    init_real_symbols();
    va_list ap;
    va_start(ap, flags);
    mode_t mode = (flags & O_CREAT) ? static_cast<mode_t>(va_arg(ap, int)) : 0;
    va_end(ap);

    if (!g_in_init) {
        init_config();
        if (is_labios_path(path)) {
            init_session();
            return g_adapter->open(path, flags, mode);
        }
    }
    return real_open(path, flags, mode);
}

extern "C" int open64(const char* path, int flags, ...) {
    init_real_symbols();
    va_list ap;
    va_start(ap, flags);
    mode_t mode = (flags & O_CREAT) ? static_cast<mode_t>(va_arg(ap, int)) : 0;
    va_end(ap);

    if (!g_in_init) {
        init_config();
        if (is_labios_path(path)) {
            init_session();
            return g_adapter->open(path, flags, mode);
        }
    }
    return real_open(path, flags | O_LARGEFILE, mode);
}

extern "C" int close(int fd) {
    init_real_symbols();
    if (!g_in_init) {
        init_config();
        if (is_labios_fd(fd)) return g_adapter->close(fd);
    }
    return real_close(fd);
}

extern "C" ssize_t write(int fd, const void* buf, size_t count) {
    init_real_symbols();
    if (!g_in_init) {
        init_config();
        if (is_labios_fd(fd)) return g_adapter->write(fd, buf, count);
    }
    return real_write(fd, buf, count);
}

extern "C" ssize_t read(int fd, void* buf, size_t count) {
    init_real_symbols();
    if (!g_in_init) {
        init_config();
        if (is_labios_fd(fd)) return g_adapter->read(fd, buf, count);
    }
    return real_read(fd, buf, count);
}

extern "C" ssize_t pwrite(int fd, const void* buf, size_t count, off_t offset) {
    init_real_symbols();
    if (!g_in_init) {
        init_config();
        if (is_labios_fd(fd)) return g_adapter->pwrite(fd, buf, count, offset);
    }
    return real_pwrite(fd, buf, count, offset);
}

extern "C" ssize_t pread(int fd, void* buf, size_t count, off_t offset) {
    init_real_symbols();
    if (!g_in_init) {
        init_config();
        if (is_labios_fd(fd)) return g_adapter->pread(fd, buf, count, offset);
    }
    return real_pread(fd, buf, count, offset);
}

extern "C" off_t lseek(int fd, off_t offset, int whence) {
    init_real_symbols();
    if (!g_in_init) {
        init_config();
        if (is_labios_fd(fd)) return g_adapter->lseek(fd, offset, whence);
    }
    return real_lseek(fd, offset, whence);
}

extern "C" int fsync(int fd) {
    init_real_symbols();
    if (!g_in_init) {
        init_config();
        if (is_labios_fd(fd)) return g_adapter->fsync(fd);
    }
    return real_fsync(fd);
}

extern "C" int fdatasync(int fd) {
    init_real_symbols();
    if (!g_in_init) {
        init_config();
        if (is_labios_fd(fd)) return g_adapter->fsync(fd);
    }
    return real_fsync(fd);
}

extern "C" int unlink(const char* path) {
    init_real_symbols();
    if (!g_in_init) {
        init_config();
        if (is_labios_path(path)) { init_session(); return g_adapter->unlink(path); }
    }
    return real_unlink(path);
}

extern "C" int access(const char* path, int mode) {
    init_real_symbols();
    if (!g_in_init) {
        init_config();
        if (is_labios_path(path)) { init_session(); return g_adapter->access(path, mode); }
    }
    return real_access(path, mode);
}

extern "C" int mkdir(const char* path, mode_t mode) {
    init_real_symbols();
    if (!g_in_init) {
        init_config();
        if (is_labios_path(path)) { init_session(); return g_adapter->mkdir(path, mode); }
    }
    return real_mkdir(path, mode);
}

extern "C" int ftruncate(int fd, off_t length) {
    init_real_symbols();
    if (!g_in_init) {
        init_config();
        if (is_labios_fd(fd)) return g_adapter->ftruncate(fd, length);
    }
    return real_ftruncate(fd, length);
}

// glibc stat wrappers
extern "C" int __xstat(int ver, const char* path, struct stat* st) {
    init_real_symbols();
    if (!g_in_init) {
        init_config();
        if (is_labios_path(path)) { init_session(); return g_adapter->stat(path, st); }
    }
    if (real_xstat) return real_xstat(ver, path, st);
    errno = ENOSYS;
    return -1;
}

extern "C" int __fxstat(int ver, int fd, struct stat* st) {
    init_real_symbols();
    if (!g_in_init) {
        init_config();
        if (is_labios_fd(fd)) return g_adapter->fstat(fd, st);
    }
    if (real_fxstat) return real_fxstat(ver, fd, st);
    errno = ENOSYS;
    return -1;
}

extern "C" int __lxstat(int ver, const char* path, struct stat* st) {
    init_real_symbols();
    if (!g_in_init) {
        init_config();
        if (is_labios_path(path)) { init_session(); return g_adapter->lstat(path, st); }
    }
    if (real_lxstat) return real_lxstat(ver, path, st);
    errno = ENOSYS;
    return -1;
}

extern "C" int dup(int oldfd) {
    init_real_symbols();
    if (!g_in_init) {
        init_config();
        if (is_labios_fd(oldfd)) return g_adapter->dup(oldfd);
    }
    return real_dup(oldfd);
}

extern "C" int dup2(int oldfd, int newfd) {
    init_real_symbols();
    if (!g_in_init) {
        init_config();
        if (is_labios_fd(oldfd)) return g_adapter->dup2(oldfd, newfd);
    }
    return real_dup2(oldfd, newfd);
}

extern "C" int rename(const char* oldpath, const char* newpath) {
    init_real_symbols();
    if (!g_in_init) {
        init_config();
        if (is_labios_path(oldpath) || is_labios_path(newpath)) {
            init_session();
            return g_adapter->rename(oldpath, newpath);
        }
    }
    return real_rename(oldpath, newpath);
}

extern "C" int rmdir(const char* path) {
    init_real_symbols();
    if (!g_in_init) {
        init_config();
        if (is_labios_path(path)) { init_session(); return g_adapter->rmdir(path); }
    }
    return real_rmdir(path);
}

// 64-bit variants (on LP64 Linux these are identical to the base calls,
// but some applications and libc versions reference them explicitly).

extern "C" off_t lseek64(int fd, off_t offset, int whence) {
    init_real_symbols();
    if (!g_in_init) {
        init_config();
        if (is_labios_fd(fd)) return g_adapter->lseek(fd, offset, whence);
    }
    return real_lseek(fd, offset, whence);
}

extern "C" int ftruncate64(int fd, off_t length) {
    init_real_symbols();
    if (!g_in_init) {
        init_config();
        if (is_labios_fd(fd)) return g_adapter->ftruncate(fd, length);
    }
    return real_ftruncate(fd, length);
}
