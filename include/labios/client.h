#pragma once

#include <labios/config.h>
#include <labios/label.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace labios {

class Session;  // Forward declare

class Client {
public:
    explicit Client(const Config& cfg);
    ~Client();

    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    void write(std::string_view filepath, std::span<const std::byte> data,
               uint64_t offset = 0);
    std::vector<std::byte> read(std::string_view filepath, uint64_t offset,
                                uint64_t size);

    Session& session();

private:
    std::unique_ptr<Session> session_;
};

Client connect(const Config& cfg);

} // namespace labios
