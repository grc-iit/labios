// tests/integration/kernel_montage_test.cpp
//
// Montage Application Kernel: Producer-Consumer Pipeline
//
// Paper reference: HPDC'19 Section 2.2(c), Section 4 (17x I/O boost)
// Pattern: 3-stage pipeline where each stage's output is the next stage's input
// Goal: demonstrate warehouse as fast data-sharing bridge between pipeline stages

#include <catch2/catch_test_macros.hpp>
#include <labios/client.h>
#include <labios/config.h>

#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

static labios::Config test_config() {
    labios::Config cfg;
    const char* nats = std::getenv("LABIOS_NATS_URL");
    if (nats) cfg.nats_url = nats;
    const char* redis_host = std::getenv("LABIOS_REDIS_HOST");
    if (redis_host) cfg.redis_host = redis_host;
    return cfg;
}

// Simulate processing work on a tile. XOR each byte with a stage marker
// to create distinct, deterministic output per stage.
static std::vector<std::byte> process_tile(std::span<const std::byte> input,
                                            uint8_t stage_marker) {
    std::vector<std::byte> output(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        output[i] = static_cast<std::byte>(
            static_cast<uint8_t>(input[i]) ^ stage_marker);
    }
    return output;
}

TEST_CASE("Montage kernel: 3-stage pipeline, sync", "[kernel][montage]") {
    auto cfg = test_config();

    constexpr int num_tiles = 8;
    constexpr size_t tile_size = 64 * 1024; // 64KB per tile

    double stage_times[3] = {};
    auto total_t0 = std::chrono::steady_clock::now();

    // --- Stage 1: Generate raw tiles (simulates reading FITS images) ---
    {
        auto producer = labios::connect(cfg);
        auto t0 = std::chrono::steady_clock::now();

        for (int i = 0; i < num_tiles; ++i) {
            std::vector<std::byte> raw(tile_size);
            // Each tile has a unique pattern based on tile index.
            for (size_t b = 0; b < tile_size; ++b) {
                raw[b] = static_cast<std::byte>((i * 17 + b) & 0xFF);
            }
            std::string path = "/montage/stage1/tile_" + std::to_string(i) + ".fits";
            producer.write(path, raw);
        }

        auto t1 = std::chrono::steady_clock::now();
        stage_times[0] = std::chrono::duration<double>(t1 - t0).count();
    }

    // --- Stage 2: Reproject tiles (reads stage 1 output, writes reprojected) ---
    {
        auto worker = labios::connect(cfg);
        auto t0 = std::chrono::steady_clock::now();

        for (int i = 0; i < num_tiles; ++i) {
            std::string input_path = "/montage/stage1/tile_" + std::to_string(i) + ".fits";
            auto tile_data = worker.read(input_path, 0, tile_size);
            REQUIRE(tile_data.size() == tile_size);

            // Process: reproject (simulated by XOR with stage marker).
            auto reprojected = process_tile(tile_data, 0xAA);

            std::string output_path = "/montage/stage2/tile_" + std::to_string(i) + ".fits";
            worker.write(output_path, reprojected);
        }

        auto t1 = std::chrono::steady_clock::now();
        stage_times[1] = std::chrono::duration<double>(t1 - t0).count();
    }

    // --- Stage 3: Mosaic (reads reprojected tiles, writes final image) ---
    {
        auto consumer = labios::connect(cfg);
        auto t0 = std::chrono::steady_clock::now();

        // Read all reprojected tiles and combine into final mosaic.
        std::vector<std::byte> mosaic;
        mosaic.reserve(num_tiles * tile_size);

        for (int i = 0; i < num_tiles; ++i) {
            std::string path = "/montage/stage2/tile_" + std::to_string(i) + ".fits";
            auto tile_data = consumer.read(path, 0, tile_size);
            REQUIRE(tile_data.size() == tile_size);

            // Process: mosaic assembly (simulated by XOR with another marker).
            auto final_tile = process_tile(tile_data, 0x55);
            mosaic.insert(mosaic.end(), final_tile.begin(), final_tile.end());
        }

        // Write the final mosaic.
        consumer.write("/montage/final_mosaic.jpg", mosaic);

        auto t1 = std::chrono::steady_clock::now();
        stage_times[2] = std::chrono::duration<double>(t1 - t0).count();
    }

    auto total_t1 = std::chrono::steady_clock::now();
    double total_sec = std::chrono::duration<double>(total_t1 - total_t0).count();
    double total_mb = static_cast<double>(num_tiles * tile_size * 3) / (1024.0 * 1024.0);

    std::cout << "\n=== Montage 3-Stage Pipeline (Sync) ===\n"
              << "  Tiles: " << num_tiles << " x " << (tile_size / 1024) << "KB\n"
              << "  Stage 1 (generate):   " << stage_times[0] << "s\n"
              << "  Stage 2 (reproject):  " << stage_times[1] << "s\n"
              << "  Stage 3 (mosaic):     " << stage_times[2] << "s\n"
              << "  Total pipeline time:  " << total_sec << "s\n"
              << "  Total data moved:     " << total_mb << " MB\n"
              << "  Pipeline throughput:  " << (total_mb / total_sec) << " MB/s\n";

    // Verify final mosaic exists and has correct size.
    auto verifier = labios::connect(cfg);
    auto final_mosaic = verifier.read("/montage/final_mosaic.jpg", 0,
                                       num_tiles * tile_size);
    REQUIRE(final_mosaic.size() == num_tiles * tile_size);
}

