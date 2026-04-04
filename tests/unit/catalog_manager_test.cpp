#include <catch2/catch_test_macros.hpp>
#include <labios/catalog_manager.h>
#include <labios/transport/redis.h>
#include <cstdlib>
#include <fcntl.h>

static std::string redis_host() {
    const char* h = std::getenv("LABIOS_REDIS_HOST");
    return (h && h[0]) ? h : "localhost";
}

static int redis_port() {
    const char* val = std::getenv("LABIOS_REDIS_PORT");
    return (val && val[0]) ? std::stoi(val) : 6379;
}

TEST_CASE("File metadata tracks writes", "[catalog_manager]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::CatalogManager catalog(redis);

    catalog.track_open("/test/meta.bin", O_CREAT | O_WRONLY);
    auto info = catalog.get_file_info("/test/meta.bin");
    REQUIRE(info.has_value());
    REQUIRE(info->exists == true);
    REQUIRE(info->size == 0);

    catalog.track_write("/test/meta.bin", 0, 1024);
    info = catalog.get_file_info("/test/meta.bin");
    REQUIRE(info->size == 1024);

    catalog.track_write("/test/meta.bin", 1024, 2048);
    info = catalog.get_file_info("/test/meta.bin");
    REQUIRE(info->size == 3072);

    catalog.track_write("/test/meta.bin", 0, 512);
    info = catalog.get_file_info("/test/meta.bin");
    REQUIRE(info->size == 3072);
}

TEST_CASE("File metadata tracks unlink", "[catalog_manager]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::CatalogManager catalog(redis);

    catalog.track_open("/test/del.bin", O_CREAT | O_WRONLY);
    catalog.track_write("/test/del.bin", 0, 100);
    catalog.track_unlink("/test/del.bin");

    auto info = catalog.get_file_info("/test/del.bin");
    REQUIRE(info.has_value());
    REQUIRE(info->exists == false);
}

TEST_CASE("File metadata tracks truncate", "[catalog_manager]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::CatalogManager catalog(redis);

    catalog.track_open("/test/trunc.bin", O_CREAT | O_WRONLY);
    catalog.track_write("/test/trunc.bin", 0, 10000);
    catalog.track_truncate("/test/trunc.bin", 5000);

    auto info = catalog.get_file_info("/test/trunc.bin");
    REQUIRE(info->size == 5000);
}

TEST_CASE("Open with O_TRUNC resets file size", "[catalog_manager]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::CatalogManager catalog(redis);

    catalog.track_open("/test/trunc_on_open.bin", O_CREAT | O_WRONLY);
    catalog.track_write("/test/trunc_on_open.bin", 0, 2048);
    catalog.track_open("/test/trunc_on_open.bin", O_TRUNC | O_WRONLY);

    auto info = catalog.get_file_info("/test/trunc_on_open.bin");
    REQUIRE(info.has_value());
    REQUIRE(info->exists == true);
    REQUIRE(info->size == 0);
}

TEST_CASE("get_file_info returns nullopt for unknown file", "[catalog_manager]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::CatalogManager catalog(redis);

    auto info = catalog.get_file_info("/nonexistent/file.bin");
    REQUIRE_FALSE(info.has_value());
}

TEST_CASE("Catalog stores label flags as attributes", "[catalog_manager]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::CatalogManager catalog(redis);

    labios::LabelData label;
    label.id = 123456;
    label.type = labios::LabelType::Write;
    label.operation = "write";
    label.flags = labios::LabelFlags::Queued;
    label.priority = 3;
    label.app_id = 99;

    catalog.create(label);
    REQUIRE(catalog.get_flags(label.id) == labios::LabelFlags::Queued);

    catalog.set_flags(label.id,
                      labios::LabelFlags::Queued | labios::LabelFlags::Scheduled);
    REQUIRE(catalog.get_flags(label.id)
            == (labios::LabelFlags::Queued | labios::LabelFlags::Scheduled));
}

TEST_CASE("Catalog stores label error details", "[catalog_manager]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::CatalogManager catalog(redis);

    catalog.create(98765, 7, labios::LabelType::Read);
    catalog.set_error(98765, "data not found");

    auto error = catalog.get_error(98765);
    REQUIRE(error.has_value());
    REQUIRE(*error == "data not found");
}
