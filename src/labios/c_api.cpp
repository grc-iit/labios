#include <labios/labios.h>

#include <labios/client.h>
#include <labios/config.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct labios_client { uint64_t token = 0; };
struct labios_status { uint64_t token = 0; };

namespace {

struct StatusRecord {
    labios::Operation operation;
};

struct HandleRegistry {
    std::mutex mutex;
    uint64_t next_token = 1;
    std::unordered_map<labios_client_t, std::shared_ptr<labios::Client>> clients;
    std::unordered_map<labios_status_t, std::shared_ptr<StatusRecord>> statuses;
    std::vector<std::unique_ptr<labios_client>> client_tokens;
    std::vector<std::unique_ptr<labios_status>> status_tokens;
};

HandleRegistry& registry() {
    static HandleRegistry value;
    return value;
}

struct LastError {
    labios_error_t code = LABIOS_OK;
    std::string category;
    std::string message;
};
thread_local LastError last_error;

labios_error_t fail(labios_error_t code, std::string category,
                    std::string message) {
    last_error = {code, std::move(category), std::move(message)};
    return code;
}

void clear_error() { last_error = {}; }

std::shared_ptr<labios::Client> acquire_client(labios_client_t token) {
    std::lock_guard lock(registry().mutex);
    const auto it = registry().clients.find(token);
    return it == registry().clients.end() ? nullptr : it->second;
}

std::shared_ptr<StatusRecord> acquire_status(labios_status_t token) {
    std::lock_guard lock(registry().mutex);
    const auto it = registry().statuses.find(token);
    return it == registry().statuses.end() ? nullptr : it->second;
}

labios_client_t register_client(std::shared_ptr<labios::Client> client) {
    auto token = std::make_unique<labios_client>();
    std::lock_guard lock(registry().mutex);
    token->token = registry().next_token++;
    auto* key = token.get();
    registry().client_tokens.push_back(std::move(token));
    registry().clients.emplace(key, std::move(client));
    return key;
}

labios_status_t register_status(labios::Operation operation) {
    auto token = std::make_unique<labios_status>();
    auto record = std::make_shared<StatusRecord>(StatusRecord{std::move(operation)});
    std::lock_guard lock(registry().mutex);
    token->token = registry().next_token++;
    auto* key = token.get();
    registry().status_tokens.push_back(std::move(token));
    registry().statuses.emplace(key, std::move(record));
    return key;
}

std::span<const std::byte> input_span(const void* data, size_t size) {
    return {static_cast<const std::byte*>(data), size};
}

char* duplicate_string(const std::string& value) {
    auto* output = static_cast<char*>(std::malloc(value.size() + 1));
    if (!output) return nullptr;
    std::memcpy(output, value.c_str(), value.size() + 1);
    return output;
}

labios_completion_state_t c_state(labios::CompletionState state) {
    switch (state) {
        case labios::CompletionState::Pending: return LABIOS_STATE_PENDING;
        case labios::CompletionState::Complete: return LABIOS_STATE_COMPLETE;
        case labios::CompletionState::Failed: return LABIOS_STATE_FAILED;
        case labios::CompletionState::Cancelled: return LABIOS_STATE_CANCELLED;
        case labios::CompletionState::Parked: return LABIOS_STATE_PARKED;
        case labios::CompletionState::Timeout: return LABIOS_STATE_TIMEOUT;
        case labios::CompletionState::Unknown: return LABIOS_STATE_UNKNOWN;
    }
    return LABIOS_STATE_UNKNOWN;
}

labios_lifecycle_state_t c_lifecycle(labios::LifecycleState state) {
    switch (state) {
        case labios::LifecycleState::Submitted: return LABIOS_LIFECYCLE_SUBMITTED;
        case labios::LifecycleState::Admitted: return LABIOS_LIFECYCLE_ADMITTED;
        case labios::LifecycleState::Queued: return LABIOS_LIFECYCLE_QUEUED;
        case labios::LifecycleState::Parked: return LABIOS_LIFECYCLE_PARKED;
        case labios::LifecycleState::Shuffled: return LABIOS_LIFECYCLE_SHUFFLED;
        case labios::LifecycleState::Scheduled: return LABIOS_LIFECYCLE_SCHEDULED;
        case labios::LifecycleState::Executing: return LABIOS_LIFECYCLE_EXECUTING;
        case labios::LifecycleState::Completed: return LABIOS_LIFECYCLE_COMPLETED;
        case labios::LifecycleState::Failed: return LABIOS_LIFECYCLE_FAILED;
        case labios::LifecycleState::Cancelled: return LABIOS_LIFECYCLE_CANCELLED;
        case labios::LifecycleState::Unknown: return LABIOS_LIFECYCLE_UNKNOWN;
    }
    return LABIOS_LIFECYCLE_UNKNOWN;
}

labios_cancel_state_t c_cancel_state(labios::CancellationState state) {
    switch (state) {
        case labios::CancellationState::Cancelled: return LABIOS_CANCEL_CANCELLED;
        case labios::CancellationState::TooLate: return LABIOS_CANCEL_TOO_LATE;
        case labios::CancellationState::Terminal: return LABIOS_CANCEL_TERMINAL;
        case labios::CancellationState::Unknown: return LABIOS_CANCEL_UNKNOWN;
    }
    return LABIOS_CANCEL_UNKNOWN;
}

bool copy_completion(const labios::CompletionResult& source,
                     labios_completion_result_t* destination) {
    *destination = {};
    destination->label_id = source.label_id;
    destination->state = c_state(source.state);
    destination->category = duplicate_string(source.category);
    destination->message = duplicate_string(source.error);
    destination->data_key = duplicate_string(source.data_key);
    destination->park_reason = duplicate_string(source.park_reason);
    if (!destination->category || !destination->message || !destination->data_key ||
        !destination->park_reason) {
        labios_completion_result_release(destination);
        return false;
    }
    destination->observation_version = source.observation_version;
    destination->worker_id = source.worker_id;
    destination->attempt = source.attempt;
    destination->queued_us = source.queued_us;
    destination->dispatched_us = source.dispatched_us;
    destination->started_us = source.started_us;
    destination->completed_us = source.completed_us;
    destination->queue_delay_us = source.queue_delay_us;
    destination->service_time_us = source.service_time_us;
    destination->park_attempts = source.park_attempts;
    destination->next_retry_at_ms = source.next_retry_at_ms;
    destination->lifecycle = c_lifecycle(source.lifecycle);
    return true;
}

labios_error_t copy_wait_result(const labios::WaitResult& source,
                                labios_completion_list_t* destination) {
    *destination = {};
    destination->state = c_state(source.state);
    if (source.results.empty()) return LABIOS_OK;
    destination->items = static_cast<labios_completion_result_t*>(
        std::calloc(source.results.size(), sizeof(labios_completion_result_t)));
    if (!destination->items) {
        return fail(LABIOS_ERR_IO, "ALLOCATION_FAILED",
                    "unable to allocate completion result array");
    }
    destination->count = source.results.size();
    for (size_t i = 0; i < source.results.size(); ++i) {
        if (!copy_completion(source.results[i], &destination->items[i])) {
            labios_completion_list_release(destination);
            return fail(LABIOS_ERR_IO, "ALLOCATION_FAILED",
                        "unable to allocate completion result strings");
        }
    }
    if (source.state == labios::CompletionState::Timeout)
        return fail(LABIOS_ERR_TIMEOUT, "TIMEOUT",
                    "wait deadline expired; operation remains active");
    if (source.state == labios::CompletionState::Cancelled)
        return fail(LABIOS_ERR_CANCELLED, "CANCELED", "operation was cancelled");
    if (source.state == labios::CompletionState::Unknown)
        return fail(LABIOS_ERR_NOT_FOUND, "LOOKUP_FAILED",
                    "label is unknown or completion retention expired");
    if (source.state == labios::CompletionState::Failed) {
        const auto& item = source.results.front();
        return fail(LABIOS_ERR_IO, item.category, item.error);
    }
    return LABIOS_OK;
}

labios_error_t map_exception(const labios::CompletionError& error) {
    if (error.state() == labios::CompletionState::Timeout) {
        return fail(LABIOS_ERR_TIMEOUT, "TIMEOUT", error.what());
    }
    if (error.state() == labios::CompletionState::Cancelled) {
        return fail(LABIOS_ERR_CANCELLED, "CANCELED", error.what());
    }
    if (error.state() == labios::CompletionState::Unknown) {
        return fail(LABIOS_ERR_NOT_FOUND, "LOOKUP_FAILED", error.what());
    }
    return fail(LABIOS_ERR_IO, "EXECUTION_FAILED", error.what());
}

labios_error_t copy_to_c_buffer(const std::vector<std::byte>& data,
                                void* buffer, size_t buffer_size,
                                size_t* bytes_read) {
    if (!bytes_read) return fail(LABIOS_ERR_INVALID, "INVALID_ARGUMENT",
                                 "bytes_read is required");
    *bytes_read = data.size();
    if (data.size() > buffer_size) {
        return fail(LABIOS_ERR_BUFFER_TOO_SMALL, "BUFFER_TOO_SMALL",
                    "output buffer is smaller than the completed result");
    }
    if (!data.empty() && !buffer) {
        return fail(LABIOS_ERR_INVALID, "INVALID_ARGUMENT",
                    "a nonempty result requires an output buffer");
    }
    if (!data.empty()) std::memcpy(buffer, data.data(), data.size());
    clear_error();
    return LABIOS_OK;
}

labios_error_t make_client(const labios::Config& config,
                           labios_client_t* output) noexcept {
    if (!output) return fail(LABIOS_ERR_INVALID, "INVALID_ARGUMENT",
                             "client output is required");
    *output = nullptr;
    try {
        auto client = std::make_shared<labios::Client>(config);
        *output = register_client(std::move(client));
        clear_error();
        return LABIOS_OK;
    } catch (const std::exception& error) {
        return fail(LABIOS_ERR_CONNECT, "CONNECT_FAILED", error.what());
    } catch (...) {
        return fail(LABIOS_ERR_CONNECT, "CONNECT_FAILED",
                    "unknown connection failure");
    }
}

} // namespace

