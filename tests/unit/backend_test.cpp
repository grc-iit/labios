#include <catch2/catch_test_macros.hpp>
#include <labios/backend/kv_backend.h>
#include <labios/backend/posix_backend.h>
#include <labios/backend/sqlite_backend.h>
#include <labios/backend/registry.h>
#include <labios/transport/redis.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>

static std::filesystem::path make_temp_dir() {
    auto dir = std::filesystem::temp_directory_path() / "labios_backend_test";
    std::filesystem::create_directories(dir);
    return dir;
}

static std::string redis_host() {
    const char* h = std::getenv("LABIOS_REDIS_HOST");
    return (h && h[0]) ? h : "localhost";
}

static int redis_port() {
    const char* val = std::getenv("LABIOS_REDIS_PORT");
    return (val && val[0]) ? std::stoi(val) : 6379;
}

// ---------------------------------------------------------------------------
// PosixBackend tests (label-based interface)
// ---------------------------------------------------------------------------

TEST_CASE("PosixBackend put/get roundtrip with label", "[backend]") {
    auto tmp = make_temp_dir();
    labios::PosixBackend backend(tmp);

    labios::LabelData label;
    label.id = 1;
    label.type = labios::LabelType::Write;
    label.dest_uri = "file:///test/roundtrip.dat";

    const char* msg = "hello backend";
    auto data = std::as_bytes(std::span(msg, std::strlen(msg)));
    auto put_result = backend.put(label, data);
    REQUIRE(put_result.success);

    labios::LabelData read_label;
    read_label.id = 2;
    read_label.type = labios::LabelType::Read;
    read_label.source_uri = "file:///test/roundtrip.dat";
    read_label.data_size = std::strlen(msg);

    auto get_result = backend.get(read_label);
    REQUIRE(get_result.success);
    REQUIRE(get_result.data.size() == std::strlen(msg));
    REQUIRE(std::memcmp(get_result.data.data(), msg, std::strlen(msg)) == 0);

    std::filesystem::remove_all(tmp);
}

TEST_CASE("PosixBackend put with offset via FilePath pointer", "[backend]") {
    auto tmp = make_temp_dir();
    labios::PosixBackend backend(tmp);

    labios::LabelData label1;
    label1.id = 1;
    label1.type = labios::LabelType::Write;
    label1.destination = labios::file_path("offset_test.dat");

    const char* first = "AAAA";
    auto d1 = std::as_bytes(std::span(first, 4));
    backend.put(label1, d1);

    labios::LabelData label2;
    label2.id = 2;
    label2.type = labios::LabelType::Write;
    label2.destination = labios::file_path("offset_test.dat", 2, 2);

    const char* second = "BB";
    auto d2 = std::as_bytes(std::span(second, 2));
    backend.put(label2, d2);

    labios::LabelData read_label;
    read_label.id = 3;
    read_label.type = labios::LabelType::Read;
    read_label.source = labios::file_path("offset_test.dat");
    read_label.data_size = 4;

    auto result = backend.get(read_label);
    REQUIRE(result.success);
    REQUIRE(result.data.size() == 4);
    REQUIRE(static_cast<char>(result.data[0]) == 'A');
    REQUIRE(static_cast<char>(result.data[1]) == 'A');
    REQUIRE(static_cast<char>(result.data[2]) == 'B');
    REQUIRE(static_cast<char>(result.data[3]) == 'B');

    std::filesystem::remove_all(tmp);
}

TEST_CASE("PosixBackend del", "[backend]") {
    auto tmp = make_temp_dir();
    labios::PosixBackend backend(tmp);

    labios::LabelData write_label;
    write_label.id = 1;
    write_label.type = labios::LabelType::Write;
    write_label.dest_uri = "file:///to_delete.dat";

    const char* msg = "delete me";
    auto data = std::as_bytes(std::span(msg, std::strlen(msg)));
    backend.put(write_label, data);

    labios::LabelData del_label;
    del_label.id = 2;
    del_label.type = labios::LabelType::Delete;
    del_label.dest_uri = "file:///to_delete.dat";

    auto del_result = backend.del(del_label);
    REQUIRE(del_result.success);

    labios::LabelData read_label;
    read_label.id = 3;
    read_label.type = labios::LabelType::Read;
    read_label.source_uri = "file:///to_delete.dat";
    read_label.data_size = 9;

    auto get_result = backend.get(read_label);
    REQUIRE_FALSE(get_result.success);

    std::filesystem::remove_all(tmp);
}

