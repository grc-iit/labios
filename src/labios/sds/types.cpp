#include <labios/sds/types.h>

#include <charconv>
#include <sstream>
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

    std::istringstream stream{std::string(s)};
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty()) continue;

        PipelineStage stage;
        size_t pos = 0;

        // operation
        auto tab1 = line.find('\t', pos);
        if (tab1 == std::string::npos)
            throw std::runtime_error("malformed pipeline stage: missing operation");
        stage.operation = line.substr(pos, tab1 - pos);
        pos = tab1 + 1;

        // args
        auto tab2 = line.find('\t', pos);
        if (tab2 == std::string::npos)
            throw std::runtime_error("malformed pipeline stage: missing args");
        stage.args = line.substr(pos, tab2 - pos);
        pos = tab2 + 1;

        // input_stage
        auto tab3 = line.find('\t', pos);
        if (tab3 == std::string::npos)
            throw std::runtime_error("malformed pipeline stage: missing input_stage");
        auto is_str = line.substr(pos, tab3 - pos);
        std::from_chars(is_str.data(), is_str.data() + is_str.size(), stage.input_stage);
        pos = tab3 + 1;

        // output_stage
        auto os_str = line.substr(pos);
        std::from_chars(os_str.data(), os_str.data() + os_str.size(), stage.output_stage);

        p.stages.push_back(std::move(stage));
    }
    return p;
}

} // namespace labios::sds