extern "C" labios_error_t labios_connect(const char* nats_url,
                                          const char* redis_host,
                                          int redis_port,
                                          labios_client_t* output) {
    if (output) *output = nullptr;
    if (!nats_url || !redis_host || redis_port <= 0 || !output) {
        return fail(LABIOS_ERR_INVALID, "INVALID_ARGUMENT",
                    "valid NATS URL, Redis host, port, and output are required");
    }
    labios::Config config;
    config.nats_url = nats_url;
    config.redis_host = redis_host;
    config.redis_port = redis_port;
    return make_client(config, output);
}

extern "C" labios_error_t labios_connect_config(const char* path,
                                                 labios_client_t* output) {
    if (output) *output = nullptr;
    if (!path || !output) {
        return fail(LABIOS_ERR_INVALID, "INVALID_ARGUMENT",
                    "config path and client output are required");
    }
    try {
        return make_client(labios::load_config(path), output);
    } catch (const std::exception& error) {
        return fail(LABIOS_ERR_INVALID, "INVALID_CONFIG", error.what());
    }
}

extern "C" void labios_disconnect(labios_client_t client) {
    if (!client) return;
    std::lock_guard lock(registry().mutex);
    registry().clients.erase(client);
}
extern "C" void labios_disconnect_ref(labios_client_t* client) {
    if (!client) return;
    labios_disconnect(*client);
    *client = nullptr;
}

