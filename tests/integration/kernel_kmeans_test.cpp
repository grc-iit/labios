// tests/integration/kernel_kmeans_test.cpp
//
// K-Means Application Kernel: Read-Intensive Iterative Workload
//
// Paper reference: HPDC'19 Section 2.2(d)
// Pattern: iterative reads of same dataset, read-dominant, MapReduce-style
// Goal: demonstrate read throughput, locality routing, and caching behavior

#include <catch2/catch_test_macros.hpp>
#include <labios/client.h>
#include <labios/config.h>

#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <cmath>
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

// A simple 2D point for the K-means simulation.
struct Point {
    float x, y;
};

// Generate a synthetic dataset of N points in clusters.
static std::vector<std::byte> generate_dataset(int num_points, int num_clusters) {
    std::vector<Point> points(num_points);
    for (int i = 0; i < num_points; ++i) {
        int cluster = i % num_clusters;
        // Deterministic pseudo-random placement around cluster centers.
        float cx = static_cast<float>(cluster * 100);
        float cy = static_cast<float>(cluster * 50);
        float noise_x = static_cast<float>((i * 7 + 13) % 41) - 20.0f;
        float noise_y = static_cast<float>((i * 11 + 3) % 37) - 18.0f;
        points[i] = {cx + noise_x, cy + noise_y};
    }
    std::vector<std::byte> raw(num_points * sizeof(Point));
    std::memcpy(raw.data(), points.data(), raw.size());
    return raw;
}

// Parse dataset back to points.
static std::vector<Point> parse_dataset(std::span<const std::byte> data) {
    size_t n = data.size() / sizeof(Point);
    std::vector<Point> pts(n);
    std::memcpy(pts.data(), data.data(), n * sizeof(Point));
    return pts;
}

// One K-means iteration: assign points to nearest centroid, recompute centroids.
static std::vector<Point> kmeans_step(const std::vector<Point>& points,
                                       const std::vector<Point>& centroids) {
    int k = static_cast<int>(centroids.size());
    std::vector<float> sum_x(k, 0), sum_y(k, 0);
    std::vector<int> count(k, 0);

    for (auto& p : points) {
        int best = 0;
        float best_dist = 1e30f;
        for (int c = 0; c < k; ++c) {
            float dx = p.x - centroids[c].x;
            float dy = p.y - centroids[c].y;
            float d = dx * dx + dy * dy;
            if (d < best_dist) { best_dist = d; best = c; }
        }
        sum_x[best] += p.x;
        sum_y[best] += p.y;
        count[best]++;
    }

    std::vector<Point> new_centroids(k);
    for (int c = 0; c < k; ++c) {
        if (count[c] > 0) {
            new_centroids[c] = {sum_x[c] / count[c], sum_y[c] / count[c]};
        } else {
            new_centroids[c] = centroids[c];
        }
    }
    return new_centroids;
}

TEST_CASE("K-means kernel: iterative sync reads", "[kernel][kmeans]") {
    auto cfg = test_config();
    auto client = labios::connect(cfg);

    constexpr int num_points = 10000;
    constexpr int num_clusters = 3;
    constexpr int iterations = 5;
    constexpr int num_partitions = 4; // Split dataset across 4 files.
    int points_per_partition = num_points / num_partitions;

    // --- Setup: write the dataset partitioned across multiple files ---
    auto dataset = generate_dataset(num_points, num_clusters);
    size_t partition_size = points_per_partition * sizeof(Point);

    for (int p = 0; p < num_partitions; ++p) {
        std::string path = "/kmeans/data/partition_" + std::to_string(p) + ".bin";
        std::span<const std::byte> chunk(
            dataset.data() + p * partition_size, partition_size);
        client.write(path, chunk);
    }

    // Initialize centroids from first 3 points.
    auto all_points = parse_dataset(dataset);
    std::vector<Point> centroids = {
        all_points[0], all_points[1], all_points[2]
    };

    // --- Iterative K-means: re-read dataset every iteration ---
    auto t0 = std::chrono::steady_clock::now();
    size_t total_bytes_read = 0;

    for (int iter = 0; iter < iterations; ++iter) {
        std::vector<Point> all;
        all.reserve(num_points);

        // Read all partitions.
        for (int p = 0; p < num_partitions; ++p) {
            std::string path = "/kmeans/data/partition_" + std::to_string(p) + ".bin";
            auto data = client.read(path, 0, partition_size);
            REQUIRE(data.size() == partition_size);
            total_bytes_read += data.size();

            auto pts = parse_dataset(data);
            all.insert(all.end(), pts.begin(), pts.end());
        }

        REQUIRE(all.size() == static_cast<size_t>(num_points));

        // Compute new centroids.
        centroids = kmeans_step(all, centroids);
    }

    auto t1 = std::chrono::steady_clock::now();
    double total_sec = std::chrono::duration<double>(t1 - t0).count();
    double read_mb = static_cast<double>(total_bytes_read) / (1024.0 * 1024.0);

    std::cout << "\n=== K-Means Sync Reads ===\n"
              << "  Points: " << num_points << " in " << num_partitions << " partitions\n"
              << "  Clusters: " << num_clusters << "\n"
              << "  Iterations: " << iterations << "\n"
              << "  Total data read: " << read_mb << " MB\n"
              << "  Total time: " << total_sec << "s\n"
              << "  Read throughput: " << (read_mb / total_sec) << " MB/s\n"
              << "  Reads/iteration: " << num_partitions << " (" << (partition_size / 1024) << " KB each)\n"
              << "  Final centroids:\n";
    for (int c = 0; c < num_clusters; ++c) {
        std::cout << "    C" << c << ": (" << centroids[c].x << ", " << centroids[c].y << ")\n";
    }

    // Sanity: centroids should be near the cluster centers (0,0), (100,50), (200,100).
    CHECK(std::abs(centroids[0].x) < 50);
    CHECK(std::abs(centroids[1].x - 100) < 50);
}

