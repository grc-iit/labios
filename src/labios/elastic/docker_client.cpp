#include <labios/elastic/docker_client.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <stdexcept>

namespace labios::elastic {

DockerClient::DockerClient(const std::string& socket_path)
    : socket_path_(socket_path) {}

std::string DockerClient::dechunk(const std::string& chunked) {
    std::string result;
    size_t pos = 0;
    while (pos < chunked.size()) {
        auto crlf = chunked.find("\r\n", pos);
        if (crlf == std::string::npos) break;
        auto chunk_size = std::stoul(chunked.substr(pos, crlf - pos), nullptr, 16);
        if (chunk_size == 0) break;
        if (chunk_size > 10 * 1024 * 1024) break;  // Sanity limit.
        pos = crlf + 2;
        if (pos + chunk_size > chunked.size()) break;
        result.append(chunked, pos, chunk_size);
        pos += chunk_size + 2;
    }
    return result;
}

DockerClient::HttpResponse DockerClient::http_request(
    const std::string& method, const std::string& path,
    const std::string& body) {

    struct FdGuard {
        int fd;
        ~FdGuard() { if (fd >= 0) ::close(fd); }
    };

    int raw_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (raw_fd < 0) {
        throw std::runtime_error("socket() failed: " + std::string(std::strerror(errno)));
    }
    FdGuard guard{raw_fd};

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);

    if (::connect(guard.fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        throw std::runtime_error(
            "connect() to " + socket_path_ + " failed: " + std::strerror(errno));
    }

    std::string request = method + " " + path + " HTTP/1.1\r\n"
        "Host: localhost\r\n";
    if (!body.empty()) {
        request += "Content-Type: application/json\r\n"
            "Content-Length: " + std::to_string(body.size()) + "\r\n";
    }
    request += "Connection: close\r\n\r\n" + body;

    auto total = request.size();
    size_t sent = 0;
    while (sent < total) {
        auto n = ::write(guard.fd, request.c_str() + sent, total - sent);
        if (n > 0) {
            sent += static_cast<size_t>(n);
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else {
            throw std::runtime_error("write() failed: " + std::string(std::strerror(errno)));
        }
    }

    std::string response;
    char buf[4096];
    while (true) {
        ssize_t n = ::read(guard.fd, buf, sizeof(buf));
        if (n > 0) {
            response.append(buf, static_cast<size_t>(n));
        } else if (n == 0) {
            break;
        } else if (errno == EINTR) {
            continue;
        } else {
            break;
        }
    }

    HttpResponse result;
    auto status_pos = response.find(' ');
    if (status_pos != std::string::npos && status_pos + 3 < response.size()) {
        result.status_code = std::stoi(response.substr(status_pos + 1, 3));
    }

    auto body_pos = response.find("\r\n\r\n");
    if (body_pos != std::string::npos) {
        std::string raw_body = response.substr(body_pos + 4);
        // Case-insensitive check for chunked transfer encoding.
        auto lower_headers = response.substr(0, body_pos);
        std::transform(lower_headers.begin(), lower_headers.end(),
                       lower_headers.begin(), ::tolower);
        if (lower_headers.find("transfer-encoding: chunked") != std::string::npos) {
            result.body = dechunk(raw_body);
        } else {
            result.body = raw_body;
        }
    }
    return result;
}

std::string DockerClient::create_and_start(const ContainerSpec& spec) {
    std::string env_json = "[";
    for (size_t i = 0; i < spec.env.size(); ++i) {
        if (i > 0) env_json += ",";
        env_json += "\"" + spec.env[i] + "\"";
    }
    env_json += "]";

    std::string body =
        "{\"Image\":\"" + spec.image + "\","
        "\"Env\":" + env_json + ","
        "\"Labels\":{\"labios.elastic\":\"true\","
            "\"labios.worker\":\"" + spec.name + "\"},"
        "\"HostConfig\":{\"NetworkMode\":\"" + spec.network + "\"}}";

    auto resp = http_request("POST",
        "/v1.41/containers/create?name=" + spec.name, body);
    if (resp.status_code != 201) {
        throw std::runtime_error(
            "Docker create failed (" + std::to_string(resp.status_code) + "): "
            + resp.body);
    }

    auto id_pos = resp.body.find("\"Id\":\"");
    if (id_pos == std::string::npos) {
        throw std::runtime_error("No container ID in Docker response");
    }
    auto id_start = id_pos + 6;
    auto id_end = resp.body.find('"', id_start);
    std::string container_id = resp.body.substr(id_start, id_end - id_start);

    auto start_resp = http_request("POST",
        "/v1.41/containers/" + container_id + "/start");
    if (start_resp.status_code != 204 && start_resp.status_code != 304) {
        throw std::runtime_error(
            "Docker start failed (" + std::to_string(start_resp.status_code) + "): "
            + start_resp.body);
    }

    return container_id;
}

void DockerClient::stop_and_remove(const std::string& container_id) {
    try {
        http_request("POST",
            "/v1.41/containers/" + container_id + "/stop?t=10");
    } catch (...) {}

    try {
        http_request("DELETE", "/v1.41/containers/" + container_id);
    } catch (...) {}
}

} // namespace labios::elastic