extern "C" labios_error_t labios_write(labios_client_t token,
                                        const char* path, const void* data,
                                        size_t size, uint64_t offset) {
    auto client = acquire_client(token);
    if (!client) return fail(token ? LABIOS_ERR_RELEASED : LABIOS_ERR_INVALID, "INVALID_HANDLE",
                             "client handle is null or released");
    if (!path || (!data && size)) return fail(LABIOS_ERR_INVALID, "INVALID_ARGUMENT",
                                               "path and input data are invalid");
    try {
        client->write(path, input_span(data, size), offset);
        clear_error();
        return LABIOS_OK;
    } catch (const labios::CompletionError& error) { return map_exception(error); }
    catch (const std::exception& error) {
        return fail(LABIOS_ERR_IO, "SUBMISSION_FAILED", error.what());
    }
}

extern "C" labios_error_t labios_read(labios_client_t token, const char* path,
                                       uint64_t offset, uint64_t size,
                                       void* buffer, size_t buffer_size,
                                       size_t* bytes_read) {
    if (bytes_read) *bytes_read = 0;
    auto client = acquire_client(token);
    if (!client) return fail(token ? LABIOS_ERR_RELEASED : LABIOS_ERR_INVALID, "INVALID_HANDLE",
                             "client handle is null or released");
    if (!path || !bytes_read) return fail(LABIOS_ERR_INVALID, "INVALID_ARGUMENT",
                                          "path and bytes_read are required");
    try {
        return copy_to_c_buffer(client->read(path, offset, size), buffer,
                                buffer_size, bytes_read);
    } catch (const labios::CompletionError& error) { return map_exception(error); }
    catch (const std::exception& error) {
        return fail(LABIOS_ERR_IO, "EXECUTION_FAILED", error.what());
    }
}

