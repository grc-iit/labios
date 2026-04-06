#include <labios/shuffler.h>

#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

namespace labios {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

/// Extract the offset range from a Pointer (FilePath variant).
/// Returns {offset, length} or {0, 0} if the pointer is not a FilePath.
struct OffsetRange {
    uint64_t offset = 0;
    uint64_t length = 0;
};

OffsetRange get_range(const Pointer& ptr) {
    if (auto* fp = std::get_if<FilePath>(&ptr)) {
        return {fp->offset, fp->length};
    }
    return {};
}

/// Get the relevant range for a label depending on its type.
/// Write/Delete/Flush use destination; Read uses source.
OffsetRange label_range(const LabelData& l) {
    if (l.type == LabelType::Write || l.type == LabelType::Delete
        || l.type == LabelType::Flush) {
        return get_range(l.destination);
    }
    return get_range(l.source);
}

/// True when two offset ranges overlap.
bool ranges_overlap(OffsetRange a, OffsetRange b) {
    if (a.length == 0 || b.length == 0) return false;
    return a.offset < b.offset + b.length
        && b.offset < a.offset + a.length;
}

/// Grouping key based on granularity.
std::string group_key(const LabelData& l, const std::string& granularity) {
    if (granularity == "per-application") {
        return std::to_string(l.app_id);
    }
    // "per-file" and "per-dataset" both use file_key.
    return l.file_key;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

Shuffler::Shuffler(ShufflerConfig config)
    : config_(std::move(config)) {}

// ---------------------------------------------------------------------------
// shuffle() pipeline
// ---------------------------------------------------------------------------

ShuffleResult Shuffler::shuffle(std::vector<LabelData> batch,
                                LocationLookup lookup) {
    ShuffleResult result;

    // Phase 1: extract read-locality labels.
    std::vector<LabelData> remaining;
    remaining.reserve(batch.size());

    for (auto& label : batch) {
        if (label.type == LabelType::Read && lookup) {
            auto range = label_range(label);
            auto loc = lookup(label.file_key, range.offset, range.length);
            if (loc.has_value()) {
                result.direct_route.emplace_back(std::move(label), *loc);
                continue;
            }
        }
        remaining.push_back(std::move(label));
    }

    // Phase 2: aggregate consecutive writes.
    remaining = aggregate(remaining, result.reply_fanout);

    // Phase 3: detect RAW/WAW/WAR dependencies.
    detect_dependencies(remaining);

    // Phase 4: build supertasks, leaving independent labels in remaining.
    result.supertasks = build_supertasks(remaining);
    result.independent = std::move(remaining);

    return result;
}

// ---------------------------------------------------------------------------
// aggregate()
// ---------------------------------------------------------------------------

std::vector<LabelData> Shuffler::aggregate(
    std::vector<LabelData>& labels,
    std::unordered_map<uint64_t, std::vector<std::string>>& reply_fanout) {
    if (!config_.aggregation_enabled) {
        return std::move(labels);
    }

    // Group by file_key, preserving insertion order per group.
    std::unordered_map<std::string, std::vector<LabelData>> groups;
    std::vector<std::string> group_order;

    for (auto& l : labels) {
        auto [it, inserted] = groups.try_emplace(l.file_key);
        if (inserted) {
            group_order.push_back(l.file_key);
        }
        it->second.push_back(std::move(l));
    }

    std::vector<LabelData> output;
    output.reserve(labels.size());

    for (auto& key : group_order) {
        auto& group = groups[key];

        // Separate writes from non-writes.
        std::vector<LabelData> writes;
        for (auto& l : group) {
            if (l.type == LabelType::Write) {
                writes.push_back(std::move(l));
            } else {
                output.push_back(std::move(l));
            }
        }

        if (writes.empty()) continue;

        // Sort writes by destination offset.
        std::sort(writes.begin(), writes.end(),
                  [](const LabelData& a, const LabelData& b) {
                      return get_range(a.destination).offset
                           < get_range(b.destination).offset;
                  });

        // Scan for consecutive runs and merge them.
        size_t i = 0;
        while (i < writes.size()) {
            size_t run_start = i;
            uint64_t run_end_offset = get_range(writes[i].destination).offset
                                    + writes[i].data_size;
            size_t j = i + 1;

            while (j < writes.size()) {
                auto next_range = get_range(writes[j].destination);
                if (next_range.offset == run_end_offset) {
                    run_end_offset = next_range.offset + writes[j].data_size;
                    ++j;
                } else {
                    break;
                }
            }

            if (j - run_start == 1) {
                // Single write, no merge needed.
                output.push_back(std::move(writes[run_start]));
            } else {
                // Merge the consecutive run.
                auto& first = writes[run_start];
                auto first_range = get_range(first.destination);
                uint64_t total_size = 0;
                std::vector<uint64_t> child_ids;
                child_ids.reserve(j - run_start);

                for (size_t k = run_start; k < j; ++k) {
                    total_size += writes[k].data_size;
                    child_ids.push_back(writes[k].id);
                }

                // Extract the path string from the first write's FilePath.
                auto* first_fp = std::get_if<FilePath>(&first.destination);
                std::string dest_path = first_fp ? first_fp->path : "";

                LabelData merged;
                merged.id = first.id;
                merged.type = LabelType::Write;
                merged.destination = file_path(dest_path,
                                               first_range.offset,
                                               total_size);
                merged.data_size = total_size;
                merged.children = std::move(child_ids);
                merged.reply_to = first.reply_to;

                // Collect all original reply_to addresses for fanout.
                std::vector<std::string> replies;
                replies.reserve(j - run_start);
                for (size_t k = run_start; k < j; ++k) {
                    if (!writes[k].reply_to.empty()) {
                        replies.push_back(writes[k].reply_to);
                    }
                }
                if (replies.size() > 1) {
                    reply_fanout[merged.id] = std::move(replies);
                }

                merged.aggregation.original_ids = merged.children;
                merged.aggregation.merged_offset = first_range.offset;
                merged.aggregation.merged_length = total_size;

                merged.file_key = first.file_key;
                merged.app_id = first.app_id;
                merged.flags = first.flags;
                merged.priority = first.priority;
                merged.intent = first.intent;
                merged.ttl_seconds = first.ttl_seconds;
                merged.isolation = first.isolation;
                merged.source = first.source;
                merged.operation = first.operation;

                output.push_back(std::move(merged));
            }
            i = j;
        }
    }

    return output;
}

// ---------------------------------------------------------------------------
// detect_dependencies()
// ---------------------------------------------------------------------------

void Shuffler::detect_dependencies(std::vector<LabelData>& labels) {
    // Group labels by granularity key.
    std::unordered_map<std::string, std::vector<size_t>> groups;
    for (size_t i = 0; i < labels.size(); ++i) {
        groups[group_key(labels[i], config_.dep_granularity)].push_back(i);
    }

    for (auto& [key, indices] : groups) {
        // Sort indices by label ID for total ordering.
        std::sort(indices.begin(), indices.end(),
                  [&](size_t a, size_t b) {
                      return labels[a].id < labels[b].id;
                  });

        // Compare every pair within the group.
        for (size_t ii = 0; ii < indices.size(); ++ii) {
            for (size_t jj = ii + 1; jj < indices.size(); ++jj) {
                auto& earlier = labels[indices[ii]];
                auto& later   = labels[indices[jj]];

                auto r_earlier = label_range(earlier);
                auto r_later   = label_range(later);

                if (!ranges_overlap(r_earlier, r_later)) continue;

                // Both reads produce no hazard.
                if (earlier.type == LabelType::Read
                    && later.type == LabelType::Read) continue;

                HazardType hazard;
                if (earlier.type == LabelType::Write
                    && later.type == LabelType::Read) {
                    hazard = HazardType::RAW;
                } else if (earlier.type == LabelType::Write
                           && later.type == LabelType::Write) {
                    hazard = HazardType::WAW;
                } else {
                    // Read then Write
                    hazard = HazardType::WAR;
                }

                later.dependencies.push_back({earlier.id, hazard});
            }
        }
    }
}

// ---------------------------------------------------------------------------
// build_supertasks()
// ---------------------------------------------------------------------------

std::vector<Supertask> Shuffler::build_supertasks(
    std::vector<LabelData>& labels) {

    // Collect all label IDs involved in any dependency relationship.
    std::unordered_set<uint64_t> involved;
    for (auto& l : labels) {
        if (!l.dependencies.empty()) {
            involved.insert(l.id);
            for (auto& dep : l.dependencies) {
                involved.insert(dep.label_id);
            }
        }
    }

    // Partition into dependent and independent.
    std::vector<LabelData> dependent;
    std::vector<LabelData> independent;

    for (auto& l : labels) {
        if (involved.count(l.id)) {
            dependent.push_back(std::move(l));
        } else {
            independent.push_back(std::move(l));
        }
    }

    // Group dependent labels by the same granularity key used for detection.
    std::unordered_map<std::string, std::vector<LabelData>> dep_groups;
    std::vector<std::string> dep_group_order;

    for (auto& l : dependent) {
        auto gk = group_key(l, config_.dep_granularity);
        auto [it, inserted] = dep_groups.try_emplace(gk);
        if (inserted) {
            dep_group_order.push_back(gk);
        }
        it->second.push_back(std::move(l));
    }

    std::vector<Supertask> supertasks;

    for (auto& key : dep_group_order) {
        auto& group = dep_groups[key];

        // Sort by ID for execution order.
        std::sort(group.begin(), group.end(),
                  [](const LabelData& a, const LabelData& b) {
                      return a.id < b.id;
                  });

        Supertask st;
        auto& first = group.front();

        st.composite.id = first.id;
        st.composite.type = LabelType::Composite;
        st.composite.file_key = first.file_key;
        st.composite.app_id = first.app_id;
        st.composite.priority = first.priority;
        st.composite.flags = first.flags;

        for (auto& child : group) {
            st.composite.children.push_back(child.id);
            child.supertask_id = st.composite.id;
        }

        st.children = std::move(group);
        supertasks.push_back(std::move(st));
    }

    // Replace labels with only the independent ones.
    labels = std::move(independent);

    return supertasks;
}

// ---------------------------------------------------------------------------
// pack_labels / unpack_labels
// ---------------------------------------------------------------------------

std::vector<std::byte> pack_labels(
    const std::vector<std::vector<std::byte>>& labels) {

    // Calculate total size: header (count) + per-label (len + data).
    size_t total = sizeof(uint32_t);
    for (auto& l : labels) {
        total += sizeof(uint32_t) + l.size();
    }

    std::vector<std::byte> buf(total);
    size_t pos = 0;

    auto count = static_cast<uint32_t>(labels.size());
    std::memcpy(buf.data() + pos, &count, sizeof(count));
    pos += sizeof(count);

    for (auto& l : labels) {
        auto len = static_cast<uint32_t>(l.size());
        std::memcpy(buf.data() + pos, &len, sizeof(len));
        pos += sizeof(len);
        std::memcpy(buf.data() + pos, l.data(), l.size());
        pos += l.size();
    }

    return buf;
}

std::vector<std::vector<std::byte>> unpack_labels(
    std::span<const std::byte> packed) {

    if (packed.size() < sizeof(uint32_t)) {
        return {};
    }

    size_t pos = 0;
    uint32_t count = 0;
    std::memcpy(&count, packed.data() + pos, sizeof(count));
    pos += sizeof(count);

    std::vector<std::vector<std::byte>> result;
    result.reserve(count);

    for (uint32_t i = 0; i < count; ++i) {
        if (pos + sizeof(uint32_t) > packed.size()) break;

        uint32_t len = 0;
        std::memcpy(&len, packed.data() + pos, sizeof(len));
        pos += sizeof(len);

        if (pos + len > packed.size()) break;

        result.emplace_back(packed.data() + pos, packed.data() + pos + len);
        pos += len;
    }

    return result;
}

} // namespace labios
