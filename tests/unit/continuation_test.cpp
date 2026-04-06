#include <catch2/catch_test_macros.hpp>
#include <labios/continuation.h>
#include <labios/label.h>
#include <labios/channel.h>
#include <labios/transport/nats.h>
#include <labios/transport/redis.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <thread>

static std::string redis_host() {
    const char* h = std::getenv("LABIOS_REDIS_HOST");
    return (h && h[0]) ? h : "localhost";
}

static int redis_port() {
    const char* val = std::getenv("LABIOS_REDIS_PORT");
    return (val && val[0]) ? std::stoi(val) : 6379;
}

static std::string nats_url() {
    const char* u = std::getenv("LABIOS_NATS_URL");
    return (u && u[0]) ? u : "nats://localhost:4222";
}

// ---------------------------------------------------------------------------
// Pure logic tests (no network needed for these functions)
// ---------------------------------------------------------------------------

TEST_CASE("Chain params encode/decode roundtrip", "[continuation]") {
    labios::LabelData tmpl;
    tmpl.type = labios::LabelType::Read;
    tmpl.source = labios::file_path("/data/input.h5", 0, 1024);
    tmpl.destination = labios::file_path("/data/output.h5");
    tmpl.operation = "read_block";
    tmpl.priority = 3;
    tmpl.data_size = 1024;
    tmpl.intent = labios::Intent::Cache;

    auto encoded = labios::encode_chain_params(tmpl);
    REQUIRE(!encoded.empty());

    auto decoded = labios::decode_chain_params(encoded);
    REQUIRE(decoded.type == labios::LabelType::Read);
    REQUIRE(decoded.operation == "read_block");
    REQUIRE(decoded.priority == 3);
    REQUIRE(decoded.data_size == 1024);
    REQUIRE(decoded.intent == labios::Intent::Cache);

    auto& src = std::get<labios::FilePath>(decoded.source);
    REQUIRE(src.path == "/data/input.h5");
    REQUIRE(src.length == 1024);

    auto& dst = std::get<labios::FilePath>(decoded.destination);
    REQUIRE(dst.path == "/data/output.h5");
}

TEST_CASE("evaluate_condition: status==Complete", "[continuation]") {
    labios::LabelData label;
    label.data_size = 4096;
    labios::CompletionData comp;
    comp.status = labios::CompletionStatus::Complete;
    comp.label_id = 42;

    REQUIRE(labios::evaluate_condition("status==Complete", label, comp));
    REQUIRE_FALSE(labios::evaluate_condition("status==Error", label, comp));
    REQUIRE(labios::evaluate_condition("status!=Error", label, comp));
}

TEST_CASE("evaluate_condition: data_size comparisons", "[continuation]") {
    labios::LabelData label;
    label.data_size = 4096;
    labios::CompletionData comp;

    REQUIRE(labios::evaluate_condition("data_size>0", label, comp));
    REQUIRE(labios::evaluate_condition("data_size>4095", label, comp));
    REQUIRE_FALSE(labios::evaluate_condition("data_size>4096", label, comp));
    REQUIRE(labios::evaluate_condition("data_size==4096", label, comp));
    REQUIRE(labios::evaluate_condition("data_size<5000", label, comp));
    REQUIRE_FALSE(labios::evaluate_condition("data_size<4096", label, comp));
}

TEST_CASE("evaluate_condition: error string matching", "[continuation]") {
    labios::LabelData label;
    labios::CompletionData comp;
    comp.error = "";

    REQUIRE(labios::evaluate_condition(R"(error=="")", label, comp));
    REQUIRE_FALSE(labios::evaluate_condition(R"(error!="")", label, comp));

    comp.error = "disk full";
    REQUIRE_FALSE(labios::evaluate_condition(R"(error=="")", label, comp));
    REQUIRE(labios::evaluate_condition(R"(error=="disk full")", label, comp));
}

TEST_CASE("evaluate_condition: label_id comparison", "[continuation]") {
    labios::LabelData label;
    labios::CompletionData comp;
    comp.label_id = 100;

    REQUIRE(labios::evaluate_condition("label_id==100", label, comp));
    REQUIRE_FALSE(labios::evaluate_condition("label_id==99", label, comp));
    REQUIRE(labios::evaluate_condition("label_id>50", label, comp));
    REQUIRE_FALSE(labios::evaluate_condition("label_id<50", label, comp));
}

// ---------------------------------------------------------------------------
// Tests requiring live Redis + NATS
// ---------------------------------------------------------------------------