extern "C" labios_error_t labios_async_write(
    labios_client_t token, const char* path, const void* data, size_t size,
    uint64_t offset, labios_status_t* output) {
    if (output) *output = nullptr;
    auto client = acquire_client(token);
    if (!client) return fail(token ? LABIOS_ERR_RELEASED : LABIOS_ERR_INVALID, "INVALID_HANDLE",
                             "client handle is null or released");
    if (!path || !output || (!data && size)) {
        return fail(LABIOS_ERR_INVALID, "INVALID_ARGUMENT",
                    "path, data, and status output are invalid");
    }
    try {
        *output = register_status(client->async_write(path, input_span(data, size), offset));
        clear_error();
        return LABIOS_OK;
    } catch (const std::exception& error) {
        return fail(LABIOS_ERR_IO, "SUBMISSION_FAILED", error.what());
    }
}

extern "C" labios_error_t labios_async_read(
    labios_client_t token, const char* path, uint64_t offset, uint64_t size,
    labios_status_t* output) {
    if (output) *output = nullptr;
    auto client = acquire_client(token);
    if (!client) return fail(token ? LABIOS_ERR_RELEASED : LABIOS_ERR_INVALID, "INVALID_HANDLE",
                             "client handle is null or released");
    if (!path || !output) return fail(LABIOS_ERR_INVALID, "INVALID_ARGUMENT",
                                      "path and status output are required");
    try {
        *output = register_status(client->async_read(path, offset, size));
        clear_error();
        return LABIOS_OK;
    } catch (const std::exception& error) {
        return fail(LABIOS_ERR_IO, "SUBMISSION_FAILED", error.what());
    }
}

extern "C" labios_error_t labios_wait(labios_status_t token) {
    auto status = acquire_status(token);
    if (!status) return fail(token ? LABIOS_ERR_RELEASED : LABIOS_ERR_INVALID, "INVALID_HANDLE",
                             "status handle is null or released");
    try {
        (void)status->operation.wait();
        clear_error();
        return LABIOS_OK;
    } catch (const labios::CompletionError& error) { return map_exception(error); }
    catch (const std::exception& error) {
        return fail(LABIOS_ERR_IO, "EXECUTION_FAILED", error.what());
    }
}

extern "C" labios_error_t labios_wait_read(labios_status_t token,
                                            void* buffer, size_t buffer_size,
                                            size_t* bytes_read) {
    if (bytes_read) *bytes_read = 0;
    auto status = acquire_status(token);
    if (!status) return fail(token ? LABIOS_ERR_RELEASED : LABIOS_ERR_INVALID, "INVALID_HANDLE",
                             "status handle is null or released");
    if (!bytes_read) return fail(LABIOS_ERR_INVALID, "INVALID_ARGUMENT",
                                 "bytes_read is required");
    try {
        return copy_to_c_buffer(status->operation.read(
                                    std::chrono::milliseconds(30000)),
                                buffer, buffer_size, bytes_read);
    } catch (const labios::CompletionError& error) { return map_exception(error); }
    catch (const std::exception& error) {
        return fail(LABIOS_ERR_INVALID, "INVALID_OPERATION", error.what());
    }
}

extern "C" size_t labios_status_label_count(labios_status_t token) {
    const auto status = acquire_status(token);
    return status ? status->operation.label_ids().size() : 0;
}
extern "C" labios_error_t labios_status_label_id(labios_status_t token,
                                                  size_t index,
                                                  uint64_t* output) {
    if (output) *output = 0;
    const auto status = acquire_status(token);
    if (!status) return fail(token ? LABIOS_ERR_RELEASED : LABIOS_ERR_INVALID, "INVALID_HANDLE",
                             "status handle is null or released");
    if (!output || index >= status->operation.label_ids().size()) {
        return fail(LABIOS_ERR_INVALID, "INVALID_ARGUMENT",
                    "label output or index is invalid");
    }
    *output = status->operation.label_id(index);
    clear_error();
    return LABIOS_OK;
}

