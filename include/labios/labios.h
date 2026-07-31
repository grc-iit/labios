/* labios.h — LABIOS 2.1 public C API */
#ifndef LABIOS_H
#define LABIOS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct labios_client* labios_client_t;
typedef struct labios_status* labios_status_t;

typedef enum {
    LABIOS_READ = 0, LABIOS_WRITE = 1, LABIOS_DELETE = 2, LABIOS_FLUSH = 3
} labios_label_type_t;

typedef enum {
    LABIOS_INTENT_NONE = 0, LABIOS_INTENT_CHECKPOINT = 1,
    LABIOS_INTENT_CACHE = 2, LABIOS_INTENT_TOOL_OUTPUT = 3,
    LABIOS_INTENT_FINAL_RESULT = 4, LABIOS_INTENT_INTERMEDIATE = 5,
    LABIOS_INTENT_SHARED_STATE = 6
} labios_intent_t;

#define LABIOS_FLAG_ASYNC (1 << 5)
#define LABIOS_FLAG_CACHED (1 << 3)
#define LABIOS_FLAG_HIGH_PRIO (1 << 6)

/* Existing numeric values are retained; additions are append-only. */
typedef enum {
    LABIOS_OK = 0,
    LABIOS_ERR_CONNECT = -1,
    LABIOS_ERR_TIMEOUT = -2,
    LABIOS_ERR_IO = -3,
    LABIOS_ERR_INVALID = -4,
    LABIOS_ERR_CANCELLED = -5,
    LABIOS_ERR_TOO_LATE = -6,
    LABIOS_ERR_NOT_FOUND = -7,
    LABIOS_ERR_RELEASED = -8,
    LABIOS_ERR_PROTOCOL = -9,
    LABIOS_ERR_BUFFER_TOO_SMALL = -10
} labios_error_t;

typedef enum {
    LABIOS_STATE_PENDING = 0,
    LABIOS_STATE_COMPLETE = 1,
    LABIOS_STATE_FAILED = 2,
    LABIOS_STATE_CANCELLED = 3,
    LABIOS_STATE_PARKED = 4,
    LABIOS_STATE_TIMEOUT = 5,
    LABIOS_STATE_UNKNOWN = 6
} labios_completion_state_t;

typedef enum {
    LABIOS_LIFECYCLE_SUBMITTED = 0,
    LABIOS_LIFECYCLE_ADMITTED = 1,
    LABIOS_LIFECYCLE_QUEUED = 2,
    LABIOS_LIFECYCLE_PARKED = 3,
    LABIOS_LIFECYCLE_SHUFFLED = 4,
    LABIOS_LIFECYCLE_SCHEDULED = 5,
    LABIOS_LIFECYCLE_EXECUTING = 6,
    LABIOS_LIFECYCLE_COMPLETED = 7,
    LABIOS_LIFECYCLE_FAILED = 8,
    LABIOS_LIFECYCLE_CANCELLED = 9,
    LABIOS_LIFECYCLE_UNKNOWN = 10
} labios_lifecycle_state_t;

typedef enum {
    LABIOS_CANCEL_CANCELLED = 0,
    LABIOS_CANCEL_TOO_LATE = 1,
    LABIOS_CANCEL_TERMINAL = 2,
    LABIOS_CANCEL_UNKNOWN = 3
} labios_cancel_state_t;

/* Strings and arrays in result objects are allocated by LABIOS and become
 * caller-owned. Release them with the corresponding release function. */
typedef struct {
    uint64_t label_id;
    labios_completion_state_t state;
    char* category;
    char* message;
    char* data_key;
    uint32_t observation_version;
    int worker_id;
    uint32_t attempt;
    uint64_t queued_us;
    uint64_t dispatched_us;
    uint64_t started_us;
    uint64_t completed_us;
    uint64_t queue_delay_us;
    uint64_t service_time_us;
    char* park_reason;
    uint64_t park_attempts;
    uint64_t next_retry_at_ms;
    labios_lifecycle_state_t lifecycle;
} labios_completion_result_t;

typedef struct {
    labios_completion_state_t state;
    labios_completion_result_t* items;
    size_t count;
} labios_completion_list_t;

typedef struct {
    uint64_t label_id;
    labios_cancel_state_t state;
    labios_completion_result_t completion;
} labios_cancel_result_t;

typedef struct {
    labios_cancel_result_t* items;
    size_t count;
} labios_cancel_list_t;

typedef struct {
    void* data;
    size_t size;
} labios_buffer_t;

typedef struct {
    labios_error_t code;
    char* category;
    char* message;
} labios_error_info_t;

labios_error_t labios_connect(const char* nats_url, const char* redis_host,
                              int redis_port, labios_client_t* out);
labios_error_t labios_connect_config(const char* config_path,
                                     labios_client_t* out);

/* Idempotent for NULL, released, or copied stale handles. Prefer the pointer
 * form, which also clears the caller's variable. */
void labios_disconnect(labios_client_t client);
void labios_disconnect_ref(labios_client_t* client);

labios_error_t labios_write(labios_client_t client, const char* filepath,
                            const void* data, size_t size, uint64_t offset);
labios_error_t labios_read(labios_client_t client, const char* filepath,
                           uint64_t offset, uint64_t size,
                           void* buf, size_t buf_size, size_t* bytes_read);

labios_error_t labios_async_write(labios_client_t client, const char* filepath,
                                  const void* data, size_t size, uint64_t offset,
                                  labios_status_t* out);
labios_error_t labios_async_read(labios_client_t client, const char* filepath,
                                 uint64_t offset, uint64_t size,
                                 labios_status_t* out);

/* Legacy blocking calls. Timeout is nonterminal and status remains reusable. */
labios_error_t labios_wait(labios_status_t status);
labios_error_t labios_wait_read(labios_status_t status, void* buf,
                                size_t buf_size, size_t* bytes_read);

/* Section 13 lifecycle API. */
size_t labios_status_label_count(labios_status_t status);
labios_error_t labios_status_label_id(labios_status_t status, size_t index,
                                      uint64_t* label_id);
/* labios_test inspects label index zero; labios_test_label selects a member. */
labios_error_t labios_test(labios_status_t status,
                           labios_completion_result_t* out);
labios_error_t labios_test_label(labios_status_t status, size_t index,
                                 labios_completion_result_t* out);
labios_error_t labios_wait_for(labios_status_t status, uint64_t timeout_ms,
                               labios_completion_list_t* out);
labios_error_t labios_wait_all(labios_status_t status, uint64_t timeout_ms,
                               labios_completion_list_t* out);
labios_error_t labios_wait_any(labios_status_t status, uint64_t timeout_ms,
                               labios_completion_list_t* out);
labios_error_t labios_cancel(labios_status_t status,
                             labios_cancel_list_t* out);
labios_error_t labios_wait_read_alloc(labios_status_t status,
                                      uint64_t timeout_ms,
                                      labios_buffer_t* out);

/* Idempotent status release. The pointer form also clears the caller variable. */
void labios_status_free(labios_status_t status);
void labios_status_release(labios_status_t* status);

void labios_completion_result_release(labios_completion_result_t* result);
void labios_completion_list_release(labios_completion_list_t* result);
void labios_cancel_list_release(labios_cancel_list_t* result);
void labios_buffer_release(labios_buffer_t* buffer);

/* Copies the current thread's most recent C API error into caller-owned output. */
labios_error_t labios_last_error(labios_error_info_t* out);
void labios_error_info_release(labios_error_info_t* error);

#ifdef __cplusplus
}
#endif
#endif
