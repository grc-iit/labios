#include <catch2/catch_test_macros.hpp>
#include <labios/shuffler.h>

namespace {

labios::LabelData make_write(uint64_t id, const std::string& file,
                              uint64_t offset, uint64_t size) {
    labios::LabelData l;
    l.id = id;
    l.type = labios::LabelType::Write;
    l.destination = labios::file_path(file, offset, size);
    l.file_key = file;
    l.data_size = size;
    l.app_id = 1;
    l.reply_to = "inbox." + std::to_string(id);
    return l;
}

labios::LabelData make_read(uint64_t id, const std::string& file,
                             uint64_t offset, uint64_t size) {
    labios::LabelData l;
    l.id = id;
    l.type = labios::LabelType::Read;
    l.source = labios::file_path(file, offset, size);
    l.file_key = file;
    l.data_size = size;
    l.app_id = 1;
    l.reply_to = "inbox." + std::to_string(id);
    return l;
}

auto no_location = [](const std::string&) -> std::optional<int> {
    return std::nullopt;
};

} // namespace

TEST_CASE("Aggregation merges consecutive offsets", "[shuffler]") {
    labios::Shuffler s(labios::ShufflerConfig{});
    std::vector<labios::LabelData> batch;
    batch.push_back(make_write(1, "/data/a.dat", 0, 1024));
    batch.push_back(make_write(2, "/data/a.dat", 1024, 1024));
    batch.push_back(make_write(3, "/data/a.dat", 2048, 1024));

    auto result = s.shuffle(std::move(batch), no_location);

    REQUIRE(result.independent.size() == 1);
    auto& merged = result.independent[0];
    CHECK(merged.id == 1);
    CHECK(merged.data_size == 3072);
    REQUIRE(merged.children.size() == 3);
    CHECK(merged.children[0] == 1);
    CHECK(merged.children[1] == 2);
    CHECK(merged.children[2] == 3);

    auto* fp = std::get_if<labios::FilePath>(&merged.destination);
    REQUIRE(fp != nullptr);
    CHECK(fp->offset == 0);
    CHECK(fp->length == 3072);
}

TEST_CASE("Aggregation disabled passes labels through", "[shuffler]") {
    labios::ShufflerConfig cfg;
    cfg.aggregation_enabled = false;
    labios::Shuffler s(cfg);

    std::vector<labios::LabelData> batch;
    batch.push_back(make_write(1, "/data/a.dat", 0, 1024));
    batch.push_back(make_write(2, "/data/a.dat", 1024, 1024));

    auto result = s.shuffle(std::move(batch), no_location);

    CHECK(result.independent.size() == 2);
    CHECK(result.independent[0].children.empty());
    CHECK(result.independent[1].children.empty());
}

TEST_CASE("Aggregation does not merge non-consecutive offsets", "[shuffler]") {
    labios::Shuffler s(labios::ShufflerConfig{});
    std::vector<labios::LabelData> batch;
    batch.push_back(make_write(1, "/data/a.dat", 0, 1024));
    batch.push_back(make_write(2, "/data/a.dat", 2048, 1024));

    auto result = s.shuffle(std::move(batch), no_location);

    CHECK(result.independent.size() == 2);
}

TEST_CASE("Aggregation does not merge across files", "[shuffler]") {
    labios::Shuffler s(labios::ShufflerConfig{});
    std::vector<labios::LabelData> batch;
    batch.push_back(make_write(1, "/data/a.dat", 0, 1024));
    batch.push_back(make_write(2, "/data/b.dat", 1024, 1024));

    auto result = s.shuffle(std::move(batch), no_location);

    CHECK(result.independent.size() == 2);
}

