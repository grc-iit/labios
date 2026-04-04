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

TEST_CASE("get_file_info returns nullopt for unknown file", "[catalog_manager]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::CatalogManager catalog(redis);

    auto info = catalog.get_file_info("/nonexistent/file.bin");
    REQUIRE_FALSE(info.has_value());
}
