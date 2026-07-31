#include <labios/labios.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

namespace {

labios_client_t connect_client() {
    labios_client_t client = nullptr;
    const char* nats = std::getenv("LABIOS_NATS_URL");
    const char* redis = std::getenv("LABIOS_REDIS_HOST");
    const char* port = std::getenv("LABIOS_REDIS_PORT");
    REQUIRE(labios_connect(nats ? nats : "nats://localhost:4222",
                           redis ? redis : "localhost",
                           port ? std::stoi(port) : 6379, &client) == LABIOS_OK);
    return client;
}

labios_status_t submit(labios_client_t client, std::string path,
                       size_t size = 4096) {
    std::vector<std::byte> data(size, std::byte{0x5a});
    labios_status_t status = nullptr;
    REQUIRE(labios_async_write(client, path.c_str(), data.data(), data.size(),
                               0, &status) == LABIOS_OK);
    return status;
}

} // namespace

TEST_CASE("C status survives client destruction", "[live][api-lifetime]") {
    auto client = connect_client();
    auto status = submit(client, "/api-lifetime/client-destruction.bin");
    labios_disconnect_ref(&client);
    REQUIRE(client == nullptr);

    labios_completion_list_t result{};
    const auto code = labios_wait_for(status, 30000, &result);
    CHECK((code == LABIOS_OK || code == LABIOS_ERR_IO));
    REQUIRE(result.count > 0);
    CHECK((result.items[0].lifecycle == LABIOS_LIFECYCLE_COMPLETED ||
           result.items[0].lifecycle == LABIOS_LIFECYCLE_FAILED));
    labios_completion_list_release(&result);
    labios_status_release(&status);
}

TEST_CASE("C timeout leaves status reusable", "[live][api-lifetime]") {
    auto client = connect_client();
    auto status = submit(client, "/api-lifetime/timeout-reuse.bin", 1024 * 1024);

    labios_completion_list_t first{};
    const auto first_code = labios_wait_for(status, 0, &first);
    CHECK((first_code == LABIOS_ERR_TIMEOUT || first_code == LABIOS_OK));
    labios_completion_list_release(&first);

    labios_completion_list_t second{};
    const auto second_code = labios_wait_for(status, 30000, &second);
    CHECK((second_code == LABIOS_OK || second_code == LABIOS_ERR_IO));
    labios_completion_list_release(&second);
    labios_status_release(&status);
    labios_disconnect_ref(&client);
}

TEST_CASE("C concurrent wait cancel and release are memory safe",
          "[live][api-lifetime]") {
    auto client = connect_client();
    auto status = submit(client, "/api-lifetime/concurrent.bin", 4 * 1024 * 1024);
    const auto stale = status;
    std::atomic<bool> start{false};
    std::atomic<int> wait_code{LABIOS_ERR_PROTOCOL};
    std::atomic<int> cancel_code{LABIOS_ERR_PROTOCOL};

    std::jthread waiter([&] {
        while (!start.load()) std::this_thread::yield();
        labios_completion_list_t result{};
        wait_code.store(labios_wait_for(stale, 30000, &result));
        labios_completion_list_release(&result);
    });
    std::jthread canceller([&] {
        while (!start.load()) std::this_thread::yield();
        labios_cancel_list_t result{};
        cancel_code.store(labios_cancel(stale, &result));
        labios_cancel_list_release(&result);
    });
    start.store(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    labios_status_release(&status);
    waiter.join();
    canceller.join();

    CHECK((wait_code.load() == LABIOS_OK ||
           wait_code.load() == LABIOS_ERR_CANCELLED ||
           wait_code.load() == LABIOS_ERR_RELEASED ||
           wait_code.load() == LABIOS_ERR_IO));
    CHECK((cancel_code.load() == LABIOS_OK ||
           cancel_code.load() == LABIOS_ERR_TOO_LATE ||
           cancel_code.load() == LABIOS_ERR_RELEASED));
    labios_status_free(stale);
    labios_status_free(stale);
    CHECK(labios_wait(stale) == LABIOS_ERR_RELEASED);
    labios_disconnect_ref(&client);
}