TEST_CASE("Montage kernel: async pipelined stages", "[kernel][montage]") {
    auto cfg = test_config();

    constexpr int num_tiles = 8;
    constexpr size_t tile_size = 64 * 1024;

    auto client = labios::connect(cfg);

    auto t0 = std::chrono::steady_clock::now();

    // Stage 1: async write all raw tiles.
    std::vector<labios::PendingIO> stage1_pending;
    for (int i = 0; i < num_tiles; ++i) {
        std::vector<std::byte> raw(tile_size);
        for (size_t b = 0; b < tile_size; ++b) {
            raw[b] = static_cast<std::byte>((i * 17 + b) & 0xFF);
        }
        std::string path = "/montage/async/s1/tile_" + std::to_string(i) + ".fits";
        stage1_pending.push_back(client.async_write(path, raw));
    }

    // Wait for stage 1.
    for (auto& p : stage1_pending) client.wait(p);

    // Stage 2: read stage 1 output, process, async write.
    std::vector<labios::PendingIO> stage2_pending;
    for (int i = 0; i < num_tiles; ++i) {
        std::string in_path = "/montage/async/s1/tile_" + std::to_string(i) + ".fits";
        auto tile = client.read(in_path, 0, tile_size);
        REQUIRE(tile.size() == tile_size);

        auto reprojected = process_tile(tile, 0xAA);
        std::string out_path = "/montage/async/s2/tile_" + std::to_string(i) + ".fits";
        stage2_pending.push_back(client.async_write(out_path, reprojected));
    }

    for (auto& p : stage2_pending) client.wait(p);

    // Stage 3: read stage 2, assemble mosaic.
    std::vector<std::byte> mosaic;
    mosaic.reserve(num_tiles * tile_size);
    for (int i = 0; i < num_tiles; ++i) {
        std::string path = "/montage/async/s2/tile_" + std::to_string(i) + ".fits";
        auto tile = client.read(path, 0, tile_size);
        REQUIRE(tile.size() == tile_size);
        auto final_tile = process_tile(tile, 0x55);
        mosaic.insert(mosaic.end(), final_tile.begin(), final_tile.end());
    }
    client.write("/montage/async/final.jpg", mosaic);

    auto t1 = std::chrono::steady_clock::now();
    double total_sec = std::chrono::duration<double>(t1 - t0).count();

    std::cout << "\n=== Montage Async Pipeline ===\n"
              << "  Total time: " << total_sec << "s\n"
              << "  Tiles processed: " << num_tiles << "\n";

    REQUIRE(mosaic.size() == num_tiles * tile_size);
}

TEST_CASE("Montage kernel: data integrity across stages", "[kernel][montage]") {
    auto cfg = test_config();
    auto client = labios::connect(cfg);

    // Write a known tile, read it back, process it, write again, read again.
    // Verify the transformation chain is deterministic.
    constexpr size_t tile_size = 4096;
    std::vector<std::byte> original(tile_size, static_cast<std::byte>(0x42));

    client.write("/montage/integrity/raw.fits", original);
    auto r1 = client.read("/montage/integrity/raw.fits", 0, tile_size);
    REQUIRE(r1.size() == tile_size);
    REQUIRE(r1[0] == static_cast<std::byte>(0x42));

    auto stage2 = process_tile(r1, 0xAA);
    client.write("/montage/integrity/reprojected.fits", stage2);
    auto r2 = client.read("/montage/integrity/reprojected.fits", 0, tile_size);
    REQUIRE(r2.size() == tile_size);
    // 0x42 XOR 0xAA = 0xE8
    REQUIRE(r2[0] == static_cast<std::byte>(0xE8));

    auto stage3 = process_tile(r2, 0x55);
    client.write("/montage/integrity/mosaic.jpg", stage3);
    auto r3 = client.read("/montage/integrity/mosaic.jpg", 0, tile_size);
    REQUIRE(r3.size() == tile_size);
    // 0xE8 XOR 0x55 = 0xBD
    REQUIRE(r3[0] == static_cast<std::byte>(0xBD));
}
