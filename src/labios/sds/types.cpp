#include <labios/sds/types.h>

#include <charconv>
#include <stdexcept>

namespace labios::sds {

// Wire format: one line per stage, fields separated by '\t'.
//   operation \t args \t input_stage \t output_stage
// Empty pipelines serialize to "".

std::string serialize_pipeline(const Pipeline& p) {
    if (p.empty()) return {};

    std::string out;
    for (size_t i = 0; i < p.stages.size(); ++i) {
        if (i > 0) out += '\n';
        auto& s = p.stages[i];
        out += s.operation;
        out += '\t';
        out += s.args;
        out += '\t';
        out += std::to_string(s.input_stage);
        out += '\t';
        out += std::to_string(s.output_stage);
    }
    return out;
}

Pipeline deserialize_pipeline(std::string_view s) {
    Pipeline p;
    if (s.empty()) return p;

    // Parse directly from string_view without istringstream or string copies.
    size_t line_start = 0;
    while (line_start < s.size()) {
        auto line_end = s.find('\n', line_start);
        if (line_end == std::string_view::npos) line_end = s.size();
        auto line = s.substr(line_start, line_end - line_start);
        line_start = line_end + 1;

        if (line.empty()) continue;

        PipelineStage stage;
        size_t pos = 0;

        // operation
        auto tab1 = line.find('\t', pos);
        if (tab1 == std::string_view::npos)
            throw std::runtime_error("malformed pipeline stage: missing operation");
        stage.operation = std::string(line.substr(pos, tab1 - pos));
        pos = tab1 + 1;

        // args
        auto tab2 = line.find('\t', pos);
        if (tab2 == std::string_view::npos)
            throw std::runtime_error("malformed pipeline stage: missing args");
        stage.args = std::string(line.substr(pos, tab2 - pos));
        pos = tab2 + 1;

        // input_stage
        auto tab3 = line.find('\t', pos);
        if (tab3 == std::string_view::npos)
            throw std::runtime_error("malformed pipeline stage: missing input_stage");
        auto is_sv = line.substr(pos, tab3 - pos);
        std::from_chars(is_sv.data(), is_sv.data() + is_sv.size(), stage.input_stage);
        pos = tab3 + 1;

        // output_stage
        auto os_sv = line.substr(pos);
        std::from_chars(os_sv.data(), os_sv.data() + os_sv.size(), stage.output_stage);

        p.stages.push_back(std::move(stage));
    }
    return p;
}

} // namespace labios::sds
