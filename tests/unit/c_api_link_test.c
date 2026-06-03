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

    labios_status_free(NULL);
    labios_disconnect(NULL);
    return 0;
}
