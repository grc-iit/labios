#include <labios/elastic/docker_client.h>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("DockerClient satisfies ContainerRuntime concept", "[docker]") {
    static_assert(labios::elastic::ContainerRuntime<labios::elastic::DockerClient>);
}

TEST_CASE("MockRuntime satisfies ContainerRuntime concept", "[docker]") {
    static_assert(labios::elastic::ContainerRuntime<labios::elastic::MockRuntime>);
}

TEST_CASE("dechunk parses chunked HTTP body correctly", "[docker]") {
    std::string chunked = "18\r\n{\"Id\":\"abc123def456789\"}\r\n0\r\n\r\n";
    auto result = labios::elastic::DockerClient::dechunk(chunked);
    CHECK(result == "{\"Id\":\"abc123def456789\"}");
}

TEST_CASE("dechunk handles empty chunks", "[docker]") {
    std::string chunked = "0\r\n\r\n";
    auto result = labios::elastic::DockerClient::dechunk(chunked);
    CHECK(result.empty());
}

TEST_CASE("dechunk handles multiple chunks", "[docker]") {
    std::string chunked = "5\r\nhello\r\n6\r\n world\r\n0\r\n\r\n";
    auto result = labios::elastic::DockerClient::dechunk(chunked);
    CHECK(result == "hello world");
}

TEST_CASE("MockRuntime records commission and decommission", "[docker]") {
    labios::elastic::MockRuntime mock;

    auto id = mock.create_and_start({
        .image = "test-image",
        .name = "test-worker",
        .env = {"FOO=bar"},
        .network = "test-net",
    });
    CHECK_FALSE(id.empty());
    CHECK(mock.created.size() == 1);
    CHECK(mock.created[0].name == "test-worker");
    CHECK(mock.created[0].image == "test-image");

    mock.stop_and_remove(id);
    CHECK(mock.stopped.size() == 1);
    CHECK(mock.stopped[0] == id);
}
