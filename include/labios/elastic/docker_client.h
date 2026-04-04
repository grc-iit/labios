#pragma once

#include <string>
#include <vector>

namespace labios::elastic {

struct ContainerSpec {
    std::string image;
    std::string name;
    std::vector<std::string> env;
    std::string network;
};

template<typename T>
concept ContainerRuntime = requires(T rt, const ContainerSpec& spec,
                                     const std::string& id) {
    { rt.create_and_start(spec) } -> std::same_as<std::string>;
    { rt.stop_and_remove(id) };
};

class DockerClient {
public:
    explicit DockerClient(const std::string& socket_path);

    std::string create_and_start(const ContainerSpec& spec);
    void stop_and_remove(const std::string& container_id);

private:
    std::string socket_path_;

    struct HttpResponse {
        int status_code = 0;
        std::string body;
    };

    HttpResponse http_request(const std::string& method,
                              const std::string& path,
                              const std::string& body = "");
    static std::string dechunk(const std::string& chunked);
};

static_assert(ContainerRuntime<DockerClient>);

struct MockRuntime {
    std::vector<ContainerSpec> created;
    std::vector<std::string> stopped;
    int id_counter = 0;

    std::string create_and_start(const ContainerSpec& spec) {
        created.push_back(spec);
        return "mock-container-" + std::to_string(id_counter++);
    }

    void stop_and_remove(const std::string& container_id) {
        stopped.push_back(container_id);
    }
};

static_assert(ContainerRuntime<MockRuntime>);

} // namespace labios::elastic
