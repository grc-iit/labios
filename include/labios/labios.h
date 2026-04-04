/* labios.h — LABIOS 2.0 Public C API
 * US Patent 11,630,834 B2, NSF Award #2331480
 *
 * This header provides a C-compatible interface for language bindings
 * (Python, Rust, Go) and agent frameworks. For C++ applications, prefer
 * the labios::Client class in <labios/client.h>.
 */
#ifndef LABIOS_H
#define LABIOS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handle types */
typedef struct labios_client* labios_client_t;
typedef struct labios_status* labios_status_t;

/* Label types matching the paper Section 3.2.1 */
typedef enum {
    LABIOS_READ   = 0,
    LABIOS_WRITE  = 1,
    LABIOS_DELETE = 2,
    LABIOS_FLUSH  = 3,
} labios_label_type_t;

/* Intent tags (LABIOS 2.0 agent extensions) */
typedef enum {
    LABIOS_INTENT_NONE         = 0,
    LABIOS_INTENT_CHECKPOINT   = 1,
    LABIOS_INTENT_CACHE        = 2,
    LABIOS_INTENT_TOOL_OUTPUT  = 3,
    LABIOS_INTENT_FINAL_RESULT = 4,
    LABIOS_INTENT_INTERMEDIATE = 5,
    LABIOS_INTENT_SHARED_STATE = 6,
} labios_intent_t;

/* Flags (bitfield) */
#define LABIOS_FLAG_ASYNC      (1 << 5)
#define LABIOS_FLAG_CACHED     (1 << 3)
#define LABIOS_FLAG_HIGH_PRIO  (1 << 6)

/* Error codes */
typedef enum {
    LABIOS_OK           = 0,
    LABIOS_ERR_CONNECT  = -1,
    LABIOS_ERR_TIMEOUT  = -2,
    LABIOS_ERR_IO       = -3,
    LABIOS_ERR_INVALID  = -4,
} labios_error_t;

/* --- Connection --- */

/** Connect to a LABIOS cluster.
 *  @param nats_url  NATS server URL (e.g., "nats://localhost:4222")
 *  @param redis_host  Redis/DragonflyDB host
 *  @param redis_port  Redis/DragonflyDB port
 *  @param out  Receives the client handle on success
 *  @return LABIOS_OK on success, error code on failure
 */
labios_error_t labios_connect(const char* nats_url,
                              const char* redis_host,
                              int redis_port,
                              labios_client_t* out);

/** Connect using a TOML config file. */
labios_error_t labios_connect_config(const char* config_path,
                                     labios_client_t* out);

/** Disconnect and free resources. */
void labios_disconnect(labios_client_t client);

/* --- Synchronous I/O --- */

/** Synchronous write. Blocks until data is persisted. */
labios_error_t labios_write(labios_client_t client,
                            const char* filepath,
                            const void* data, size_t size,
                            uint64_t offset);

/** Synchronous read. Blocks until data is available.
 *  @param buf  Output buffer (caller-allocated)
 *  @param buf_size  Size of output buffer
 *  @param bytes_read  Receives actual bytes read
 */
labios_error_t labios_read(labios_client_t client,
                           const char* filepath,
                           uint64_t offset, uint64_t size,
                           void* buf, size_t buf_size,
                           size_t* bytes_read);

/* --- Asynchronous I/O (paper Figure 4) --- */

/** Publish a write asynchronously. Returns immediately.
 *  Use labios_wait() to block until complete.
 */
labios_error_t labios_async_write(labios_client_t client,
                                  const char* filepath,
                                  const void* data, size_t size,
                                  uint64_t offset,
                                  labios_status_t* out);

/** Publish a read asynchronously. Returns immediately.
 *  Use labios_wait_read() to block and retrieve data.
 */
labios_error_t labios_async_read(labios_client_t client,
                                 const char* filepath,
                                 uint64_t offset, uint64_t size,
                                 labios_status_t* out);

/** Block until an async operation completes. */
labios_error_t labios_wait(labios_status_t status);

/** Block until an async read completes and copy data to buffer. */
labios_error_t labios_wait_read(labios_status_t status,
                                void* buf, size_t buf_size,
                                size_t* bytes_read);

/** Free an async status handle. */
void labios_status_free(labios_status_t status);

#ifdef __cplusplus
}
#endif

#endif /* LABIOS_H */
