#include <labios/client.h>
#include <labios/config.h>

#include <chrono>
#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    try {
        const std::string config_path = argc > 1 ? argv[1] : "conf/labios.toml";
        auto client = labios::connect(labios::load_config(config_path));
        std::vector<std::byte> data(4096, std::byte{0x2a});

        // Label-level submission carries the same intent as convenience calls.
        labios::LabelParams params{};
        params.type = labios::LabelType::Write;
        params.dest_uri = "file:///examples/native-label.bin";
        params.intent = labios::Intent::Checkpoint;
        params.priority = 200;
        auto operation = client.publish(client.create_label(params), data);

        // Timeout is typed and nonterminal; the same handle remains reusable.
        auto observed = operation.wait_for(std::chrono::milliseconds(1));
        if (observed.state == labios::CompletionState::Timeout) {
            observed = operation.wait_all(std::chrono::seconds(30));
        }
        if (observed.state != labios::CompletionState::Complete) {
            const auto cancelled = operation.cancel();
            std::cerr << "label did not complete; cancel outcomes="
                      << cancelled.size() << '\n';
            return 1;
        }

        // URI conveniences compile into the same typed Label I/O path.
        client.write_to("file:///examples/native-uri.bin", data);
        const auto roundtrip = client.read_from(
            "file:///examples/native-uri.bin", data.size());
        if (roundtrip != data) return 1;

        labios::sds::Pipeline pipeline;
        pipeline.stages.push_back({"builtin://identity", "", -1, -1});
        auto pipeline_operation = client.execute_pipeline(
            "file:///examples/native-uri.bin",
            "sqlite:///examples/native-pipeline-result",
            pipeline, labios::Intent::Intermediate);
        client.wait(pipeline_operation);

        std::cout << client.observe("system/health") << '\n';
        return 0;
    } catch (const labios::CompletionError& error) {
        std::cerr << "completion error for label " << error.label_id()
                  << ": " << error.what() << '\n';
        return 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
