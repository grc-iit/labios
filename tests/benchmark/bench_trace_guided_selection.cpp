#include <catch2/catch_test_macros.hpp>
#include <labios/client.h>
#include <labios/config.h>
#include <labios/sds/types.h>
#include <labios/session.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <limits>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct Row {
    std::string run_id;
    std::string profile;
    std::string arm;
    int repetition = 0;
    int workload_order = 0;
    uint64_t label_id = 0;
    std::string scheme;
    std::string resource;
    uint64_t bytes = 0;
    uint64_t submission_us = 0;
    uint64_t completion_us = 0;
    int worker_id = -1;
    double trace_service_input_us = 0.0;
    double trace_queue_input = 0.0;
    uint64_t trace_samples_input = 0;
    uint64_t park_retries = 0;
    std::string terminal_state;
    std::string failure;
    std::string expected_digest;
    std::string observed_digest;
    bool verified = false;
};

struct Submitted {
    labios::PendingIO pending;
    Clock::time_point submitted_at;
    Row row;
};

bool enabled() {
    const auto* value = std::getenv("LABIOS_BENCH_LIVE");
    return value != nullptr && std::string(value) == "1";
}

std::string env_or(const char* name, std::string fallback) {
    const auto* value = std::getenv(name);
    return value == nullptr ? fallback : std::string(value);
}

labios::Config benchmark_config() {
    labios::Config config;
    if (const auto* value = std::getenv("LABIOS_NATS_URL"))
        config.nats_url = value;
    if (const auto* value = std::getenv("LABIOS_REDIS_HOST"))
        config.redis_host = value;
    return config;
}

std::vector<std::byte> payload(size_t size, uint8_t seed) {
    std::vector<std::byte> result(size);
    for (size_t i = 0; i < size; ++i) {
        result[i] =
            static_cast<std::byte>(static_cast<uint8_t>(seed + i % 251));
    }
    return result;
}

std::string digest(std::span<const std::byte> data) {
    uint64_t value = 1469598103934665603ULL;
    for (const auto byte : data) {
        value ^= std::to_integer<uint8_t>(byte);
        value *= 1099511628211ULL;
    }
    std::ostringstream output;
    output << std::hex << std::setw(16) << std::setfill('0') << value;
    return output.str();
}

std::string csv(std::string_view value) {
    std::string output{"\""};
    for (const auto ch : value) {
        if (ch == '"') output += '"';
        output += ch;
    }
    output += '"';
    return output;
}

std::string completion_state(labios::CompletionState state) {
    switch (state) {
    case labios::CompletionState::Pending: return "pending";
    case labios::CompletionState::Complete: return "complete";
    case labios::CompletionState::Failed: return "failed";
    case labios::CompletionState::Cancelled: return "cancelled";
    case labios::CompletionState::Parked: return "parked";
    case labios::CompletionState::Timeout: return "timeout";
    }
    return "unknown";
}

void enrich_from_snapshot(labios::Client& client, Row& row) {
    auto& catalog = client.session().catalog_manager();
    row.park_retries = catalog.park_attempts(row.label_id);
    const auto snapshot = catalog.get_snapshot(row.label_id);
    if (!snapshot || snapshot->score_snapshot.decisions.empty()) return;
    const auto& decision = snapshot->score_snapshot.decisions.back();
    row.worker_id = decision.chosen_worker_id;
    const auto worker = std::find_if(
        decision.replay_workers.begin(), decision.replay_workers.end(),
        [&](const auto& value) { return value.worker_id == row.worker_id; });
    if (worker == decision.replay_workers.end()) return;
    row.trace_service_input_us = worker->trace_service_us;
    row.trace_queue_input = worker->trace_queue_depth;
    row.trace_samples_input = worker->trace_samples;
}

std::vector<Row> await_all(labios::Client& client,
                           std::vector<Submitted> submitted) {
    std::vector<Row> rows;
    rows.reserve(submitted.size());
    while (!submitted.empty()) {
        std::vector<uint64_t> ids;
        ids.reserve(submitted.size());
        for (const auto& item : submitted)
            ids.push_back(item.row.label_id);
        const auto result =
            client.wait_any(ids, std::chrono::milliseconds(30'000));
        REQUIRE(result.state != labios::CompletionState::Timeout);
        REQUIRE(result.results.size() == 1);
        const auto completion = result.results.front();
        const auto found = std::find_if(
            submitted.begin(), submitted.end(), [&](const auto& item) {
                return item.row.label_id == completion.label_id;
            });
        REQUIRE(found != submitted.end());
        found->row.completion_us = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                Clock::now() - found->submitted_at)
                .count());
        found->row.terminal_state = completion_state(completion.state);
        found->row.failure = completion.error;
        enrich_from_snapshot(client, found->row);
        rows.push_back(std::move(found->row));
        submitted.erase(found);
    }
    return rows;
}