TEST_CASE("PosixBackend get nonexistent file", "[backend]") {
    auto tmp = make_temp_dir();
    labios::PosixBackend backend(tmp);

    labios::LabelData label;
    label.id = 1;
    label.source_uri = "file:///no_such_file.dat";
    label.data_size = 10;

    auto result = backend.get(label);
    REQUIRE_FALSE(result.success);

    std::filesystem::remove_all(tmp);
}

TEST_CASE("PosixBackend query returns unsupported", "[backend]") {
    labios::PosixBackend backend("/tmp");
    labios::LabelData label;
    auto result = backend.query(label);
    REQUIRE_FALSE(result.success);
}

TEST_CASE("PosixBackend scheme", "[backend]") {
    labios::PosixBackend backend("/tmp");
    REQUIRE(backend.scheme() == "file");
}

TEST_CASE("PosixBackend keeps FilePath writes inside storage root", "[backend]") {
    auto tmp = make_temp_dir();
    auto outside = std::filesystem::temp_directory_path() / "labios_escape_target.dat";
    std::filesystem::remove(outside);

    labios::PosixBackend backend(tmp);

    labios::LabelData label;
    label.id = 1;
    label.type = labios::LabelType::Write;
    label.destination = labios::file_path(outside.string());

    const char* msg = "sandboxed";
    auto data = std::as_bytes(std::span(msg, std::strlen(msg)));
    auto put_result = backend.put(label, data);
    REQUIRE(put_result.success);

    REQUIRE_FALSE(std::filesystem::exists(outside));
    auto expected = tmp / outside.relative_path();
    REQUIRE(std::filesystem::exists(expected));

    std::filesystem::remove_all(tmp);
    std::filesystem::remove(outside);
}

// ---------------------------------------------------------------------------
// SQLiteBackend tests
// ---------------------------------------------------------------------------

TEST_CASE("SQLiteBackend put/get/del roundtrip", "[backend][sqlite]") {
    auto db_path = std::filesystem::temp_directory_path() / "labios_test.db";
    std::filesystem::remove(db_path);
    labios::SQLiteBackend backend(db_path.string());

    labios::LabelData label;
    label.id = 1;
    label.dest_uri = "sqlite:///memories/test-key";
    label.intent = labios::Intent::ReasoningTrace;
    label.isolation = labios::Isolation::Agent;
    label.priority = 5;

    std::string value = "test data for SQLite backend";
    auto data = std::as_bytes(std::span(value));
    auto put_result = backend.put(label, data);
    REQUIRE(put_result.success);

    labios::LabelData get_label;
    get_label.source_uri = "sqlite:///memories/test-key";
    auto get_result = backend.get(get_label);
    REQUIRE(get_result.success);
    REQUIRE(get_result.data.size() == value.size());

    labios::LabelData del_label;
    del_label.dest_uri = "sqlite:///memories/test-key";
    auto del_result = backend.del(del_label);
    REQUIRE(del_result.success);

    auto get_after_del = backend.get(get_label);
    REQUIRE_FALSE(get_after_del.success);

    std::filesystem::remove(db_path);
}

TEST_CASE("SQLiteBackend query by intent", "[backend][sqlite]") {
    auto db_path = std::filesystem::temp_directory_path() / "labios_query_test.db";
    std::filesystem::remove(db_path);
    labios::SQLiteBackend backend(db_path.string());

    // Insert two entries with different intents.
    labios::LabelData label1;
    label1.id = 1;
    label1.dest_uri = "sqlite:///memories/trace-1";
    label1.intent = labios::Intent::ReasoningTrace;
    label1.isolation = labios::Isolation::Agent;
    label1.priority = 5;

    std::string val1 = "reasoning trace data";
    backend.put(label1, std::as_bytes(std::span(val1)));

    labios::LabelData label2;
    label2.id = 2;
    label2.dest_uri = "sqlite:///memories/checkpoint-1";
    label2.intent = labios::Intent::Checkpoint;
    label2.isolation = labios::Isolation::Agent;
    label2.priority = 3;

    std::string val2 = "checkpoint data";
    backend.put(label2, std::as_bytes(std::span(val2)));

    // Query for ReasoningTrace intent (enum value 10).
    labios::LabelData query_label;
    query_label.source_uri = "sqlite:///memories?intent=10";
    auto query_result = backend.query(query_label);
    REQUIRE(query_result.success);
    REQUIRE(query_result.json_data.find("trace-1") != std::string::npos);
    REQUIRE(query_result.json_data.find("checkpoint-1") == std::string::npos);

    std::filesystem::remove(db_path);
}

