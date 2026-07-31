#include <labios/labios.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    labios_client_t client = NULL;
    labios_status_t status = NULL;
    const char payload[] = "Label I/O from C";

    labios_error_t code = labios_connect(
        "nats://localhost:4222", "localhost", 6379, &client);
    if (code != LABIOS_OK) goto error;

    code = labios_async_write(client, "/examples/c-golden.bin",
                              payload, sizeof(payload), 0, &status);
    if (code != LABIOS_OK) goto error;

    /* Status owns its operation session; client release is legal before wait. */
    labios_disconnect_ref(&client);

    labios_completion_list_t results = {0};
    code = labios_wait_for(status, 1, &results);
    if (code == LABIOS_ERR_TIMEOUT) {
        labios_completion_list_release(&results);
        code = labios_wait_for(status, 30000, &results);
    }
    if (code != LABIOS_OK) {
        labios_cancel_list_t cancellations = {0};
        (void)labios_cancel(status, &cancellations);
        labios_cancel_list_release(&cancellations);
    }
    labios_completion_list_release(&results);
    labios_status_release(&status);
    return code == LABIOS_OK ? 0 : 1;

error: {
        labios_error_info_t info = {0};
        if (labios_last_error(&info) == LABIOS_OK) {
            fprintf(stderr, "%s: %s\n", info.category, info.message);
        }
        labios_error_info_release(&info);
        labios_status_release(&status);
        labios_disconnect_ref(&client);
        return 1;
    }
}