Submitted submit_write(labios::Client& client, const Row& prototype,
                       std::string uri, std::span<const std::byte> data) {
    const auto start = Clock::now();
    auto pending = client.async_write_to(uri, data);
    const auto submitted_at = Clock::now();
    REQUIRE(pending.pending.size() == 1);
    auto row = prototype;
    row.resource = uri;
    row.label_id = pending.pending.front().label_id;
    row.bytes = data.size();
    row.submission_us = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            submitted_at - start)
            .count());
    row.expected_digest = digest(data);
    return {std::move(pending), submitted_at, std::move(row)};
}

std::vector<Row> small_hot_metadata(labios::Client& client,
                                    std::string_view prefix,
                                    const Row& prototype) {
    const auto data = payload(4096, 0x11);
    std::vector<Submitted> submitted;
    for (int i = 0; i < 16; ++i) {
        submitted.push_back(submit_write(
            client, prototype,
            std::string(prefix) + "/small-" + std::to_string(i), data));
    }
    auto rows = await_all(client, std::move(submitted));
    for (int i = 0; i < 16; ++i) {
        const auto resource =
            std::string(prefix) + "/small-" + std::to_string(i);
        const auto value = client.read_from(resource, data.size());
        const auto observed = digest(value);
        const auto row = std::find_if(rows.begin(), rows.end(),
                                      [&](const auto& item) {
            return item.resource == resource;
        });
        REQUIRE(row != rows.end());
        row->observed_digest = observed;
        row->verified = value == data &&
                        row->terminal_state == "complete";
    }
    return rows;
}

std::vector<Row> large_sequential(labios::Client& client,
                                  std::string_view prefix,
                                  const Row& prototype) {
    const auto data = payload(1024 * 1024, 0x42);
    std::vector<Submitted> submitted;
    for (int i = 0; i < 4; ++i) {
        submitted.push_back(submit_write(
            client, prototype,
            std::string(prefix) + "/large-" + std::to_string(i), data));
    }
    auto rows = await_all(client, std::move(submitted));
    for (int i = 0; i < 4; ++i) {
        const auto resource =
            std::string(prefix) + "/large-" + std::to_string(i);
        const auto value = client.read_from(resource, data.size());
        const auto row = std::find_if(rows.begin(), rows.end(),
                                      [&](const auto& item) {
            return item.resource == resource;
        });
        REQUIRE(row != rows.end());
        row->observed_digest = digest(value);
        row->verified =
            value == data && row->terminal_state == "complete";
    }
    return rows;
}

std::vector<Row> mixed_pipeline(labios::Client& client,
                                std::string_view prefix,
                                const Row& prototype) {
    const auto source = payload(256 * 1024, 0x73);
    client.write_to(std::string(prefix) + "/pipeline-source", source);
    labios::sds::Pipeline pipeline;
    pipeline.stages.push_back({"builtin://identity", "", -1, 1});
    const std::string destination =
        "sqlite:///bench_p08_" +
        std::to_string(prototype.repetition) + "_" +
        std::to_string(prototype.workload_order) + "_result";
    const auto start = Clock::now();
    auto pending = client.execute_pipeline(
        std::string(prefix) + "/pipeline-source", destination, pipeline);
    const auto submitted_at = Clock::now();
    REQUIRE(pending.pending.size() == 1);
    auto row = prototype;
    row.label_id = pending.pending.front().label_id;
    row.resource = destination;
    row.bytes = source.size();
    row.submission_us = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            submitted_at - start)
            .count());
    row.expected_digest = digest(source);
    auto rows = await_all(
        client, {{std::move(pending), submitted_at, std::move(row)}});
    const auto result = client.read_from(destination, source.size());
    rows.front().observed_digest = digest(result);
    rows.front().verified =
        result == source && rows.front().terminal_state == "complete";
    return rows;
}

using Workload = std::vector<Row> (*)(labios::Client&, std::string_view,
                                      const Row&);

constexpr std::array<std::array<int, 3>, 6> orders{{
    {{0, 1, 2}}, {{0, 2, 1}}, {{1, 0, 2}},
    {{1, 2, 0}}, {{2, 0, 1}}, {{2, 1, 0}},
}};

void write_header(std::ofstream& output) {
    output
        << "run_id,profile,arm,repetition,workload_order,label_id,scheme,"
           "resource,"
           "bytes,submission_us,completion_us,worker_id,"
           "trace_service_input_us,trace_queue_input,trace_samples_input,"
           "park_retries,terminal_state,failure,expected_digest,"
           "observed_digest,verified\n";
}

void write_row(std::ofstream& output, const Row& row) {
    output << csv(row.run_id) << ',' << csv(row.profile) << ','
           << csv(row.arm) << ',' << row.repetition << ','
           << row.workload_order << ',' << row.label_id << ','
           << csv(row.scheme) << ',' << csv(row.resource) << ','
           << row.bytes << ','
           << row.submission_us << ',' << row.completion_us << ','
           << row.worker_id << ',' << row.trace_service_input_us << ','
           << row.trace_queue_input << ',' << row.trace_samples_input << ','
           << row.park_retries << ',' << csv(row.terminal_state) << ','
           << csv(row.failure) << ',' << csv(row.expected_digest) << ','
           << csv(row.observed_digest) << ',' << (row.verified ? 1 : 0)
           << '\n';
}