extern "C" labios_error_t labios_test_label(
    labios_status_t token, size_t index, labios_completion_result_t* output) {
    if (output) *output = {};
    const auto status = acquire_status(token);
    if (!status) return fail(token ? LABIOS_ERR_RELEASED : LABIOS_ERR_INVALID, "INVALID_HANDLE",
                             "status handle is null or released");
    if (!output) return fail(LABIOS_ERR_INVALID, "INVALID_ARGUMENT",
                             "completion output is required");
    try {
        const auto count = status->operation.label_ids().size();
        if ((count == 0 && index != 0) || (count != 0 && index >= count)) {
            return fail(LABIOS_ERR_INVALID, "INVALID_ARGUMENT",
                        "label index is outside the status operation");
        }
        const auto result = status->operation.test(index);
        if (!copy_completion(result, output)) {
            return fail(LABIOS_ERR_IO, "ALLOCATION_FAILED",
                        "unable to allocate completion result");
        }
        if (result.state == labios::CompletionState::Unknown)
            return fail(LABIOS_ERR_NOT_FOUND, result.category, result.error);
        if (result.state == labios::CompletionState::Cancelled)
            return fail(LABIOS_ERR_CANCELLED, result.category, result.error);
        if (result.state == labios::CompletionState::Failed)
            return fail(LABIOS_ERR_IO, result.category, result.error);
        clear_error();
        return LABIOS_OK;
    } catch (const std::exception& error) {
        return fail(LABIOS_ERR_PROTOCOL, "PROTOCOL_ERROR", error.what());
    }
}

extern "C" labios_error_t labios_test(
    labios_status_t token, labios_completion_result_t* output) {
    return labios_test_label(token, 0, output);
}

extern "C" labios_error_t labios_wait_all(labios_status_t token,
                                           uint64_t timeout_ms,
                                           labios_completion_list_t* output) {
    if (output) *output = {};
    const auto status = acquire_status(token);
    if (!status) return fail(token ? LABIOS_ERR_RELEASED : LABIOS_ERR_INVALID, "INVALID_HANDLE",
                             "status handle is null or released");
    if (!output) return fail(LABIOS_ERR_INVALID, "INVALID_ARGUMENT",
                             "completion output is required");
    try {
        const auto result = status->operation.wait_all(
            std::chrono::milliseconds(timeout_ms));
        const auto code = copy_wait_result(result, output);
        if (code == LABIOS_OK) clear_error();
        return code;
    } catch (const std::exception& error) {
        return fail(LABIOS_ERR_PROTOCOL, "PROTOCOL_ERROR", error.what());
    }
}

extern "C" labios_error_t labios_wait_for(labios_status_t token,
                                           uint64_t timeout_ms,
                                           labios_completion_list_t* output) {
    return labios_wait_all(token, timeout_ms, output);
}

extern "C" labios_error_t labios_wait_any(labios_status_t token,
                                           uint64_t timeout_ms,
                                           labios_completion_list_t* output) {
    if (output) *output = {};
    const auto status = acquire_status(token);
    if (!status) return fail(token ? LABIOS_ERR_RELEASED : LABIOS_ERR_INVALID, "INVALID_HANDLE",
                             "status handle is null or released");
    if (!output) return fail(LABIOS_ERR_INVALID, "INVALID_ARGUMENT",
                             "completion output is required");
    try {
        const auto result = status->operation.wait_any(
            std::chrono::milliseconds(timeout_ms));
        const auto code = copy_wait_result(result, output);
        if (code == LABIOS_OK) clear_error();
        return code;
    } catch (const std::exception& error) {
        return fail(LABIOS_ERR_PROTOCOL, "PROTOCOL_ERROR", error.what());
    }
}

