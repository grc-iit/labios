#include <labios/sds/program_repo.h>

#include <algorithm>
#include <charconv>
#include <cstring>
#include <random>

namespace labios::sds {
namespace {

// ---- builtin://identity ----
StageResult fn_identity(std::span<const std::byte> input, std::string_view /*args*/) {
    return {true, {}, {input.begin(), input.end()}};
}

// ---- builtin://compress_rle ----
// Simple run-length encoding: [byte, count_u8] pairs.
// If a run exceeds 255, it emits multiple pairs.
StageResult fn_compress_rle(std::span<const std::byte> input, std::string_view /*args*/) {
    if (input.empty()) return {true, {}, {}};

    std::vector<std::byte> out;
    out.reserve(input.size());

    size_t i = 0;
    while (i < input.size()) {
        std::byte val = input[i];
        size_t run = 1;
        while (i + run < input.size() && input[i + run] == val && run < 255) {
            ++run;
        }
        out.push_back(val);
        out.push_back(static_cast<std::byte>(run));
        i += run;
    }
    return {true, {}, std::move(out)};
}

// ---- builtin://decompress_rle ----
StageResult fn_decompress_rle(std::span<const std::byte> input, std::string_view /*args*/) {
    if (input.empty()) return {true, {}, {}};
    if (input.size() % 2 != 0)
        return {false, "RLE data must have even length (byte,count pairs)", {}};

    std::vector<std::byte> out;
    for (size_t i = 0; i < input.size(); i += 2) {
        std::byte val = input[i];
        auto count = static_cast<uint8_t>(input[i + 1]);
        out.insert(out.end(), count, val);
    }
    return {true, {}, std::move(out)};
}

// ---- builtin://filter_bytes ----
// Keep only bytes matching a single-byte pattern (args = decimal byte value).
StageResult fn_filter_bytes(std::span<const std::byte> input, std::string_view args) {
    if (args.empty())
        return {false, "filter_bytes requires a pattern byte (decimal value)", {}};

    int val = 0;
    auto [ptr, ec] = std::from_chars(args.data(), args.data() + args.size(), val);
    if (ec != std::errc{})
        return {false, "filter_bytes: invalid pattern byte", {}};

    auto pattern = static_cast<std::byte>(val);
    std::vector<std::byte> out;
    for (auto b : input) {
        if (b == pattern) out.push_back(b);
    }
    return {true, {}, std::move(out)};
}

// ---- builtin://sum_uint64 ----
StageResult fn_sum_uint64(std::span<const std::byte> input, std::string_view /*args*/) {
    size_t count = input.size() / sizeof(uint64_t);
    if (count == 0) {
        uint64_t zero = 0;
        std::vector<std::byte> out(sizeof(uint64_t));
        std::memcpy(out.data(), &zero, sizeof(uint64_t));
        return {true, {}, std::move(out)};
    }

    uint64_t sum = 0;
    for (size_t i = 0; i < count; ++i) {
        uint64_t v;
        std::memcpy(&v, input.data() + i * sizeof(uint64_t), sizeof(uint64_t));
        sum += v;
    }

    std::vector<std::byte> out(sizeof(uint64_t));
    std::memcpy(out.data(), &sum, sizeof(uint64_t));
    return {true, {}, std::move(out)};
}

// ---- builtin://sort_uint64 ----
StageResult fn_sort_uint64(std::span<const std::byte> input, std::string_view /*args*/) {
    size_t count = input.size() / sizeof(uint64_t);
    if (count == 0) return {true, {}, {}};

    std::vector<uint64_t> values(count);
    std::memcpy(values.data(), input.data(), count * sizeof(uint64_t));
    std::sort(values.begin(), values.end());

    std::vector<std::byte> out(count * sizeof(uint64_t));
    std::memcpy(out.data(), values.data(), out.size());
    return {true, {}, std::move(out)};
}

// ---- builtin://sample ----
// Return a random sample of N bytes from input. Args = N as decimal string.
StageResult fn_sample(std::span<const std::byte> input, std::string_view args) {
    if (args.empty())
        return {false, "sample requires N (number of bytes)", {}};

    size_t n = 0;
    auto [ptr, ec] = std::from_chars(args.data(), args.data() + args.size(), n);
    if (ec != std::errc{})
        return {false, "sample: invalid N", {}};

    if (n >= input.size())
        return {true, {}, {input.begin(), input.end()}};

    // Fisher-Yates sample: pick n indices.
    std::vector<size_t> indices(input.size());
    std::iota(indices.begin(), indices.end(), 0);

    std::mt19937 rng(42); // deterministic seed for reproducibility
    for (size_t i = 0; i < n; ++i) {
        std::uniform_int_distribution<size_t> dist(i, indices.size() - 1);
        std::swap(indices[i], indices[dist(rng)]);
    }
    indices.resize(n);
    std::sort(indices.begin(), indices.end()); // preserve order

    std::vector<std::byte> out;
    out.reserve(n);
    for (auto idx : indices) {
        out.push_back(input[idx]);
    }
    return {true, {}, std::move(out)};
}

// ---- builtin://truncate ----
// Return first N bytes of input.
StageResult fn_truncate(std::span<const std::byte> input, std::string_view args) {
    if (args.empty())
        return {false, "truncate requires N (number of bytes)", {}};

    size_t n = 0;
    auto [ptr, ec] = std::from_chars(args.data(), args.data() + args.size(), n);
    if (ec != std::errc{})
        return {false, "truncate: invalid N", {}};

    size_t take = std::min(n, input.size());
    return {true, {}, {input.begin(), input.begin() + take}};
}

} // anonymous namespace

void register_all_builtins(ProgramRepository& repo) {
    repo.register_function("builtin://identity",        fn_identity);
    repo.register_function("builtin://compress_rle",     fn_compress_rle);
    repo.register_function("builtin://decompress_rle",   fn_decompress_rle);
    repo.register_function("builtin://filter_bytes",     fn_filter_bytes);
    repo.register_function("builtin://sum_uint64",       fn_sum_uint64);
    repo.register_function("builtin://sort_uint64",      fn_sort_uint64);
    repo.register_function("builtin://sample",           fn_sample);
    repo.register_function("builtin://truncate",         fn_truncate);
}

} // namespace labios::sds
