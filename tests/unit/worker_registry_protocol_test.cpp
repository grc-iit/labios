#include <labios/worker_registry_protocol.h>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Worker registry protocol preserves tier field", "[worker-registry]") {
    std::vector<labios::WorkerInfo> workers{
        {1, true, 0.9, 0.1, 5, 2, labios::WorkerTier::Databot},
        {2, true, 0.8, 0.2, 4, 3, labios::WorkerTier::Pipeline},
        {3, false, 0.7, 0.3, 3, 4, labios::WorkerTier::Agentic},
    };

    auto encoded = labios::encode_worker_registry(workers);
    auto decoded = labios::decode_worker_registry(encoded);

    REQUIRE(decoded.malformed_rows == 0);
    REQUIRE(decoded.workers.size() == 3);
    CHECK(decoded.workers[0].tier == labios::WorkerTier::Databot);
    CHECK(decoded.workers[1].tier == labios::WorkerTier::Pipeline);
    CHECK(decoded.workers[2].tier == labios::WorkerTier::Agentic);
    CHECK_FALSE(decoded.workers[2].available);
}

TEST_CASE("Worker registry protocol accepts legacy rows without tier", "[worker-registry]") {
    auto decoded = labios::decode_worker_registry("4,1,0.6,0.4,2,5\n");

    REQUIRE(decoded.malformed_rows == 0);
    REQUIRE(decoded.workers.size() == 1);
    CHECK(decoded.workers[0].tier == labios::WorkerTier::Databot);
}

TEST_CASE("Worker registry protocol reports malformed rows", "[worker-registry]") {
    auto decoded = labios::decode_worker_registry(
        "1,1,0.9,0.1,5,2,0\n"
        "not,a,worker\n"
        "2,1,0.8,0.2,4\n");

    CHECK(decoded.workers.size() == 1);
    CHECK(decoded.malformed_rows == 2);
}