TEST_CASE("RAW dependency detected (write then read same offset)",
          "[shuffler]") {
    labios::ShufflerConfig cfg;
    cfg.aggregation_enabled = false;
    labios::Shuffler s(cfg);

    std::vector<labios::LabelData> batch;
    batch.push_back(make_write(1, "/data/a.dat", 0, 1024));
    batch.push_back(make_read(2, "/data/a.dat", 0, 1024));

    auto result = s.shuffle(std::move(batch), no_location);

    REQUIRE(result.supertasks.size() == 1);
    auto& st = result.supertasks[0];
    REQUIRE(st.children.size() == 2);

    // The read (id=2) should have a RAW dependency on the write (id=1).
    auto& read_label = st.children[1];
    REQUIRE(read_label.dependencies.size() == 1);
    CHECK(read_label.dependencies[0].label_id == 1);
    CHECK(read_label.dependencies[0].hazard_type == labios::HazardType::RAW);
}

TEST_CASE("WAW dependency detected (two writes same offset)", "[shuffler]") {
    labios::ShufflerConfig cfg;
    cfg.aggregation_enabled = false;
    labios::Shuffler s(cfg);

    std::vector<labios::LabelData> batch;
    batch.push_back(make_write(1, "/data/a.dat", 0, 1024));
    batch.push_back(make_write(2, "/data/a.dat", 0, 512));

    auto result = s.shuffle(std::move(batch), no_location);

    REQUIRE(result.supertasks.size() == 1);
    auto& st = result.supertasks[0];
    REQUIRE(st.children.size() == 2);

    auto& second_write = st.children[1];
    REQUIRE(second_write.dependencies.size() == 1);
    CHECK(second_write.dependencies[0].label_id == 1);
    CHECK(second_write.dependencies[0].hazard_type == labios::HazardType::WAW);
}

TEST_CASE("WAR dependency detected (read then write same offset)",
          "[shuffler]") {
    labios::ShufflerConfig cfg;
    cfg.aggregation_enabled = false;
    labios::Shuffler s(cfg);

    std::vector<labios::LabelData> batch;
    batch.push_back(make_read(1, "/data/a.dat", 0, 1024));
    batch.push_back(make_write(2, "/data/a.dat", 0, 1024));

    auto result = s.shuffle(std::move(batch), no_location);

    REQUIRE(result.supertasks.size() == 1);
    auto& st = result.supertasks[0];
    REQUIRE(st.children.size() == 2);

    auto& write_label = st.children[1];
    REQUIRE(write_label.dependencies.size() == 1);
    CHECK(write_label.dependencies[0].label_id == 1);
    CHECK(write_label.dependencies[0].hazard_type == labios::HazardType::WAR);
}

TEST_CASE("No false dependencies across different files", "[shuffler]") {
    labios::ShufflerConfig cfg;
    cfg.aggregation_enabled = false;
    labios::Shuffler s(cfg);

    std::vector<labios::LabelData> batch;
    batch.push_back(make_write(1, "/data/a.dat", 0, 1024));
    batch.push_back(make_read(2, "/data/b.dat", 0, 1024));

    auto result = s.shuffle(std::move(batch), no_location);

    CHECK(result.supertasks.empty());
    CHECK(result.independent.size() == 2);
    CHECK(result.independent[0].dependencies.empty());
    CHECK(result.independent[1].dependencies.empty());
}

TEST_CASE("No dependency for non-overlapping offsets", "[shuffler]") {
    labios::ShufflerConfig cfg;
    cfg.aggregation_enabled = false;
    labios::Shuffler s(cfg);

    std::vector<labios::LabelData> batch;
    batch.push_back(make_write(1, "/data/a.dat", 0, 1024));
    batch.push_back(make_read(2, "/data/a.dat", 2048, 1024));

    auto result = s.shuffle(std::move(batch), no_location);

    CHECK(result.supertasks.empty());
    CHECK(result.independent.size() == 2);
}

TEST_CASE("Read-locality extraction routes reads to holding worker",
          "[shuffler]") {
    labios::ShufflerConfig cfg;
    cfg.aggregation_enabled = false;
    labios::Shuffler s(cfg);

    auto lookup = [](const std::string& key) -> std::optional<int> {
        if (key == "cached.dat") return 2;
        return std::nullopt;
    };

    std::vector<labios::LabelData> batch;
    batch.push_back(make_read(1, "cached.dat", 0, 1024));
    batch.push_back(make_read(2, "unknown.dat", 0, 1024));
    batch.push_back(make_write(3, "cached.dat", 0, 1024));

    auto result = s.shuffle(std::move(batch), lookup);

    REQUIRE(result.direct_route.size() == 1);
    CHECK(result.direct_route[0].first.id == 1);
    CHECK(result.direct_route[0].second == 2);
    CHECK(result.independent.size() == 2);
}

