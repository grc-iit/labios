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

class Status {
public:
    void wait();
    [[nodiscard]] bool ready() const;
    [[nodiscard]] CompletionStatus result() const;
    [[nodiscard]] std::string error() const;
    [[nodiscard]] std::string data_key() const;
    [[nodiscard]] uint64_t label_id() const;

private:
    friend class Client;
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

class Label {
public:
    [[nodiscard]] uint64_t id() const { return data_.id; }
    [[nodiscard]] LabelType type() const { return data_.type; }
    [[nodiscard]] const LabelData& data() const { return data_; }
    [[nodiscard]] const std::vector<std::byte>& serialized() const {
        return serialized_;
    }

private:
    friend class Client;
    LabelData data_;
    std::vector<std::byte> serialized_;
};

class Client {
public:
    explicit Client(const Config& cfg);
    ~Client();

    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    Label create_label(const LabelParams& params);
    Status publish(const Label& label, std::span<const std::byte> data = {});

    void write(std::string_view filepath, std::span<const std::byte> data,
               uint64_t offset = 0);
    std::vector<std::byte> read(std::string_view filepath, uint64_t offset,
                                uint64_t size);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

Client connect(const Config& cfg);

} // namespace labios
