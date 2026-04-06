#include <catch2/catch_test_macros.hpp>
#include <labios/backend/posix_backend.h>
#include <labios/backend/registry.h>

#include <cstring>
#include <filesystem>

static std::filesystem::path make_temp_dir() {
    auto dir = std::filesystem::temp_directory_path() / "labios_backend_test";
    std::filesystem::create_directories(dir);
    return dir;
}

TEST_CASE("PosixBackend put/get roundtrip", "[backend]") {
    auto tmp = make_temp_dir();
    labios::PosixBackend backend(tmp);

    const char* msg = "hello backend";
    auto data = std::as_bytes(std::span(msg, std::strlen(msg)));

    auto put_result = backend.put("test/roundtrip.dat", 0, data);
    REQUIRE(put_result.success);

    auto get_result = backend.get("test/roundtrip.dat", 0, std::strlen(msg));
    REQUIRE(get_result.success);
    REQUIRE(get_result.data.size() == std::strlen(msg));
    REQUIRE(std::memcmp(get_result.data.data(), msg, std::strlen(msg)) == 0);

    std::filesystem::remove_all(tmp);
}

TEST_CASE("PosixBackend put with offset", "[backend]") {
    auto tmp = make_temp_dir();
    labios::PosixBackend backend(tmp);

    const char* first = "AAAA";
    auto d1 = std::as_bytes(std::span(first, 4));
    backend.put("offset_test.dat", 0, d1);

    const char* second = "BB";
    auto d2 = std::as_bytes(std::span(second, 2));
    backend.put("offset_test.dat", 2, d2);

    auto result = backend.get("offset_test.dat", 0, 4);
    REQUIRE(result.success);
    REQUIRE(result.data.size() == 4);
    // First two bytes are 'A', next two overwritten by 'B'.
    REQUIRE(static_cast<char>(result.data[0]) == 'A');
    REQUIRE(static_cast<char>(result.data[1]) == 'A');
    REQUIRE(static_cast<char>(result.data[2]) == 'B');
    REQUIRE(static_cast<char>(result.data[3]) == 'B');

    std::filesystem::remove_all(tmp);
}

TEST_CASE("PosixBackend del", "[backend]") {
    auto tmp = make_temp_dir();
    labios::PosixBackend backend(tmp);

    const char* msg = "delete me";
    auto data = std::as_bytes(std::span(msg, std::strlen(msg)));
    backend.put("to_delete.dat", 0, data);

    auto del_result = backend.del("to_delete.dat");
    REQUIRE(del_result.success);

    auto get_result = backend.get("to_delete.dat", 0, 9);
    REQUIRE_FALSE(get_result.success);

    std::filesystem::remove_all(tmp);
}

TEST_CASE("PosixBackend get nonexistent file", "[backend]") {
    auto tmp = make_temp_dir();
    labios::PosixBackend backend(tmp);

    auto result = backend.get("no_such_file.dat", 0, 10);
    REQUIRE_FALSE(result.success);

    std::filesystem::remove_all(tmp);
}

TEST_CASE("PosixBackend scheme", "[backend]") {
    labios::PosixBackend backend("/tmp");
    REQUIRE(backend.scheme() == "file");
}

TEST_CASE("BackendRegistry register and resolve", "[backend][registry]") {
    labios::BackendRegistry registry;
    registry.register_backend(labios::PosixBackend("/tmp"));

    REQUIRE(registry.has_scheme("file"));
    REQUIRE(registry.resolve("file") != nullptr);
}

TEST_CASE("BackendRegistry resolve unknown scheme returns nullptr", "[backend][registry]") {
    labios::BackendRegistry registry;
    REQUIRE_FALSE(registry.has_scheme("s3"));
    REQUIRE(registry.resolve("s3") == nullptr);
}

TEST_CASE("BackendRegistry roundtrip through type-erased interface", "[backend][registry]") {
    auto tmp = make_temp_dir();
    labios::BackendRegistry registry;
    registry.register_backend(labios::PosixBackend(tmp));

    auto* backend = registry.resolve("file");
    REQUIRE(backend != nullptr);

    const char* msg = "registry roundtrip";
    auto data = std::as_bytes(std::span(msg, std::strlen(msg)));
    auto put_result = backend->put("registry_test.dat", 0, data);
    REQUIRE(put_result.success);

    auto get_result = backend->get("registry_test.dat", 0, std::strlen(msg));
    REQUIRE(get_result.success);
    REQUIRE(get_result.data.size() == std::strlen(msg));

    std::filesystem::remove_all(tmp);
}