std::vector<double> trace_services(std::string_view json) {
    std::vector<double> values;
    const std::regex pattern(
        "\"trace_service_us\":([0-9]+(?:\\.[0-9]+)?)");
    const std::string text(json);
    for (auto it = std::sregex_iterator(text.begin(), text.end(), pattern);
         it != std::sregex_iterator(); ++it) {
        const auto value = std::stod((*it)[1].str());
        if (value > 0.0) values.push_back(value);
    }
    return values;
}

} // namespace

TEST_CASE("P10 trace-guided live selection experiment",
          "[bench][live][trace][!benchmark]") {
    if (!enabled()) {
        SKIP("set LABIOS_BENCH_LIVE=1 to run the real dispatcher/worker "
             "experiment");
    }

    const auto arm = env_or("LABIOS_BENCH_ARM", "baseline");
    const auto run_id = env_or("LABIOS_BENCH_RUN_ID", "p08-fixed-seed");
    const auto output_path =
        env_or("LABIOS_BENCH_OUTPUT", "p08-trace-guided.csv");
    const int repetitions =
        std::max(1, std::stoi(env_or("LABIOS_BENCH_REPETITIONS", "20")));
    const int warmup =
        std::max(0, std::stoi(env_or("LABIOS_BENCH_WARMUP", "1")));
    REQUIRE((arm == "baseline" || arm == "ablation" ||
             arm == "informed"));

    auto client = labios::connect(benchmark_config());
    const auto active_policy =
        env_or("LABIOS_BENCH_ACTIVE_POLICY", "unset");
    const auto active_profile =
        env_or("LABIOS_BENCH_ACTIVE_PROFILE", "unset");
    const auto config_probe = client.observe("config/current");
    REQUIRE(config_probe.find("\"scheduler_policy\":\"" + active_policy +
                              "\"") != std::string::npos);
    if (arm == "baseline") {
        REQUIRE(active_policy == "round-robin");
        REQUIRE(active_profile == "none");
    } else {
        REQUIRE(active_policy == "minmax");
        REQUIRE(active_profile ==
                (arm == "ablation" ? "trace_ablation"
                                    : "trace_guided"));
        REQUIRE(config_probe.find(active_profile + ".toml") !=
                std::string::npos);
    }

    const std::array<std::string, 3> profiles{
        "small-hot-metadata", "large-sequential", "mixed-pipeline"};
    const std::array<std::string, 3> schemes{"file", "file", "sqlite"};
    const std::array<Workload, 3> workloads{
        small_hot_metadata, large_sequential, mixed_pipeline};

    for (int repetition = -warmup; repetition < repetitions; ++repetition) {
        const auto& order =
            orders[static_cast<size_t>(
                (repetition < 0 ? 0 : repetition) %
                static_cast<int>(orders.size()))];
        for (int order_index = 0; order_index < 3; ++order_index) {
            const auto workload_index =
                static_cast<size_t>(order[static_cast<size_t>(order_index)]);
            Row prototype;
            prototype.run_id = run_id;
            prototype.profile = profiles[workload_index];
            prototype.arm = arm;
            prototype.repetition = repetition;
            prototype.workload_order = order_index;
            prototype.scheme = schemes[workload_index];
            const auto prefix =
                "file:///bench/p08/" + run_id + "/" + arm + "/" +
                std::to_string(repetition) + "/" +
                std::to_string(order_index);
            auto rows = workloads[workload_index](client, prefix, prototype);
            for (const auto& row : rows) {
                REQUIRE(row.verified);
            }
            if (repetition >= 0) {
                std::ofstream output(output_path, std::ios::app);
                REQUIRE(output.good());
                if (output.tellp() == 0) write_header(output);
                for (const auto& row : rows) write_row(output, row);
            }
        }
    }

    const auto trace_probe = client.observe("workers/scores");
    const auto calibration_output =
        env_or("LABIOS_BENCH_CALIBRATION_OUTPUT", "");
    if (!calibration_output.empty()) {
        std::ofstream output(calibration_output);
        REQUIRE(output.good());
        output << trace_probe << '\n';
    }
    if (arm == "informed") {
        REQUIRE(std::regex_search(
            trace_probe,
            std::regex("\"trace_samples\":[1-9][0-9]*")));
        const auto services = trace_services(trace_probe);
        REQUIRE(services.size() >= 2);
        const auto [minimum, maximum] =
            std::minmax_element(services.begin(), services.end());
        INFO("calibration services: min=" << *minimum
                                          << " max=" << *maximum);
        REQUIRE(*maximum >= *minimum * 1.20);
    }
}
