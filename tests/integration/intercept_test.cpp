#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <numeric>
#include <sys/stat.h>
#include <unistd.h>

#include <string>
#include <vector>

TEST_CASE("Intercept write and read back", "[intercept]") {
    constexpr size_t SIZE = 64 * 1024;  // 64KB (>= min_label_size)
    std::vector<uint8_t> buf(SIZE);
    std::iota(buf.begin(), buf.end(), static_cast<uint8_t>(0));

    int fd = ::open("/labios/intercept_rw.bin", O_WRONLY | O_CREAT, 0644);
    REQUIRE(fd >= 0);
    ssize_t written = ::write(fd, buf.data(), SIZE);
    REQUIRE(written == static_cast<ssize_t>(SIZE));
    REQUIRE(::close(fd) == 0);

    fd = ::open("/labios/intercept_rw.bin", O_RDONLY);
    REQUIRE(fd >= 0);
    std::vector<uint8_t> rbuf(SIZE);
    ssize_t nread = ::read(fd, rbuf.data(), SIZE);
    REQUIRE(nread == static_cast<ssize_t>(SIZE));
    REQUIRE(::close(fd) == 0);

    REQUIRE(buf == rbuf);
}

TEST_CASE("Intercept lseek tracks offset", "[intercept]") {
    int fd = ::open("/labios/intercept_seek.bin", O_RDWR | O_CREAT, 0644);
    REQUIRE(fd >= 0);

    std::vector<uint8_t> data(1024 * 1024, 0xAA); // 1MB
    REQUIRE(::write(fd, data.data(), data.size()) == static_cast<ssize_t>(data.size()));

    off_t pos = ::lseek(fd, 0, SEEK_CUR);
    REQUIRE(pos == static_cast<off_t>(data.size()));

    pos = ::lseek(fd, 0, SEEK_SET);
    REQUIRE(pos == 0);

    REQUIRE(::close(fd) == 0);
}

TEST_CASE("Non-LABIOS paths pass through to real FS", "[intercept]") {
    int fd = ::open("/tmp/labios_passthrough_test.txt", O_WRONLY | O_CREAT, 0644);
    REQUIRE(fd >= 0);
    const char* msg = "hello";
    REQUIRE(::write(fd, msg, 5) == 5);
    REQUIRE(::close(fd) == 0);

    fd = ::open("/tmp/labios_passthrough_test.txt", O_RDONLY);
    REQUIRE(fd >= 0);
    char buf[5];
    REQUIRE(::read(fd, buf, 5) == 5);
    REQUIRE(std::memcmp(buf, msg, 5) == 0);
    REQUIRE(::close(fd) == 0);

    ::unlink("/tmp/labios_passthrough_test.txt");
}
