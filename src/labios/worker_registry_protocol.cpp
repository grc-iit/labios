#include <labios/worker_registry_protocol.h>

#include <algorithm>
#include <exception>
#include <optional>
#include <sstream>
#include <string>

namespace labios {
namespace {

std::optional<WorkerInfo> decode_worker_row(std::string_view line) {
    std::istringstream ls{std::string(line)};
    std::string token;
    WorkerInfo w{};

    try {
        if (!std::getline(ls, token, ',')) return std::nullopt;
        w.id = std::stoi(token);

        if (!std::getline(ls, token, ',')) return std::nullopt;
        w.available = (token == "1");

        if (!std::getline(ls, token, ',')) return std::nullopt;
        w.capacity = std::stod(token);

        if (!std::getline(ls, token, ',')) return std::nullopt;
        w.load = std::stod(token);

        if (!std::getline(ls, token, ',')) return std::nullopt;
        w.speed = std::stoi(token);

        if (!std::getline(ls, token, ',')) return std::nullopt;
        w.energy = std::stoi(token);

        if (std::getline(ls, token, ',') && !token.empty()) {
            auto tier = std::clamp(std::stoi(token), 0, 2);
            w.tier = static_cast<WorkerTier>(tier);
        }
    } catch (const std::exception&) {
        return std::nullopt;
    }

    return w;
}

} // namespace

std::string encode_worker_registry(std::span<const WorkerInfo> workers) {
    std::string response;
    for (const auto& w : workers) {
        response += std::to_string(w.id) + ","
                  + (w.available ? "1" : "0") + ","
                  + std::to_string(w.capacity) + ","
                  + std::to_string(w.load) + ","
                  + std::to_string(w.speed) + ","
                  + std::to_string(w.energy) + ","
                  + std::to_string(static_cast<int>(w.tier)) + "\n";
    }
    return response;
}

WorkerRegistrySnapshot decode_worker_registry(std::string_view payload) {
    WorkerRegistrySnapshot snapshot;
    std::istringstream lines{std::string(payload)};
    std::string line;

    while (std::getline(lines, line)) {
        if (line.empty()) continue;
        auto worker = decode_worker_row(line);
        if (worker) {
            snapshot.workers.push_back(*worker);
        } else {
            ++snapshot.malformed_rows;
        }
    }

    return snapshot;
}

} // namespace labios