TEST_CASE("K-means kernel: async parallel partition reads", "[kernel][kmeans]") {
    auto cfg = test_config();
    auto client = labios::connect(cfg);

    constexpr int num_points = 10000;
    constexpr int num_clusters = 3;
    constexpr int iterations = 5;
    constexpr int num_partitions = 4;
    int points_per_partition = num_points / num_partitions;
    size_t partition_size = points_per_partition * sizeof(Point);

    // Setup: write dataset.
    auto dataset = generate_dataset(num_points, num_clusters);
    for (int p = 0; p < num_partitions; ++p) {
        std::string path = "/kmeans/async/partition_" + std::to_string(p) + ".bin";
        std::span<const std::byte> chunk(
            dataset.data() + p * partition_size, partition_size);
        client.write(path, chunk);
    }

    auto all_points = parse_dataset(dataset);
    std::vector<Point> centroids = {
        all_points[0], all_points[1], all_points[2]
    };

    auto t0 = std::chrono::steady_clock::now();

    for (int iter = 0; iter < iterations; ++iter) {
        // Issue all partition reads asynchronously.
        std::vector<labios::PendingIO> reads;
        for (int p = 0; p < num_partitions; ++p) {
            std::string path = "/kmeans/async/partition_" + std::to_string(p) + ".bin";
            reads.push_back(client.async_read(path, 0, partition_size));
        }

        // Collect results.
        std::vector<Point> all;
        all.reserve(num_points);
        for (auto& r : reads) {
            auto data = client.wait_read(r);
            REQUIRE(data.size() == partition_size);
            auto pts = parse_dataset(data);
            all.insert(all.end(), pts.begin(), pts.end());
        }

        centroids = kmeans_step(all, centroids);
    }

    auto t1 = std::chrono::steady_clock::now();
    double total_sec = std::chrono::duration<double>(t1 - t0).count();
    double read_mb = static_cast<double>(iterations * num_partitions * partition_size)
                     / (1024.0 * 1024.0);

    std::cout << "\n=== K-Means Async Parallel Reads ===\n"
              << "  Total time: " << total_sec << "s\n"
              << "  Read throughput: " << (read_mb / total_sec) << " MB/s\n"
              << "  Final centroids:\n";
    for (int c = 0; c < num_clusters; ++c) {
        std::cout << "    C" << c << ": (" << centroids[c].x << ", " << centroids[c].y << ")\n";
    }

    CHECK(std::abs(centroids[0].x) < 50);
}

TEST_CASE("K-means kernel: read data integrity across iterations", "[kernel][kmeans]") {
    auto cfg = test_config();
    auto client = labios::connect(cfg);

    // Write a small dataset, read it back multiple times, verify it never changes.
    constexpr size_t data_size = 8 * 1024;
    std::vector<std::byte> original(data_size);
    std::iota(reinterpret_cast<uint8_t*>(original.data()),
              reinterpret_cast<uint8_t*>(original.data()) + data_size,
              static_cast<uint8_t>(0));

    client.write("/kmeans/integrity/dataset.bin", original);

    for (int iter = 0; iter < 10; ++iter) {
        auto data = client.read("/kmeans/integrity/dataset.bin", 0, data_size);
        REQUIRE(data.size() == data_size);
        REQUIRE(std::equal(data.begin(), data.end(), original.begin()));
    }
}