TEST_CASE("Configurable granularity per-application groups across files",
          "[shuffler]") {
    labios::ShufflerConfig cfg;
    cfg.aggregation_enabled = false;
    cfg.dep_granularity = "per-application";
    labios::Shuffler s(cfg);

    auto w = make_write(1, "/data/a.dat", 0, 1024);
    w.app_id = 42;
    auto r = make_read(2, "/data/b.dat", 0, 1024);
    r.app_id = 42;

    std::vector<labios::LabelData> batch;
    batch.push_back(std::move(w));
    batch.push_back(std::move(r));

    auto result = s.shuffle(std::move(batch), no_location);

    // Per-application groups across files, so overlapping offsets cause a dep.
    REQUIRE(result.supertasks.size() == 1);
}

TEST_CASE("Per-file granularity does not group across files", "[shuffler]") {
    labios::ShufflerConfig cfg;
    cfg.aggregation_enabled = false;
    cfg.dep_granularity = "per-file";
    labios::Shuffler s(cfg);

    auto w = make_write(1, "/data/a.dat", 0, 1024);
    w.app_id = 42;
    auto r = make_read(2, "/data/b.dat", 0, 1024);
    r.app_id = 42;

    std::vector<labios::LabelData> batch;
    batch.push_back(std::move(w));
    batch.push_back(std::move(r));

    auto result = s.shuffle(std::move(batch), no_location);

    CHECK(result.supertasks.empty());
    CHECK(result.independent.size() == 2);
}

TEST_CASE("pack_labels and unpack_labels round-trip", "[shuffler]") {
    std::vector<std::vector<std::byte>> originals;
    originals.push_back({std::byte{0x01}, std::byte{0x02}});
    originals.push_back({std::byte{0xAA}, std::byte{0xBB}, std::byte{0xCC}});
    originals.push_back({std::byte{0xFF}});

    auto packed = labios::pack_labels(originals);
    auto unpacked = labios::unpack_labels(packed);

    REQUIRE(unpacked.size() == 3);
    CHECK(unpacked[0] == originals[0]);
    CHECK(unpacked[1] == originals[1]);
    CHECK(unpacked[2] == originals[2]);
}

TEST_CASE("unpack_labels handles empty input", "[shuffler]") {
    std::span<const std::byte> empty;
    auto result = labios::unpack_labels(empty);
    CHECK(result.empty());
}

TEST_CASE("Aggregation preserves all original reply_to addresses", "[shuffler]") {
    labios::ShufflerConfig cfg{.aggregation_enabled = true};
    labios::Shuffler shuffler(cfg);

    std::vector<labios::LabelData> batch;
    batch.push_back(make_write(1, "/data/file.dat", 0, 1024));
    batch.push_back(make_write(2, "/data/file.dat", 1024, 1024));
    batch.push_back(make_write(3, "/data/file.dat", 2048, 1024));

    auto result = shuffler.shuffle(std::move(batch), no_location);

    REQUIRE(result.independent.size() == 1);
    auto& merged = result.independent[0];
    CHECK(merged.reply_to == "inbox.1");

    auto it = result.reply_fanout.find(merged.id);
    REQUIRE(it != result.reply_fanout.end());
    REQUIRE(it->second.size() == 3);
    CHECK(it->second[0] == "inbox.1");
    CHECK(it->second[1] == "inbox.2");
    CHECK(it->second[2] == "inbox.3");
}

TEST_CASE("Non-aggregated labels have no reply_fanout entry", "[shuffler]") {
    labios::ShufflerConfig cfg{.aggregation_enabled = false};
    labios::Shuffler shuffler(cfg);

    std::vector<labios::LabelData> batch;
    batch.push_back(make_write(1, "/data/file.dat", 0, 1024));

    auto result = shuffler.shuffle(std::move(batch), no_location);

    CHECK(result.reply_fanout.empty());
}
