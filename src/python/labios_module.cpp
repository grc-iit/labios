#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>

#include <labios/client.h>
#include <labios/config.h>
#include <labios/label.h>

namespace py = pybind11;

PYBIND11_MODULE(_labios, m) {
    m.doc() = "LABIOS 2.0 Python SDK";

    // --- Enums ---
    py::enum_<labios::LabelType>(m, "LabelType")
        .value("Read", labios::LabelType::Read)
        .value("Write", labios::LabelType::Write)
        .value("Delete", labios::LabelType::Delete)
        .value("Flush", labios::LabelType::Flush)
        .value("Composite", labios::LabelType::Composite)
        .value("Observe", labios::LabelType::Observe);

    py::enum_<labios::Intent>(m, "Intent")
        .value("NONE", labios::Intent::None)
        .value("CHECKPOINT", labios::Intent::Checkpoint)
        .value("CACHE", labios::Intent::Cache)
        .value("TOOL_OUTPUT", labios::Intent::ToolOutput)
        .value("FINAL_RESULT", labios::Intent::FinalResult)
        .value("INTERMEDIATE", labios::Intent::Intermediate)
        .value("SHARED_STATE", labios::Intent::SharedState)
        .value("EMBEDDING", labios::Intent::Embedding)
        .value("MODEL_WEIGHT", labios::Intent::ModelWeight)
        .value("KV_CACHE", labios::Intent::KVCache)
        .value("REASONING_TRACE", labios::Intent::ReasoningTrace);

    py::enum_<labios::Isolation>(m, "Isolation")
        .value("NONE", labios::Isolation::None)
        .value("AGENT", labios::Isolation::Agent)
        .value("WORKSPACE", labios::Isolation::Workspace)
        .value("GLOBAL", labios::Isolation::Global);

    py::enum_<labios::Durability>(m, "Durability")
        .value("EPHEMERAL", labios::Durability::Ephemeral)
        .value("DURABLE", labios::Durability::Durable);

    // --- Config ---
    py::class_<labios::Config>(m, "Config")
        .def(py::init<>())
        .def_readwrite("nats_url", &labios::Config::nats_url)
        .def_readwrite("redis_host", &labios::Config::redis_host)
        .def_readwrite("redis_port", &labios::Config::redis_port);

    m.def("load_config", &labios::load_config, py::arg("path"),
          "Load configuration from a TOML file");

    // --- PendingIO ---
    py::class_<labios::PendingIO>(m, "PendingIO")
        .def(py::init<>());

    // --- Client ---
    py::class_<labios::Client>(m, "Client")
        .def(py::init<const labios::Config&>(), py::arg("config"))

        // Sync I/O
        .def("write", [](labios::Client& self, const std::string& path,
                          py::bytes data, uint64_t offset) {
            std::string d = data;
            self.write(path, std::as_bytes(std::span(d)), offset);
        }, py::arg("filepath"), py::arg("data"), py::arg("offset") = 0)

        .def("read", [](labios::Client& self, const std::string& path,
                         uint64_t offset, uint64_t size) -> py::bytes {
            auto result = self.read(path, offset, size);
            return py::bytes(reinterpret_cast<const char*>(result.data()),
                             result.size());
        }, py::arg("filepath"), py::arg("offset"), py::arg("size"))

        // Async I/O
        .def("async_write", [](labios::Client& self, const std::string& path,
                                py::bytes data, uint64_t offset) {
            std::string d = data;
            return self.async_write(path, std::as_bytes(std::span(d)), offset);
        }, py::arg("filepath"), py::arg("data"), py::arg("offset") = 0)

        .def("async_read", &labios::Client::async_read,
             py::arg("filepath"), py::arg("offset"), py::arg("size"))

        .def("wait", &labios::Client::wait, py::arg("status"))

        .def("wait_read", [](labios::Client& self, labios::PendingIO& status) -> py::bytes {
            auto result = self.wait_read(status);
            return py::bytes(reinterpret_cast<const char*>(result.data()),
                             result.size());
        }, py::arg("status"))

        // URI-based I/O
        .def("write_to", [](labios::Client& self, const std::string& uri,
                             py::bytes data) {
            std::string d = data;
            self.write_to(uri, std::as_bytes(std::span(d)));
        }, py::arg("dest_uri"), py::arg("data"))

        .def("read_from", [](labios::Client& self, const std::string& uri,
                              uint64_t size) -> py::bytes {
            auto result = self.read_from(uri, size);
            return py::bytes(reinterpret_cast<const char*>(result.data()),
                             result.size());
        }, py::arg("source_uri"), py::arg("size"))

        // Channels
        .def("publish_to_channel", [](labios::Client& self, const std::string& name,
                                       py::bytes data, uint64_t label_id) {
            std::string d = data;
            return self.publish_to_channel(name, std::as_bytes(std::span(d)), label_id);
        }, py::arg("channel"), py::arg("data"), py::arg("label_id") = 0)

        // Workspaces
        .def("workspace_put", [](labios::Client& self, const std::string& ws,
                                  const std::string& key, py::bytes data) {
            std::string d = data;
            return self.workspace_put(ws, key, std::as_bytes(std::span(d)));
        }, py::arg("workspace"), py::arg("key"), py::arg("data"))

        .def("workspace_get", [](labios::Client& self, const std::string& ws,
                                  const std::string& key) -> py::object {
            auto result = self.workspace_get(ws, key);
            if (!result) return py::none();
            return py::bytes(reinterpret_cast<const char*>(result->data()),
                             result->size());
        }, py::arg("workspace"), py::arg("key"))

        .def("workspace_del", &labios::Client::workspace_del,
             py::arg("workspace"), py::arg("key"))

        .def("workspace_grant", &labios::Client::workspace_grant,
             py::arg("workspace"), py::arg("app_id"))

        // Observability
        .def("observe", [](labios::Client& self, const std::string& query) {
            return self.observe(query);
        }, py::arg("query"))

        .def("get_config", &labios::Client::get_config);

    // --- Convenience connect function ---
    m.def("connect", [](const std::string& config_path) {
        auto cfg = labios::load_config(config_path);
        return std::make_unique<labios::Client>(cfg);
    }, py::arg("config_path") = "conf/labios.toml",
       "Connect to LABIOS using a config file");

    m.def("connect_to", [](const std::string& nats_url,
                            const std::string& redis_host, int redis_port) {
        labios::Config cfg;
        cfg.nats_url = nats_url;
        cfg.redis_host = redis_host;
        cfg.redis_port = redis_port;
        return std::make_unique<labios::Client>(cfg);
    }, py::arg("nats_url") = "nats://localhost:4222",
       py::arg("redis_host") = "localhost",
       py::arg("redis_port") = 6379,
       "Connect to LABIOS with explicit connection parameters");
}
