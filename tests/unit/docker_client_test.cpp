#include <labios/elastic/docker_client.h>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("DockerClient satisfies ContainerRuntime concept", "[docker]") {
    static_assert(labios::elastic::ContainerRuntime<labios::elastic::DockerClient>);
}

TEST_CASE("MockRuntime satisfies ContainerRuntime concept", "[docker]") {
    static_assert(labios::elastic::ContainerRuntime<labios::elastic::MockRuntime>);
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