TEST_CASE("ContinuationKind::None returns nullopt", "[continuation]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::transport::NatsConnection nats(nats_url());
    labios::ChannelRegistry channels(redis, nats);

    labios::LabelData label;
    label.id = 1;
    label.continuation.kind = labios::ContinuationKind::None;

    labios::CompletionData comp;
    comp.label_id = 1;
    comp.status = labios::CompletionStatus::Complete;

    auto result = labios::process_continuation(label, comp, channels, nats, redis);
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("ContinuationKind::Notify publishes completion to channel", "[continuation]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::transport::NatsConnection nats(nats_url());
    labios::ChannelRegistry channels(redis, nats);

    auto* ch = channels.create("test-cont-notify");
    REQUIRE(ch != nullptr);

    std::atomic<int> received{0};
    labios::ChannelMessage last_msg{};
    std::mutex msg_mu;

    ch->subscribe([&](const labios::ChannelMessage& m) {
        std::lock_guard lock(msg_mu);
        last_msg.sequence = m.sequence;
        last_msg.label_id = m.label_id;
        last_msg.data = m.data;
        received.fetch_add(1);
    });

    labios::LabelData label;
    label.id = 42;
    label.continuation.kind = labios::ContinuationKind::Notify;
    label.continuation.target_channel = "test-cont-notify";

    labios::CompletionData comp;
    comp.label_id = 42;
    comp.status = labios::CompletionStatus::Complete;
    comp.data_key = "warehouse:42";

    auto result = labios::process_continuation(label, comp, channels, nats, redis);
    REQUIRE_FALSE(result.has_value());

    for (int i = 0; i < 50 && received.load() == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    REQUIRE(received.load() == 1);
    {
        std::lock_guard lock(msg_mu);
        REQUIRE(last_msg.label_id == 42);
        auto deserialized = labios::deserialize_completion(last_msg.data);
        REQUIRE(deserialized.label_id == 42);
        REQUIRE(deserialized.status == labios::CompletionStatus::Complete);
        REQUIRE(deserialized.data_key == "warehouse:42");
    }

    ch->destroy();
    channels.remove("test-cont-notify");
}

TEST_CASE("ContinuationKind::Chain creates new label from chain_params", "[continuation]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::transport::NatsConnection nats(nats_url());
    labios::ChannelRegistry channels(redis, nats);

    labios::LabelData tmpl;
    tmpl.type = labios::LabelType::Read;
    tmpl.source = labios::file_path("/data/result.bin");
    tmpl.data_size = 2048;

    labios::LabelData completed;
    completed.id = 100;
    completed.app_id = 7;
    completed.continuation.kind = labios::ContinuationKind::Chain;
    completed.continuation.chain_params = labios::encode_chain_params(tmpl);

    labios::CompletionData comp;
    comp.label_id = 100;
    comp.status = labios::CompletionStatus::Complete;

    auto result = labios::process_continuation(completed, comp, channels, nats, redis);
    REQUIRE(result.has_value());

    auto& chained = *result;
    REQUIRE(chained.id != 0);
    REQUIRE(chained.id != completed.id);
    REQUIRE(chained.app_id == 7);
    REQUIRE(chained.type == labios::LabelType::Read);
    REQUIRE(chained.data_size == 2048);
    REQUIRE(chained.status == labios::StatusCode::Created);

    auto& src = std::get<labios::FilePath>(chained.source);
    REQUIRE(src.path == "/data/result.bin");
}

TEST_CASE("Chained label gets unique snowflake ID", "[continuation]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::transport::NatsConnection nats(nats_url());
    labios::ChannelRegistry channels(redis, nats);

    labios::LabelData tmpl;
    tmpl.type = labios::LabelType::Write;

    labios::LabelData completed;
    completed.id = 500;
    completed.app_id = 3;
    completed.continuation.kind = labios::ContinuationKind::Chain;
    completed.continuation.chain_params = labios::encode_chain_params(tmpl);

    labios::CompletionData comp;
    comp.label_id = 500;
    comp.status = labios::CompletionStatus::Complete;

    auto r1 = labios::process_continuation(completed, comp, channels, nats, redis);
    auto r2 = labios::process_continuation(completed, comp, channels, nats, redis);
    REQUIRE(r1.has_value());
    REQUIRE(r2.has_value());
    REQUIRE(r1->id != r2->id);
    REQUIRE(r1->app_id == 3);
    REQUIRE(r2->app_id == 3);
}

TEST_CASE("ContinuationKind::Conditional chains when condition is true", "[continuation]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::transport::NatsConnection nats(nats_url());
    labios::ChannelRegistry channels(redis, nats);

    labios::LabelData tmpl;
    tmpl.type = labios::LabelType::Write;

    labios::LabelData completed;
    completed.id = 200;
    completed.app_id = 5;
    completed.data_size = 8192;
    completed.continuation.kind = labios::ContinuationKind::Conditional;
    completed.continuation.condition = "status==Complete";
    completed.continuation.chain_params = labios::encode_chain_params(tmpl);

    labios::CompletionData comp;
    comp.label_id = 200;
    comp.status = labios::CompletionStatus::Complete;

    auto result = labios::process_continuation(completed, comp, channels, nats, redis);
    REQUIRE(result.has_value());
    REQUIRE(result->app_id == 5);
    REQUIRE(result->type == labios::LabelType::Write);
}

TEST_CASE("ContinuationKind::Conditional returns nullopt when false", "[continuation]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::transport::NatsConnection nats(nats_url());
    labios::ChannelRegistry channels(redis, nats);

    labios::LabelData tmpl;
    tmpl.type = labios::LabelType::Read;

    labios::LabelData completed;
    completed.id = 300;
    completed.app_id = 9;
    completed.data_size = 0;
    completed.continuation.kind = labios::ContinuationKind::Conditional;
    completed.continuation.condition = "data_size>0";
    completed.continuation.chain_params = labios::encode_chain_params(tmpl);

    labios::CompletionData comp;
    comp.label_id = 300;
    comp.status = labios::CompletionStatus::Complete;

    auto result = labios::process_continuation(completed, comp, channels, nats, redis);
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("Notify auto-creates channel if not found in registry", "[continuation]") {
    labios::transport::RedisConnection redis(redis_host(), redis_port());
    labios::transport::NatsConnection nats(nats_url());
    labios::ChannelRegistry channels(redis, nats);

    labios::LabelData label;
    label.id = 600;
    label.continuation.kind = labios::ContinuationKind::Notify;
    label.continuation.target_channel = "auto-created-channel";

    labios::CompletionData comp;
    comp.label_id = 600;
    comp.status = labios::CompletionStatus::Complete;

    auto result = labios::process_continuation(label, comp, channels, nats, redis);
    REQUIRE_FALSE(result.has_value());

    auto* ch = channels.get("auto-created-channel");
    REQUIRE(ch != nullptr);

    ch->destroy();
    channels.remove("auto-created-channel");
}