TEST_CASE("SQLiteBackend scheme", "[backend][sqlite]") {
    auto db_path = std::filesystem::temp_directory_path() / "labios_scheme_test.db";
    std::filesystem::remove(db_path);
    labios::SQLiteBackend backend(db_path.string());
    REQUIRE(backend.scheme() == "sqlite");
    std::filesystem::remove(db_path);
}

TEST_CASE("SQLiteBackend preserves zero-length blobs", "[backend][sqlite]") {
    auto db_path = std::filesystem::temp_directory_path() / "labios_empty_blob.db";
    std::filesystem::remove(db_path);
    labios::SQLiteBackend backend(db_path.string());

    labios::LabelData put_label;
    put_label.dest_uri = "sqlite:///memories/empty";

    std::vector<std::byte> empty;
    auto put_result = backend.put(put_label, empty);
    REQUIRE(put_result.success);

    labios::LabelData get_label;
    get_label.source_uri = "sqlite:///memories/empty";
    auto get_result = backend.get(get_label);
    REQUIRE(get_result.success);
    REQUIRE(get_result.data.empty());

    std::filesystem::remove(db_path);
}

// ---------------------------------------------------------------------------
// KVBackend tests
// ---------------------------------------------------------------------------

TEST_CASE("KVBackend preserves zero-length values", "[backend][kv]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::KVBackend backend(redis, "labios:kv:test:");

    labios::LabelData put_label;
    put_label.dest_uri = "kv://project/empty-value";

    std::vector<std::byte> empty;
    auto put_result = backend.put(put_label, empty);
    REQUIRE(put_result.success);

    labios::LabelData get_label;
    get_label.source_uri = "kv://project/empty-value";
    auto get_result = backend.get(get_label);
    REQUIRE(get_result.success);
    REQUIRE(get_result.data.empty());

    auto del_result = backend.del(put_label);
    REQUIRE(del_result.success);
}

// ---------------------------------------------------------------------------
// BackendRegistry tests (updated for label-based interface)
// ---------------------------------------------------------------------------

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

    labios::LabelData write_label;
    write_label.id = 1;
    write_label.type = labios::LabelType::Write;
    write_label.dest_uri = "file:///registry_test.dat";

    const char* msg = "registry roundtrip";
    auto data = std::as_bytes(std::span(msg, std::strlen(msg)));
    auto put_result = backend->put(write_label, data);
    REQUIRE(put_result.success);

    labios::LabelData read_label;
    read_label.id = 2;
    read_label.type = labios::LabelType::Read;
    read_label.source_uri = "file:///registry_test.dat";
    read_label.data_size = std::strlen(msg);

    auto get_result = backend->get(read_label);
    REQUIRE(get_result.success);
    REQUIRE(get_result.data.size() == std::strlen(msg));

    std::filesystem::remove_all(tmp);
}

TEST_CASE("BackendRegistry multi-scheme resolution", "[backend][registry]") {
    auto tmp = make_temp_dir();
    auto db_path = std::filesystem::temp_directory_path() / "labios_registry_multi.db";
    std::filesystem::remove(db_path);

    labios::BackendRegistry registry;
    registry.register_backend(labios::PosixBackend(tmp));
    registry.register_backend(labios::SQLiteBackend(db_path.string()));

    REQUIRE(registry.has_scheme("file"));
    REQUIRE(registry.has_scheme("sqlite"));
    REQUIRE_FALSE(registry.has_scheme("kv"));

    std::filesystem::remove_all(tmp);
    std::filesystem::remove(db_path);
}
