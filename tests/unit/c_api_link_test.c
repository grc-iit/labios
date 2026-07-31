#include <labios/labios.h>

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

int main(void) {
    labios_client_t client = NULL;
    labios_status_t status = NULL;
    char buf[8] = {0};
    size_t bytes_read = 99;

    assert(labios_connect(NULL, "localhost", 6379, &client) == LABIOS_ERR_INVALID);
    assert(client == NULL);
    assert(labios_connect("nats://localhost:4222", NULL, 6379, &client) == LABIOS_ERR_INVALID);
    assert(client == NULL);
    assert(labios_connect("nats://localhost:4222", "localhost", 0, &client) == LABIOS_ERR_INVALID);
    assert(client == NULL);
    assert(labios_connect_config(NULL, &client) == LABIOS_ERR_INVALID);
    assert(client == NULL);

    assert(labios_write(NULL, "/tmp/data", buf, sizeof(buf), 0) == LABIOS_ERR_INVALID);
    assert(labios_read(NULL, "/tmp/data", 0, sizeof(buf), buf, sizeof(buf), &bytes_read)
           == LABIOS_ERR_INVALID);
    assert(bytes_read == 0);
    assert(labios_async_write(NULL, "/tmp/data", buf, sizeof(buf), 0, &status)
           == LABIOS_ERR_INVALID);
    assert(status == NULL);
    assert(labios_async_read(NULL, "/tmp/data", 0, sizeof(buf), &status)
           == LABIOS_ERR_INVALID);
    assert(status == NULL);
    assert(labios_wait(NULL) == LABIOS_ERR_INVALID);
    assert(labios_wait_read(NULL, buf, sizeof(buf), &bytes_read) == LABIOS_ERR_INVALID);
    assert(bytes_read == 0);

    labios_completion_result_t completion = {0};
    labios_completion_list_t completions = {0};
    labios_cancel_list_t cancellations = {0};
    labios_buffer_t buffer = {0};
    labios_error_info_t error = {0};
    assert(labios_test(NULL, &completion) == LABIOS_ERR_INVALID);
    assert(labios_test_label(NULL, 0, &completion) == LABIOS_ERR_INVALID);
    assert(labios_wait_for(NULL, 0, &completions) == LABIOS_ERR_INVALID);
    assert(labios_wait_all(NULL, 0, &completions) == LABIOS_ERR_INVALID);
    assert(labios_wait_any(NULL, 0, &completions) == LABIOS_ERR_INVALID);
    assert(labios_cancel(NULL, &cancellations) == LABIOS_ERR_INVALID);
    labios_completion_result_release(&completion);
    labios_completion_result_release(&completion);
    labios_completion_list_release(&completions);
    labios_cancel_list_release(&cancellations);
    labios_buffer_release(&buffer);
    assert(labios_last_error(&error) == LABIOS_OK);
    labios_error_info_release(&error);
    labios_error_info_release(&error);

    /* Registry validation never dereferences stale or foreign token pointers. */
    labios_status_t stale_status = (labios_status_t)(uintptr_t)1;
    labios_client_t stale_client = (labios_client_t)(uintptr_t)1;
    labios_status_free(stale_status);
    labios_status_free(stale_status);
    labios_disconnect(stale_client);
    labios_disconnect(stale_client);
    assert(labios_wait(stale_status) == LABIOS_ERR_RELEASED);
    assert(labios_write(stale_client, "/tmp/data", buf, sizeof(buf), 0)
           == LABIOS_ERR_RELEASED);
    labios_status_release(&stale_status);
    labios_disconnect_ref(&stale_client);
    assert(stale_status == NULL);
    assert(stale_client == NULL);

    labios_status_free(NULL);
    labios_disconnect(NULL);
    return 0;
}
