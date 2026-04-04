#include <labios/client.h>
#include <labios/config.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

int main() {
    const char* config_path = std::getenv("LABIOS_CONFIG_PATH");
    auto cfg = labios::load_config(config_path ? config_path : "conf/labios.toml");
    auto client = labios::connect(cfg);

    constexpr uint64_t label_size = 1024 * 1024;
    constexpr int num_labels = 100;
    constexpr uint64_t total_size = label_size * num_labels;

    std::vector<std::byte> label_data(label_size);
    std::iota(reinterpret_cast<uint8_t*>(label_data.data()),
              reinterpret_cast<uint8_t*>(label_data.data()) + label_size,
              static_cast<uint8_t>(0));

    // WRITE
    auto write_start = std::chrono::steady_clock::now();
    for (int i = 0; i < num_labels; ++i) {
        std::string path = "/demo/chunk_" + std::to_string(i) + ".bin";
        client.write(path, label_data);
    }
    auto write_end = std::chrono::steady_clock::now();
    double write_sec = std::chrono::duration<double>(write_end - write_start).count();
    double write_mbps = (static_cast<double>(total_size) / (1024.0 * 1024.0)) / write_sec;

    std::cout << "Written: " << (total_size / (1024 * 1024)) << "MB ("
              << num_labels << " labels) in " << write_sec << "s ("
              << write_mbps << " MB/s)\n";

    // READ + verify
    auto read_start = std::chrono::steady_clock::now();
    bool verify_ok = true;
    for (int i = 0; i < num_labels; ++i) {
        std::string path = "/demo/chunk_" + std::to_string(i) + ".bin";
        auto result = client.read(path, 0, label_size);
        if (result.size() != label_size ||
            !std::equal(result.begin(), result.end(), label_data.begin())) {
            verify_ok = false;
            std::cerr << "Mismatch at chunk " << i << "\n";
        }
    }
    auto read_end = std::chrono::steady_clock::now();
    double read_sec = std::chrono::duration<double>(read_end - read_start).count();
    double read_mbps = (static_cast<double>(total_size) / (1024.0 * 1024.0)) / read_sec;

    std::cout << "Read:    " << (total_size / (1024 * 1024)) << "MB ("
              << num_labels << " labels) in " << read_sec << "s ("
              << read_mbps << " MB/s)\n";
    std::cout << "Verify:  " << (verify_ok ? "OK (all bytes match)" : "FAILED") << "\n";

    return verify_ok ? 0 : 1;
}
