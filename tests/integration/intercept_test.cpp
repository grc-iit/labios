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

TEST_CASE("Intercept mixed I/O: pwrite, pread, stat, fstat, fsync, unlink",
          "[intercept]") {
    constexpr size_t SIZE = 128 * 1024;  // 128KB
    std::vector<uint8_t> buf(SIZE, 0x42);

    int fd = ::open("/labios/intercept_mixed.bin", O_RDWR | O_CREAT, 0644);
    REQUIRE(fd >= 0);

    // pwrite at offset 4096
    ssize_t pw = ::pwrite(fd, buf.data(), SIZE, 4096);
    REQUIRE(pw == static_cast<ssize_t>(SIZE));

    // fsync
    REQUIRE(::fsync(fd) == 0);

    // fstat
    struct stat st{};
    REQUIRE(::fstat(fd, &st) == 0);
    REQUIRE(st.st_size >= static_cast<off_t>(4096 + SIZE));

    // pread back at same offset
    std::vector<uint8_t> rbuf(SIZE);
    ssize_t pr = ::pread(fd, rbuf.data(), SIZE, 4096);
    REQUIRE(pr == static_cast<ssize_t>(SIZE));
    REQUIRE(buf == rbuf);

    REQUIRE(::close(fd) == 0);

    // stat on path
    struct stat st2{};
    REQUIRE(::stat("/labios/intercept_mixed.bin", &st2) == 0);
    REQUIRE(st2.st_size >= static_cast<off_t>(4096 + SIZE));

    // unlink
    REQUIRE(::unlink("/labios/intercept_mixed.bin") == 0);

    // After unlink, stat should fail
    struct stat st3{};
    REQUIRE(::stat("/labios/intercept_mixed.bin", &st3) == -1);
}

TEST_CASE("Intercept 10MB write produces split labels", "[intercept]") {
    constexpr size_t SIZE = 10 * 1024 * 1024;  // 10MB, max_label_size=1MB -> 10 labels
    std::vector<uint8_t> buf(SIZE);
    std::iota(buf.begin(), buf.end(), static_cast<uint8_t>(0));

    int fd = ::open("/labios/intercept_split.bin", O_WRONLY | O_CREAT, 0644);
    REQUIRE(fd >= 0);
    ssize_t written = ::write(fd, buf.data(), SIZE);
    REQUIRE(written == static_cast<ssize_t>(SIZE));
    REQUIRE(::close(fd) == 0);

    // Read back and verify
    fd = ::open("/labios/intercept_split.bin", O_RDONLY);
    REQUIRE(fd >= 0);
    std::vector<uint8_t> rbuf(SIZE);
    ssize_t nread = ::read(fd, rbuf.data(), SIZE);
    REQUIRE(nread == static_cast<ssize_t>(SIZE));
    REQUIRE(::close(fd) == 0);

    REQUIRE(buf == rbuf);
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
