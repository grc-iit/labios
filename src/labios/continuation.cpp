#include <labios/continuation.h>

#include <charconv>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace labios {

namespace {

std::string to_hex(const std::vector<std::byte>& bytes) {
    static constexpr char hex_chars[] = "0123456789abcdef";
    std::string result;
    result.reserve(bytes.size() * 2);
    for (auto b : bytes) {
        auto val = static_cast<uint8_t>(b);
        result.push_back(hex_chars[val >> 4]);
        result.push_back(hex_chars[val & 0x0F]);
    }
    return result;
}

std::vector<std::byte> from_hex(std::string_view hex) {
    if (hex.size() % 2 != 0) {
        throw std::runtime_error("invalid hex string length");
    }
    std::vector<std::byte> bytes;
    bytes.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        auto nibble = [](char c) -> uint8_t {
            if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
            if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(c - 'a' + 10);
            if (c >= 'A' && c <= 'F') return static_cast<uint8_t>(c - 'A' + 10);
            throw std::runtime_error("invalid hex character");
        };
        bytes.push_back(static_cast<std::byte>(
            (nibble(hex[i]) << 4) | nibble(hex[i + 1])));
    }
    return bytes;
}

std::string_view trim(std::string_view sv) {
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t'))
        sv.remove_prefix(1);
    while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t'))
        sv.remove_suffix(1);
    return sv;
}

struct OpPos {
    size_t start;
    size_t len;
};

OpPos find_operator(std::string_view expr) {
    // Two-char operators first to avoid partial matches.
    auto pos = expr.find("==");
    if (pos != std::string_view::npos) return {pos, 2};
    pos = expr.find("!=");
    if (pos != std::string_view::npos) return {pos, 2};
    pos = expr.find('>');
    if (pos != std::string_view::npos) return {pos, 1};
    pos = expr.find('<');
    if (pos != std::string_view::npos) return {pos, 1};
    return {std::string_view::npos, 0};
}

std::string status_string(CompletionStatus s) {
    switch (s) {
    case CompletionStatus::Complete: return "Complete";
    case CompletionStatus::Error:    return "Error";
    }
    return "Unknown";
}

} // anonymous namespace

std::string encode_chain_params(const LabelData& label_template) {
    auto bytes = serialize_label(label_template);
    return to_hex(bytes);
}

LabelData decode_chain_params(std::string_view encoded) {
    auto bytes = from_hex(encoded);
    return deserialize_label(bytes);
}

bool evaluate_condition(std::string_view condition,
                        const LabelData& completed_label,
                        const CompletionData& completion) {
    auto trimmed = trim(condition);
    auto op = find_operator(trimmed);
    if (op.start == std::string_view::npos) {
        throw std::runtime_error(
            "invalid condition: no operator found in '"
            + std::string(condition) + "'");
    }

    auto field = trim(trimmed.substr(0, op.start));
    auto op_str = trimmed.substr(op.start, op.len);
    auto value = trim(trimmed.substr(op.start + op.len));

    // Strip surrounding quotes from value if present.
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value = value.substr(1, value.size() - 2);
    }

    auto compare_string = [&](std::string_view actual) -> bool {
        if (op_str == "==") return actual == value;
        if (op_str == "!=") return actual != value;
        throw std::runtime_error(
            "operator " + std::string(op_str)
            + " not supported for string field " + std::string(field));
    };

    auto compare_numeric = [&](uint64_t actual) -> bool {
        uint64_t rhs = 0;
        auto [ptr, ec] = std::from_chars(
            value.data(), value.data() + value.size(), rhs);
        if (ec != std::errc{}) {
            throw std::runtime_error(
                "invalid numeric value: " + std::string(value));
        }
        if (op_str == "==") return actual == rhs;
        if (op_str == "!=") return actual != rhs;
        if (op_str == ">")  return actual > rhs;
        if (op_str == "<")  return actual < rhs;
        return false;
    };

    if (field == "status") return compare_string(status_string(completion.status));
    if (field == "error")  return compare_string(completion.error);
    if (field == "data_size") return compare_numeric(completed_label.data_size);
    if (field == "label_id")  return compare_numeric(completion.label_id);

    throw std::runtime_error("unknown condition field: " + std::string(field));
}

std::optional<LabelData> process_continuation(
    const LabelData& completed_label,
    const CompletionData& completion,
    ChannelRegistry& channels,
    transport::NatsConnection& /*nats*/,
    transport::RedisConnection& /*redis*/) {

    const auto& cont = completed_label.continuation;

    switch (cont.kind) {
    case ContinuationKind::None:
        return std::nullopt;

    case ContinuationKind::Notify: {
        auto* ch = channels.get(cont.target_channel);
        if (!ch) {
            ch = channels.create(cont.target_channel);
        }
        if (ch) {
            auto comp_bytes = serialize_completion(completion);
            ch->publish(std::span<const std::byte>(comp_bytes),
                        completed_label.id);
        }
        return std::nullopt;
    }

    case ContinuationKind::Chain: {
        auto chained = decode_chain_params(cont.chain_params);
        chained.id = generate_label_id(completed_label.app_id);
        chained.app_id = completed_label.app_id;
        chained.status = StatusCode::Created;
        return chained;
    }

    case ContinuationKind::Conditional: {
        if (!evaluate_condition(cont.condition, completed_label, completion)) {
            return std::nullopt;
        }
        auto chained = decode_chain_params(cont.chain_params);
        chained.id = generate_label_id(completed_label.app_id);
        chained.app_id = completed_label.app_id;
        chained.status = StatusCode::Created;
        return chained;
    }
    }

    return std::nullopt;
}

} // namespace labios
