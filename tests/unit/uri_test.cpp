#include <catch2/catch_test_macros.hpp>
#include <labios/uri.h>

TEST_CASE("parse_uri with file scheme", "[uri]") {
    auto uri = labios::parse_uri("file:///data/output.dat");
    REQUIRE(uri.scheme == "file");
    REQUIRE(uri.authority.empty());
    REQUIRE(uri.path == "/data/output.dat");
    REQUIRE(uri.query.empty());
    REQUIRE_FALSE(uri.empty());
}

TEST_CASE("parse_uri with s3 scheme", "[uri]") {
    auto uri = labios::parse_uri("s3://bucket/key/path");
    REQUIRE(uri.scheme == "s3");
    REQUIRE(uri.authority == "bucket");
    REQUIRE(uri.path == "/key/path");
    REQUIRE(uri.query.empty());
}

TEST_CASE("parse_uri with vector scheme", "[uri]") {
    auto uri = labios::parse_uri("vector://collection/embeddings");
    REQUIRE(uri.scheme == "vector");
    REQUIRE(uri.authority == "collection");
    REQUIRE(uri.path == "/embeddings");
}

TEST_CASE("parse_uri with bare path defaults to file", "[uri]") {
    auto uri = labios::parse_uri("/bare/path");
    REQUIRE(uri.scheme == "file");
    REQUIRE(uri.authority.empty());
    REQUIRE(uri.path == "/bare/path");
}

TEST_CASE("parse_uri with empty string", "[uri]") {
    auto uri = labios::parse_uri("");
    REQUIRE(uri.empty());
}

TEST_CASE("parse_uri with query string", "[uri]") {
    auto uri = labios::parse_uri("s3://bucket/key?region=us-east-1");
    REQUIRE(uri.scheme == "s3");
    REQUIRE(uri.authority == "bucket");
    REQUIRE(uri.path == "/key");
    REQUIRE(uri.query == "region=us-east-1");
}

TEST_CASE("parse_uri bare path with query", "[uri]") {
    auto uri = labios::parse_uri("/data/file.dat?compress=true");
    REQUIRE(uri.scheme == "file");
    REQUIRE(uri.path == "/data/file.dat");
    REQUIRE(uri.query == "compress=true");
}

TEST_CASE("parse_uri authority only, no path", "[uri]") {
    auto uri = labios::parse_uri("kv://redis-host");
    REQUIRE(uri.scheme == "kv");
    REQUIRE(uri.authority == "redis-host");
    REQUIRE(uri.path.empty());
}

TEST_CASE("to_string roundtrip", "[uri]") {
    SECTION("file URI") {
        auto uri = labios::parse_uri("file:///data/output.dat");
        REQUIRE(labios::to_string(uri) == "file:///data/output.dat");
    }

    SECTION("s3 URI") {
        auto uri = labios::parse_uri("s3://bucket/key/path");
        REQUIRE(labios::to_string(uri) == "s3://bucket/key/path");
    }

    SECTION("URI with query") {
        auto uri = labios::parse_uri("s3://bucket/key?region=us-east-1");
        REQUIRE(labios::to_string(uri) == "s3://bucket/key?region=us-east-1");
    }

    SECTION("empty URI") {
        auto uri = labios::parse_uri("");
        REQUIRE(labios::to_string(uri).empty());
    }
}
