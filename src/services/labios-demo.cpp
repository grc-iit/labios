#include <labios/client.h>
#include <labios/config.h>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

int main() {
    try {
        const char* config_path = std::getenv("LABIOS_CONFIG_PATH");
        auto cfg = labios::load_config(config_path ? config_path : "conf/labios.toml");
        auto client = labios::connect(cfg);

        // More than twenty independent round-robin writes exercise both
        // shared worker backends. Read-back may be scheduled to any worker.
        constexpr int operation_count = 24;
        constexpr size_t payload_size = 4096;
        std::vector<std::vector<std::byte>> expected;
        std::vector<std::string> uris;
        expected.reserve(operation_count);
        uris.reserve(operation_count);

        for (int i = 0; i < operation_count; ++i) {
            std::vector<std::byte> payload(payload_size, static_cast<std::byte>(i));
            const auto uri = i % 2 == 0
                ? "file:///demo/golden-" + std::to_string(i) + ".bin"
                : "sqlite:///demo/golden-" + std::to_string(i);
            client.write_to(uri, payload);
            uris.push_back(uri);
            expected.push_back(std::move(payload));
        }

        for (int i = 0; i < operation_count; ++i) {
            const auto actual = client.read_from(uris[i], payload_size);
            if (actual != expected[i]) {
                std::cerr << "LABIOS demo: read-back mismatch for " << uris[i] << '\n';
                return 1;
            }
        }

        std::cout << "LABIOS demo: verified " << operation_count
                  << " writes and read-backs across shared file and SQLite backends\n";
        return 0;
    } catch (const labios::CompletionError& ex) {
        std::cerr << "LABIOS demo: completion error for label " << ex.label_id()
                  << ": " << ex.what() << '\n';
        return 1;
    } catch (const std::exception& ex) {
        std::cerr << "LABIOS demo: " << ex.what() << '\n';
        return 1;
    }
}
