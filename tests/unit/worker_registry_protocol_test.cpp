#include <labios/worker_registry_protocol.h>
#include <labios/backend/posix_backend.h>
#include <labios/backend/registry.h>
#include <labios/backend/sqlite_backend.h>
#include <labios/label.h>

#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <filesystem>
#include <span>

namespace {
class FakeKvBackend {
public:
    labios::BackendResult put(const labios::LabelData&, std::span<const std::byte>) { return {}; }
    labios::BackendDataResult get(const labios::LabelData&) { return {}; }
    labios::BackendResult del(const labios::LabelData&) { return {}; }
    labios::BackendQueryResult query(const labios::LabelData&) { return {}; }
    std::string_view scheme() const { return "kv"; }
};
static_assert(labios::BackendStore<FakeKvBackend>);
}

TEST_CASE("Worker capabilities reflect constructed external backends and tier", "[worker-registry][backend]") {
    const auto root = std::filesystem::temp_directory_path() / "labios-capability-test";
    const auto database = root / "worker.db";
    std::filesystem::create_directories(root);
    labios::BackendRegistry backends;
    backends.register_backend(labios::PosixBackend(root));
    backends.register_backend(labios::SQLiteBackend(database.string()));

    labios::WorkerInfo base;
    base.id = 1;
    base.registration_epoch = 1;
    base.tier = labios::WorkerTier::Databot;
    const std::vector<std::string> pipeline{"builtin://identity"};
    auto tier0 = labios::derive_worker_capabilities(base, backends, pipeline);
    CHECK(tier0.operations == std::vector<std::string>{"core.read", "core.write", "core.composite"});
    CHECK(tier0.operation_versions == std::vector<uint32_t>{1, 1, 1});
    CHECK(tier0.pipeline_operations.empty());
    REQUIRE(tier0.attachments.size() == 2);

    backends.register_backend(FakeKvBackend{});
    base.tier = labios::WorkerTier::Pipeline;
    auto tier1 = labios::derive_worker_capabilities(base, backends, pipeline);
    CHECK(tier1.pipeline_operations == pipeline);
    CHECK(tier1.pipeline_operation_versions == std::vector<uint32_t>{1});
    REQUIRE(tier1.attachments.size() == 3);
    CHECK(std::any_of(tier1.attachments.begin(), tier1.attachments.end(),
        [](const auto& attachment) { return attachment.scheme == "kv"; }));
    CHECK(std::none_of(tier1.operations.begin(), tier1.operations.end(),
        [](const auto& operation) { return operation == "core.delete" || operation == "core.flush"; }));
    std::filesystem::remove_all(root);
}

TEST_CASE("Verified v2 snapshot permits an empty registry", "[worker-registry]") {
    auto encoded = labios::encode_worker_snapshot({}, 0, 1);
    auto decoded = labios::decode_worker_message(std::span<const std::byte>(encoded));
    REQUIRE(decoded.kind == labios::WorkerRegistryMessage::Kind::Snapshot);
    CHECK(decoded.registry_generation == 0);
    CHECK(decoded.workers.empty());
}

TEST_CASE("Verified v2 preserves capability versions and attachments", "[worker-registry]") {
    labios::WorkerInfo worker;
    worker.id = 7;
    worker.registration_epoch = 9;
    worker.operations = {"core.read", "core.write"};
    worker.operation_versions = {1, 1};
    worker.pipeline_operations = {"builtin://identity"};
    worker.pipeline_operation_versions = {1};
    worker.attachments = {{static_cast<uint8_t>(labios::ResourceFamily::FileRange),
                           "default", "file", labios::LocalityKind::Shared, {}}};
    auto encoded = labios::encode_worker_registration(worker);
    auto decoded = labios::decode_worker_message(std::span<const std::byte>(encoded));
    CHECK(decoded.worker.operations == worker.operations);
    CHECK(decoded.worker.operation_versions == worker.operation_versions);
    CHECK(decoded.worker.pipeline_operation_versions == worker.pipeline_operation_versions);
    CHECK(decoded.worker.attachments.size() == 1);

    worker.operation_versions.clear();
    CHECK_THROWS(labios::encode_worker_registration(worker));
}

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
