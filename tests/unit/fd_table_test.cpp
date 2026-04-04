#include <catch2/catch_test_macros.hpp>
#include <labios/adapter/fd_table.h>

#include <atomic>
#include <fcntl.h>
#include <thread>
#include <vector>

TEST_CASE("FdTable allocates valid fds", "[fd_table]") {
    labios::FdTable table;
    int fd = table.allocate("/test/file.bin", O_WRONLY | O_CREAT);
    REQUIRE(fd >= 0);
    REQUIRE(table.is_labios_fd(fd));
    REQUIRE(table.size() == 1);

    auto* state = table.get(fd);
    REQUIRE(state != nullptr);
    REQUIRE(state->filepath == "/test/file.bin");

    bool was_last = table.release(fd);
    REQUIRE(was_last);
    REQUIRE_FALSE(table.is_labios_fd(fd));
    REQUIRE(table.size() == 0);
}

TEST_CASE("FdTable duplicate shares state", "[fd_table]") {
    labios::FdTable table;
    int fd1 = table.allocate("/test/dup.bin", O_RDWR);
    REQUIRE(fd1 >= 0);

    int fd2 = table.duplicate(fd1);
    REQUIRE(fd2 >= 0);
    REQUIRE(fd2 != fd1);
    REQUIRE(table.is_labios_fd(fd2));
    REQUIRE(table.size() == 2);

    auto* s1 = table.get(fd1);
    auto* s2 = table.get(fd2);
    REQUIRE(s1 == s2);

    bool was_last = table.release(fd1);
    REQUIRE_FALSE(was_last);

    was_last = table.release(fd2);
    REQUIRE(was_last);
}

TEST_CASE("FdTable O_SYNC detection", "[fd_table]") {
    labios::FdTable table;
    int fd1 = table.allocate("/test/sync.bin", O_WRONLY | O_SYNC);
    auto* s1 = table.get(fd1);
    REQUIRE(s1->sync_mode == true);

    int fd2 = table.allocate("/test/async.bin", O_WRONLY);
    auto* s2 = table.get(fd2);
    REQUIRE(s2->sync_mode == false);

    table.release(fd1);
    table.release(fd2);
}

TEST_CASE("FdTable concurrent access", "[fd_table]") {
    labios::FdTable table;
    constexpr int num_threads = 8;
    constexpr int ops_per_thread = 100;

    std::atomic<int> success_count{0};
    std::atomic<int> lookup_failures{0};

    std::vector<std::thread> threads;
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&table, &success_count, &lookup_failures, t]() {
            for (int i = 0; i < ops_per_thread; ++i) {
                std::string path = "/test/t" + std::to_string(t)
                                 + "_" + std::to_string(i) + ".bin";
                int fd = table.allocate(path, O_WRONLY);
                if (fd >= 0) {
                    if (!table.is_labios_fd(fd)) {
                        lookup_failures.fetch_add(1, std::memory_order_relaxed);
                    }
                    table.release(fd);
                    success_count.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }
    for (auto& t : threads) t.join();

    REQUIRE(lookup_failures.load() == 0);
    REQUIRE(success_count.load() == num_threads * ops_per_thread);
    REQUIRE(table.size() == 0);
}

TEST_CASE("is_labios_fd returns false for unknown fds", "[fd_table]") {
    labios::FdTable table;
    REQUIRE_FALSE(table.is_labios_fd(0));
    REQUIRE_FALSE(table.is_labios_fd(1));
    REQUIRE_FALSE(table.is_labios_fd(999));
    REQUIRE_FALSE(table.is_labios_fd(-1));
}
