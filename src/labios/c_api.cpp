#include <labios/labios.h>

#include <labios/client.h>
#include <labios/config.h>

#include <algorithm>
#include <cstring>
#include <exception>
#include <new>
#include <span>
#include <utility>
#include <vector>

struct labios_client {
    explicit labios_client(const labios::Config& cfg) : impl(cfg) {}
    labios::Client impl;
};

struct labios_status {
    labios_client_t client = nullptr;
    labios::PendingIO pending;
    bool is_read = false;
};

namespace {

std::span<const std::byte> input_span(const void* data, size_t size) {
    return {static_cast<const std::byte*>(data), size};
}

labios_error_t make_client(const labios::Config& cfg, labios_client_t* out) noexcept {
    if (out == nullptr) return LABIOS_ERR_INVALID;
    *out = nullptr;

    try {
        auto* handle = new (std::nothrow) labios_client(cfg);
        if (handle == nullptr) return LABIOS_ERR_IO;
        *out = handle;
        return LABIOS_OK;
    } catch (const std::exception&) {
        return LABIOS_ERR_CONNECT;
    } catch (...) {
        return LABIOS_ERR_CONNECT;
    }
}

labios_error_t copy_to_c_buffer(const std::vector<std::byte>& data,
                                void* buf, size_t buf_size,
                                size_t* bytes_read) {
    if (bytes_read == nullptr) return LABIOS_ERR_INVALID;
    *bytes_read = data.size();

    if (!data.empty() && buf == nullptr) return LABIOS_ERR_INVALID;
    if (data.size() > buf_size) return LABIOS_ERR_INVALID;
    if (!data.empty()) {
        std::memcpy(buf, data.data(), data.size());
    }
    return LABIOS_OK;
}

} // namespace

extern "C" labios_error_t labios_connect(const char* nats_url,
                                          const char* redis_host,
                                          int redis_port,
                                          labios_client_t* out) {
    if (out != nullptr) *out = nullptr;
    if (nats_url == nullptr || redis_host == nullptr ||
        redis_port <= 0 || out == nullptr) {
        return LABIOS_ERR_INVALID;
    }

    labios::Config cfg;
    cfg.nats_url = nats_url;
    cfg.redis_host = redis_host;
    cfg.redis_port = redis_port;
    return make_client(cfg, out);
}

extern "C" labios_error_t labios_connect_config(const char* config_path,
                                                 labios_client_t* out) {
    if (out != nullptr) *out = nullptr;
    if (config_path == nullptr || out == nullptr) return LABIOS_ERR_INVALID;

    try {
        auto cfg = labios::load_config(config_path);
        return make_client(cfg, out);
    } catch (const std::exception&) {
        return LABIOS_ERR_INVALID;
    } catch (...) {
        return LABIOS_ERR_INVALID;
    }
}

extern "C" void labios_disconnect(labios_client_t client) {
    delete client;
}

extern "C" labios_error_t labios_write(labios_client_t client,
                                        const char* filepath,
                                        const void* data, size_t size,
                                        uint64_t offset) {
    if (client == nullptr || filepath == nullptr || (data == nullptr && size > 0)) {
        return LABIOS_ERR_INVALID;
    }

    try {
        client->impl.write(filepath, input_span(data, size), offset);
        return LABIOS_OK;
    } catch (const std::exception&) {
        return LABIOS_ERR_IO;
    } catch (...) {
        return LABIOS_ERR_IO;
    }
}

extern "C" labios_error_t labios_read(labios_client_t client,
                                       const char* filepath,
                                       uint64_t offset, uint64_t size,
                                       void* buf, size_t buf_size,
                                       size_t* bytes_read) {
    if (bytes_read != nullptr) *bytes_read = 0;
    if (client == nullptr || filepath == nullptr || bytes_read == nullptr) {
        return LABIOS_ERR_INVALID;
    }

    try {
        auto data = client->impl.read(filepath, offset, size);
        return copy_to_c_buffer(data, buf, buf_size, bytes_read);
    } catch (const std::exception&) {
        return LABIOS_ERR_IO;
    } catch (...) {
        return LABIOS_ERR_IO;
    }
}

extern "C" labios_error_t labios_async_write(labios_client_t client,
                                             const char* filepath,
                                             const void* data, size_t size,
                                             uint64_t offset,
                                             labios_status_t* out) {
    if (out != nullptr) *out = nullptr;
    if (client == nullptr || filepath == nullptr || out == nullptr ||
        (data == nullptr && size > 0)) {
        return LABIOS_ERR_INVALID;
    }

    try {
        auto pending = client->impl.async_write(filepath, input_span(data, size), offset);
        auto* status = new (std::nothrow) labios_status{client, std::move(pending), false};
        if (status == nullptr) return LABIOS_ERR_IO;
        *out = status;
        return LABIOS_OK;
    } catch (const std::exception&) {
        return LABIOS_ERR_IO;
    } catch (...) {
        return LABIOS_ERR_IO;
    }
}

extern "C" labios_error_t labios_async_read(labios_client_t client,
                                            const char* filepath,
                                            uint64_t offset, uint64_t size,
                                            labios_status_t* out) {
    if (out != nullptr) *out = nullptr;
    if (client == nullptr || filepath == nullptr || out == nullptr) {
        return LABIOS_ERR_INVALID;
    }

    try {
        auto pending = client->impl.async_read(filepath, offset, size);
        auto* status = new (std::nothrow) labios_status{client, std::move(pending), true};
        if (status == nullptr) return LABIOS_ERR_IO;
        *out = status;
        return LABIOS_OK;
    } catch (const std::exception&) {
        return LABIOS_ERR_IO;
    } catch (...) {
        return LABIOS_ERR_IO;
    }
}

extern "C" labios_error_t labios_wait(labios_status_t status) {
    if (status == nullptr || status->client == nullptr) return LABIOS_ERR_INVALID;

    try {
        status->client->impl.wait(status->pending);
        return LABIOS_OK;
    } catch (const std::exception&) {
        return LABIOS_ERR_IO;
    } catch (...) {
        return LABIOS_ERR_IO;
    }
}

extern "C" labios_error_t labios_wait_read(labios_status_t status,
                                           void* buf, size_t buf_size,
                                           size_t* bytes_read) {
    if (bytes_read != nullptr) *bytes_read = 0;
    if (status == nullptr || status->client == nullptr || !status->is_read ||
        bytes_read == nullptr) {
        return LABIOS_ERR_INVALID;
    }

    try {
        auto data = status->client->impl.wait_read(status->pending);
        return copy_to_c_buffer(data, buf, buf_size, bytes_read);
    } catch (const std::exception&) {
        return LABIOS_ERR_IO;
    } catch (...) {
        return LABIOS_ERR_IO;
    }
}

extern "C" void labios_status_free(labios_status_t status) {
    delete status;
}