extern "C" labios_error_t labios_cancel(labios_status_t token,
                                         labios_cancel_list_t* output) {
    if (output) *output = {};
    const auto status = acquire_status(token);
    if (!status) return fail(token ? LABIOS_ERR_RELEASED : LABIOS_ERR_INVALID, "INVALID_HANDLE",
                             "status handle is null or released");
    if (!output) return fail(LABIOS_ERR_INVALID, "INVALID_ARGUMENT",
                             "cancellation output is required");
    try {
        const auto results = status->operation.cancel();
        if (!results.empty()) {
            output->items = static_cast<labios_cancel_result_t*>(
                std::calloc(results.size(), sizeof(labios_cancel_result_t)));
            if (!output->items) return fail(LABIOS_ERR_IO, "ALLOCATION_FAILED",
                                             "unable to allocate cancel results");
        }
        output->count = results.size();
        labios_error_t code = LABIOS_OK;
        for (size_t i = 0; i < results.size(); ++i) {
            output->items[i].label_id = results[i].label_id;
            output->items[i].state = c_cancel_state(results[i].state);
            if (!copy_completion(results[i].completion,
                                 &output->items[i].completion)) {
                labios_cancel_list_release(output);
                return fail(LABIOS_ERR_IO, "ALLOCATION_FAILED",
                            "unable to allocate cancel result strings");
            }
            if (results[i].state == labios::CancellationState::TooLate)
                code = LABIOS_ERR_TOO_LATE;
            else if (results[i].state == labios::CancellationState::Unknown)
                code = LABIOS_ERR_NOT_FOUND;
        }
        if (code == LABIOS_OK) {
            clear_error();
        } else if (code == LABIOS_ERR_TOO_LATE) {
            (void)fail(code, "TOO_LATE", "execution already started");
        } else {
            (void)fail(code, "LOOKUP_FAILED", "label is unknown");
        }
        return code;
    } catch (const std::exception& error) {
        return fail(LABIOS_ERR_PROTOCOL, "PROTOCOL_ERROR", error.what());
    }
}

extern "C" labios_error_t labios_wait_read_alloc(labios_status_t token,
                                                  uint64_t timeout_ms,
                                                  labios_buffer_t* output) {
    if (output) *output = {};
    const auto status = acquire_status(token);
    if (!status) return fail(token ? LABIOS_ERR_RELEASED : LABIOS_ERR_INVALID, "INVALID_HANDLE",
                             "status handle is null or released");
    if (!output) return fail(LABIOS_ERR_INVALID, "INVALID_ARGUMENT",
                             "buffer output is required");
    try {
        const auto data = status->operation.read(std::chrono::milliseconds(timeout_ms));
        if (!data.empty()) {
            output->data = std::malloc(data.size());
            if (!output->data) return fail(LABIOS_ERR_IO, "ALLOCATION_FAILED",
                                            "unable to allocate result buffer");
            std::memcpy(output->data, data.data(), data.size());
        }
        output->size = data.size();
        clear_error();
        return LABIOS_OK;
    } catch (const labios::CompletionError& error) { return map_exception(error); }
    catch (const std::exception& error) {
        return fail(LABIOS_ERR_INVALID, "INVALID_OPERATION", error.what());
    }
}

extern "C" void labios_status_free(labios_status_t status) {
    if (!status) return;
    std::lock_guard lock(registry().mutex);
    registry().statuses.erase(status);
}
extern "C" void labios_status_release(labios_status_t* status) {
    if (!status) return;
    labios_status_free(*status);
    *status = nullptr;
}

extern "C" void labios_completion_result_release(
    labios_completion_result_t* result) {
    if (!result) return;
    std::free(result->category);
    std::free(result->message);
    std::free(result->data_key);
    std::free(result->park_reason);
    *result = {};
}
extern "C" void labios_completion_list_release(labios_completion_list_t* result) {
    if (!result) return;
    for (size_t i = 0; i < result->count; ++i)
        labios_completion_result_release(&result->items[i]);
    std::free(result->items);
    *result = {};
}
extern "C" void labios_cancel_list_release(labios_cancel_list_t* result) {
    if (!result) return;
    for (size_t i = 0; i < result->count; ++i)
        labios_completion_result_release(&result->items[i].completion);
    std::free(result->items);
    *result = {};
}
extern "C" void labios_buffer_release(labios_buffer_t* buffer) {
    if (!buffer) return;
    std::free(buffer->data);
    *buffer = {};
}

extern "C" labios_error_t labios_last_error(labios_error_info_t* output) {
    if (!output) return LABIOS_ERR_INVALID;
    *output = {};
    output->code = last_error.code;
    output->category = duplicate_string(last_error.category);
    output->message = duplicate_string(last_error.message);
    if (!output->category || !output->message) {
        labios_error_info_release(output);
        return LABIOS_ERR_IO;
    }
    return LABIOS_OK;
}
extern "C" void labios_error_info_release(labios_error_info_t* error) {
    if (!error) return;
    std::free(error->category);
    std::free(error->message);
    *error = {};
}
