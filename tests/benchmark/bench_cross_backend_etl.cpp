#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <labios/backend/posix_backend.h>
#include <labios/backend/registry.h>
#include <labios/uri.h>

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace {

const std::vector<std::string>& mixed_uris() {
    static const std::vector<std::string> uris = [] {
        std::vector<std::string> v;
        v.reserve(10000);
        for (int i = 0; i < 10000; ++i) {
            switch (i % 4) {
                case 0: v.push_back("file:///data/chunk_" + std::to_string(i) + ".dat"); break;
                case 1: v.push_back("s3://bucket/key_" + std::to_string(i)); break;
                case 2: v.push_back("vector://collection/embed_" + std::to_string(i)); break;
                case 3: v.push_back("graph://neo4j/node_" + std::to_string(i)); break;
            }
        }
        return v;
    }();
    return uris;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Correctness: URI parsing and registry resolution
// ---------------------------------------------------------------------------

TEST_CASE("Cross-backend ETL: mixed URI parsing", "[bench][etl]") {
    const auto& uris = mixed_uris();

    auto file_uri = labios::parse_uri(uris[0]);
    REQUIRE(file_uri.scheme == "file");

    auto s3_uri = labios::parse_uri(uris[1]);
    REQUIRE(s3_uri.scheme == "s3");

    auto vec_uri = labios::parse_uri(uris[2]);
    REQUIRE(vec_uri.scheme == "vector");

    auto graph_uri = labios::parse_uri(uris[3]);
    REQUIRE(graph_uri.scheme == "graph");
}

TEST_CASE("Cross-backend ETL: registry resolves file scheme", "[bench][etl]") {
    auto tmp = std::filesystem::temp_directory_path() / "labios_bench_etl";
    std::filesystem::create_directories(tmp);

    labios::BackendRegistry registry;
    registry.register_backend(labios::PosixBackend(tmp));

    REQUIRE(registry.has_scheme("file"));
    REQUIRE_FALSE(registry.has_scheme("s3"));
    REQUIRE_FALSE(registry.has_scheme("vector"));

    auto* backend = registry.resolve("file");
    REQUIRE(backend != nullptr);

    std::filesystem::remove_all(tmp);
}

TEST_CASE("Cross-backend ETL: 10K URIs all parse successfully", "[bench][etl]") {
    const auto& uris = mixed_uris();
    for (const auto& raw : uris) {
        auto uri = labios::parse_uri(raw);
        REQUIRE_FALSE(uri.empty());
        REQUIRE_FALSE(uri.scheme.empty());
    }
}

// ---------------------------------------------------------------------------
// Benchmarks
// ---------------------------------------------------------------------------

TEST_CASE("Cross-backend ETL benchmarks", "[bench][etl][!benchmark]") {
    const auto& uris = mixed_uris();

    BENCHMARK("URI parse 10K mixed") {
        labios::URI last;
        for (const auto& raw : uris) {
            last = labios::parse_uri(raw);
        }
        return last.scheme;
    };

    auto tmp = std::filesystem::temp_directory_path() / "labios_bench_etl_bm";
    std::filesystem::create_directories(tmp);

    labios::BackendRegistry registry;
    registry.register_backend(labios::PosixBackend(tmp));

    // Pre-parse URIs
    std::vector<labios::URI> parsed;
    parsed.reserve(uris.size());
    for (const auto& raw : uris) {
        parsed.push_back(labios::parse_uri(raw));
    }

    BENCHMARK("Backend resolve 10K") {
        int resolved = 0;
        for (const auto& uri : parsed) {
            if (registry.resolve(uri.scheme) != nullptr) ++resolved;
        }
        return resolved;
    };

    BENCHMARK("URI roundtrip (parse + to_string) 10K") {
        std::string last;
        for (const auto& raw : uris) {
            auto uri = labios::parse_uri(raw);
            last = labios::to_string(uri);
        }
        return last;
    };

    std::filesystem::remove_all(tmp);
}
