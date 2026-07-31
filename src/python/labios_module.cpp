#include <pybind11/functional.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <labios/client.h>
#include <labios/config.h>
#include <labios/label.h>

#include <chrono>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace py = pybind11;
using namespace std::chrono_literals;

namespace {

std::span<const std::byte> bytes_span(const std::string& value) {
    return std::as_bytes(std::span(value));
}

py::bytes python_bytes(const std::vector<std::byte>& value) {
    return py::bytes(reinterpret_cast<const char*>(value.data()), value.size());
}

std::chrono::milliseconds milliseconds(uint64_t value) {
    return std::chrono::milliseconds(value);
}

struct PythonExceptions {
    PyObject* base = nullptr;
    PyObject* client = nullptr;
    PyObject* invalid_argument = nullptr;
    PyObject* lookup = nullptr;
    PyObject* session = nullptr;
    PyObject* protocol = nullptr;
    PyObject* submission = nullptr;
    PyObject* completion = nullptr;
    PyObject* timeout = nullptr;
    PyObject* cancelled = nullptr;
    PyObject* malformed = nullptr;
    PyObject* unsupported_version = nullptr;
    PyObject* validation = nullptr;
    PyObject* authorization = nullptr;
    PyObject* resource = nullptr;
    PyObject* pipeline = nullptr;
    PyObject* dependency = nullptr;
    PyObject* backend = nullptr;
    PyObject* expired = nullptr;
    PyObject* execution = nullptr;
};

PythonExceptions exceptions;

PyObject* make_exception(py::module_& module, const char* name, PyObject* base) {
    const auto qualified = std::string(py::str(module.attr("__name__"))) + "." + name;
    PyObject* value = PyErr_NewException(qualified.c_str(), base, nullptr);
    if (!value) throw py::error_already_set();
    module.attr(name) = py::reinterpret_borrow<py::object>(value);
    return value;
}

PyObject* exception_for_category(std::string_view category) {
    if (category == "MALFORMED_BUFFER" || category == "INVALID_ENUM") {
        return exceptions.malformed;
    }
    if (category == "UNSUPPORTED_IR_VERSION" || category == "UNSUPPORTED_VERSION") {
        return exceptions.unsupported_version;
    }
    if (category == "AUTHORIZATION_FAILED" || category == "UNAUTHORIZED_MUTATION") {
        return exceptions.authorization;
    }
    if (category == "UNKNOWN_RESOURCE" || category == "FORBIDDEN_INTERNAL_RESOURCE" ||
        category == "INVALID_RESOURCE" || category == "UNSAFE_MEMORY_REFERENCE" ||
        category == "RESOURCE_VERSION_CONFLICT" ||
        category == "STAGED_CONTENT_UNAVAILABLE") {
        return exceptions.resource;
    }
    if (category == "INVALID_PIPELINE") return exceptions.pipeline;
    if (category == "INVALID_DEPENDENCY" || category == "DEPENDENCY_FAILED" ||
        category == "COMPOSITE_ABORTED") {
        return exceptions.dependency;
    }
    if (category == "BACKEND_UNSUPPORTED" ||
        category == "UNSATISFIABLE_REQUIREMENTS") {
        return exceptions.backend;
    }
    if (category == "CANCELED") return exceptions.cancelled;
    if (category == "EXPIRED") return exceptions.expired;
    if (category == "EXECUTION_FAILED") return exceptions.execution;
    if (!category.empty()) return exceptions.validation;
    return exceptions.completion;
}

std::string category_from_message(std::string_view message) {
    static constexpr std::string_view marker = "did not complete: ";
    const auto start = message.find(marker);
    if (start == std::string_view::npos) return {};
    const auto category_start = start + marker.size();
    const auto end = message.find(':', category_start);
    if (end == std::string_view::npos) return {};
    return std::string(message.substr(category_start, end - category_start));
}

void install_exceptions(py::module_& module) {
    exceptions.base = make_exception(module, "LabiosError", PyExc_Exception);
    exceptions.client = make_exception(module, "ClientError", exceptions.base);
    exceptions.invalid_argument =
        make_exception(module, "InvalidArgumentError", exceptions.client);
    exceptions.lookup = make_exception(module, "CompletionLookupError", exceptions.client);
    exceptions.session = make_exception(module, "SessionShutdownError", exceptions.client);
    exceptions.protocol = make_exception(module, "ProtocolError", exceptions.client);
    exceptions.submission = make_exception(module, "SubmissionError", exceptions.client);
    exceptions.completion = make_exception(module, "CompletionError", exceptions.base);
    exceptions.timeout = make_exception(module, "TimeoutError", exceptions.completion);
    exceptions.cancelled = make_exception(module, "CancelledError", exceptions.completion);
    exceptions.malformed = make_exception(module, "MalformedBufferError", exceptions.protocol);
    exceptions.unsupported_version =
        make_exception(module, "UnsupportedVersionError", exceptions.protocol);
    exceptions.validation = make_exception(module, "ValidationError", exceptions.submission);
    exceptions.authorization =
        make_exception(module, "AuthorizationError", exceptions.submission);
    exceptions.resource = make_exception(module, "ResourceError", exceptions.submission);
    exceptions.pipeline = make_exception(module, "PipelineError", exceptions.submission);
    exceptions.dependency = make_exception(module, "DependencyError", exceptions.submission);
    exceptions.backend = make_exception(module, "BackendError", exceptions.completion);
    exceptions.expired = make_exception(module, "ExpiredError", exceptions.completion);
    exceptions.execution = make_exception(module, "ExecutionError", exceptions.completion);

    py::register_exception_translator([](std::exception_ptr pointer) {
        try {
            if (pointer) std::rethrow_exception(pointer);
        } catch (const labios::ClientError& error) {
            PyObject* type = exceptions.client;
            switch (error.code()) {
            case labios::ClientErrorCode::InvalidArgument:
                type = exceptions.invalid_argument;
                break;
            case labios::ClientErrorCode::LookupFailed:
                type = exceptions.lookup;
                break;
            case labios::ClientErrorCode::SessionShutdown:
                type = exceptions.session;
                break;
            case labios::ClientErrorCode::ProtocolError:
                type = exceptions.protocol;
                break;
            case labios::ClientErrorCode::SubmissionFailed:
                type = exception_for_category(error.category());
                break;
            }
            PyErr_SetString(type, error.what());
        } catch (const labios::CompletionError& error) {
            PyObject* type = exceptions.completion;
            if (error.state() == labios::CompletionState::Timeout) {
                type = exceptions.timeout;
            } else if (error.state() == labios::CompletionState::Cancelled) {
                type = exceptions.cancelled;
            } else {
                type = exception_for_category(category_from_message(error.what()));
            }
            PyErr_SetString(type, error.what());
        } catch (const labios::LabelDecodeError& error) {
            PyErr_SetString(exception_for_category(error.category()), error.what());
        }
    });
}

} // namespace

PYBIND11_MODULE(_labios, module) {
    module.doc() = "LABIOS 2.1 Python Label I/O SDK";
    install_exceptions(module);

    py::enum_<labios::LabelType>(module, "LabelType")
        .value("Read", labios::LabelType::Read)
        .value("Write", labios::LabelType::Write)
        .value("Delete", labios::LabelType::Delete)
        .value("Flush", labios::LabelType::Flush)
        .value("Composite", labios::LabelType::Composite)
        .value("Observe", labios::LabelType::Observe);
    py::enum_<labios::Intent>(module, "Intent")
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
    py::enum_<labios::Isolation>(module, "Isolation")
        .value("NONE", labios::Isolation::None)
        .value("AGENT", labios::Isolation::Agent)
        .value("WORKSPACE", labios::Isolation::Workspace)
        .value("GLOBAL", labios::Isolation::Global);
    py::enum_<labios::Durability>(module, "Durability")
        .value("EPHEMERAL", labios::Durability::Ephemeral)
        .value("DURABLE", labios::Durability::Durable);
    py::enum_<labios::ResourceFamily>(module, "ResourceFamily")
        .value("FILE_RANGE", labios::ResourceFamily::FileRange)
        .value("MEMORY", labios::ResourceFamily::Memory)
        .value("NETWORK", labios::ResourceFamily::Network)
        .value("KEY_VALUE", labios::ResourceFamily::KeyValue)
        .value("RELATIONAL", labios::ResourceFamily::Relational)
        .value("OBJECT", labios::ResourceFamily::Object)
        .value("VECTOR", labios::ResourceFamily::Vector)
        .value("GRAPH", labios::ResourceFamily::Graph)
        .value("CHANNEL", labios::ResourceFamily::Channel)
        .value("WORKSPACE", labios::ResourceFamily::Workspace)
        .value("EXTENSION", labios::ResourceFamily::Extension);
    py::enum_<labios::BindingProvenance>(module, "BindingProvenance")
        .value("DIRECT_PRODUCER", labios::BindingProvenance::DirectProducer)
        .value("MATERIALIZED_SOURCE", labios::BindingProvenance::MaterializedSource);
    py::enum_<labios::OperationKind>(module, "OperationKind")
        .value("GENERIC", labios::OperationKind::Generic)
        .value("READ", labios::OperationKind::Read);
    py::enum_<labios::CompletionState>(module, "CompletionState")
        .value("PENDING", labios::CompletionState::Pending)
        .value("COMPLETE", labios::CompletionState::Complete)
        .value("FAILED", labios::CompletionState::Failed)
        .value("CANCELLED", labios::CompletionState::Cancelled)
        .value("PARKED", labios::CompletionState::Parked)
        .value("TIMEOUT", labios::CompletionState::Timeout)
        .value("UNKNOWN", labios::CompletionState::Unknown);
    py::enum_<labios::CancellationState>(module, "CancellationState")
        .value("CANCELLED", labios::CancellationState::Cancelled)
        .value("TOO_LATE", labios::CancellationState::TooLate)
        .value("TERMINAL", labios::CancellationState::Terminal)
        .value("UNKNOWN", labios::CancellationState::Unknown);
    py::enum_<labios::LifecycleState>(module, "LifecycleState")
        .value("SUBMITTED", labios::LifecycleState::Submitted)
        .value("ADMITTED", labios::LifecycleState::Admitted)
        .value("QUEUED", labios::LifecycleState::Queued)
        .value("PARKED", labios::LifecycleState::Parked)
        .value("SHUFFLED", labios::LifecycleState::Shuffled)
        .value("SCHEDULED", labios::LifecycleState::Scheduled)
        .value("EXECUTING", labios::LifecycleState::Executing)
        .value("COMPLETED", labios::LifecycleState::Completed)
        .value("FAILED", labios::LifecycleState::Failed)
        .value("CANCELLED", labios::LifecycleState::Cancelled)
        .value("UNKNOWN", labios::LifecycleState::Unknown);
    py::enum_<labios::StatusCode>(module, "StatusCode")
        .value("CREATED", labios::StatusCode::Created)
        .value("QUEUED", labios::StatusCode::Queued)
        .value("SHUFFLED", labios::StatusCode::Shuffled)
        .value("SCHEDULED", labios::StatusCode::Scheduled)
        .value("EXECUTING", labios::StatusCode::Executing)
        .value("COMPLETE", labios::StatusCode::Complete)
        .value("FAILED", labios::StatusCode::Failed);

    py::class_<labios::Config>(module, "Config")
        .def(py::init<>())
        .def_readwrite("nats_url", &labios::Config::nats_url)
        .def_readwrite("redis_host", &labios::Config::redis_host)
        .def_readwrite("redis_port", &labios::Config::redis_port)
        .def_readwrite("reply_timeout_ms", &labios::Config::reply_timeout_ms);
    module.def("load_config", &labios::load_config, py::arg("path"));

    py::class_<labios::ResourceRef>(module, "ResourceRef")
        .def(py::init<>())
        .def_readwrite("family", &labios::ResourceRef::family)
        .def_readwrite("backend_id", &labios::ResourceRef::backend_id)
        .def_readwrite("logical_id", &labios::ResourceRef::logical_id)
        .def_readwrite("namespace_name", &labios::ResourceRef::namespace_name)
        .def_readwrite("database", &labios::ResourceRef::database)
        .def_readwrite("schema", &labios::ResourceRef::schema)
        .def_readwrite("path", &labios::ResourceRef::path)
        .def_readwrite("key", &labios::ResourceRef::key)
        .def_readwrite("selector", &labios::ResourceRef::selector)
        .def_readwrite("host", &labios::ResourceRef::host)
        .def_readwrite("transport", &labios::ResourceRef::transport)
        .def_readwrite("stream", &labios::ResourceRef::stream)
        .def_readwrite("bucket", &labios::ResourceRef::bucket)
        .def_readwrite("collection", &labios::ResourceRef::collection)
        .def_readwrite("item_id", &labios::ResourceRef::item_id)
        .def_readwrite("graph", &labios::ResourceRef::graph)
        .def_readwrite("element_id", &labios::ResourceRef::element_id)
        .def_readwrite("owner", &labios::ResourceRef::owner)
        .def_readwrite("allocation_id", &labios::ResourceRef::allocation_id)
        .def_readwrite("transfer_token", &labios::ResourceRef::transfer_token)
        .def_readwrite("extent", &labios::ResourceRef::extent)
        .def_readwrite("offset", &labios::ResourceRef::offset)
        .def_readwrite("length", &labios::ResourceRef::length)
        .def_readwrite("port", &labios::ResourceRef::port)
        .def_readwrite("schema_version", &labios::ResourceRef::schema_version)
        .def_readwrite("version_token", &labios::ResourceRef::version_token)
        .def_readwrite("version_exact", &labios::ResourceRef::version_exact)
        .def_readwrite("version_must_not_exist",
                       &labios::ResourceRef::version_must_not_exist);
    module.def("resource_from_uri", &labios::resource_from_uri, py::arg("uri"));

    py::class_<labios::StagedInputBinding>(module, "StagedInputBinding")
        .def(py::init<>())
        .def_readwrite("provenance", &labios::StagedInputBinding::provenance)
        .def_readwrite("content_id", &labios::StagedInputBinding::content_id)
        .def_readwrite("logical_length", &labios::StagedInputBinding::logical_length)
        .def_readwrite("digest_algorithm", &labios::StagedInputBinding::digest_algorithm)
        .def_readwrite("observed_version", &labios::StagedInputBinding::observed_version);
    py::class_<labios::sds::PipelineStage>(module, "PipelineStage")
        .def(py::init<>())
        .def(py::init([](std::string operation, std::string args,
                         int input_stage, int output_stage) {
            return labios::sds::PipelineStage{std::move(operation), std::move(args),
                                              input_stage, output_stage};
        }), py::arg("operation"), py::arg("args") = "",
           py::arg("input_stage") = -1, py::arg("output_stage") = -1)
        .def_readwrite("operation", &labios::sds::PipelineStage::operation)
        .def_readwrite("args", &labios::sds::PipelineStage::args)
        .def_readwrite("input_stage", &labios::sds::PipelineStage::input_stage)
        .def_readwrite("output_stage", &labios::sds::PipelineStage::output_stage);
    py::class_<labios::sds::Pipeline>(module, "Pipeline")
        .def(py::init<>())
        .def_readwrite("stages", &labios::sds::Pipeline::stages)
        .def("empty", &labios::sds::Pipeline::empty);

    py::class_<labios::LabelParams>(module, "LabelParams")
        .def(py::init<>())
        .def_readwrite("type", &labios::LabelParams::type)
        .def_readwrite("operation", &labios::LabelParams::operation)
        .def_readwrite("priority", &labios::LabelParams::priority)
        .def_readwrite("declared_dependencies", &labios::LabelParams::declared_dependencies)
        .def_readwrite("intent", &labios::LabelParams::intent)
        .def_property("source_resource",
            [](const labios::LabelParams& value) -> py::object {
                if (!value.has_source_resource) return py::none();
                return py::cast(value.source_resource);
            },
            [](labios::LabelParams& value, const std::optional<labios::ResourceRef>& resource) {
                value.has_source_resource = resource.has_value();
                if (resource) value.source_resource = *resource;
            })
        .def_property("destination_resource",
            [](const labios::LabelParams& value) -> py::object {
                if (!value.has_destination_resource) return py::none();
                return py::cast(value.destination_resource);
            },
            [](labios::LabelParams& value, const std::optional<labios::ResourceRef>& resource) {
                value.has_destination_resource = resource.has_value();
                if (resource) value.destination_resource = *resource;
            })
        .def_readwrite("ttl_seconds", &labios::LabelParams::ttl_seconds)
        .def_readwrite("isolation", &labios::LabelParams::isolation)
        .def_readwrite("version", &labios::LabelParams::version)
        .def_readwrite("durability", &labios::LabelParams::durability)
        .def_readwrite("source_uri", &labios::LabelParams::source_uri)
        .def_readwrite("dest_uri", &labios::LabelParams::dest_uri)
        .def_readwrite("pipeline", &labios::LabelParams::pipeline);

    py::class_<labios::ScoreComponent>(module, "ScoreComponent")
        .def_readonly("metric", &labios::ScoreComponent::metric)
        .def_readonly("raw_value", &labios::ScoreComponent::raw_value)
        .def_readonly("normalized_value", &labios::ScoreComponent::normalized_value)
        .def_readonly("weight", &labios::ScoreComponent::weight)
        .def_readonly("contribution", &labios::ScoreComponent::contribution);
    py::class_<labios::CandidateEvaluation>(module, "CandidateEvaluation")
        .def_readonly("worker_id", &labios::CandidateEvaluation::worker_id)
        .def_readonly("feasible", &labios::CandidateEvaluation::feasible)
        .def_readonly("reason_codes", &labios::CandidateEvaluation::reason_codes)
        .def_readonly("available_capacity_before",
                      &labios::CandidateEvaluation::available_capacity_before)
        .def_readonly("available_capacity_after",
                      &labios::CandidateEvaluation::available_capacity_after)
        .def_readonly("locality_match", &labios::CandidateEvaluation::locality_match)
        .def_readonly("score_components", &labios::CandidateEvaluation::score_components)
        .def_readonly("final_objective", &labios::CandidateEvaluation::final_objective)
        .def_readonly("policy_rank", &labios::CandidateEvaluation::policy_rank)
        .def_readonly("selected", &labios::CandidateEvaluation::selected);
    py::class_<labios::SchedulingDecisionSnapshot>(module, "PlacementDecision")
        .def_readonly("decision_id", &labios::SchedulingDecisionSnapshot::decision_id)
        .def_readonly("batch_id", &labios::SchedulingDecisionSnapshot::batch_id)
        .def_readonly("scheduling_unit_id",
                      &labios::SchedulingDecisionSnapshot::scheduling_unit_id)
        .def_readonly("attempt", &labios::SchedulingDecisionSnapshot::attempt)
        .def_readonly("registry_generation",
                      &labios::SchedulingDecisionSnapshot::registry_generation)
        .def_readonly("outcome", &labios::SchedulingDecisionSnapshot::outcome)
        .def_readonly("chosen_worker_id",
                      &labios::SchedulingDecisionSnapshot::chosen_worker_id)
        .def_readonly("park_reason", &labios::SchedulingDecisionSnapshot::park_reason)
        .def_readonly("reservation_bytes",
                      &labios::SchedulingDecisionSnapshot::reservation_bytes)
        .def_readonly("complete_size_known",
                      &labios::SchedulingDecisionSnapshot::complete_size_known)
        .def_readonly("candidates", &labios::SchedulingDecisionSnapshot::candidates)
        .def_readonly("policy_name", &labios::SchedulingDecisionSnapshot::policy_name)
        .def_readonly("policy_version", &labios::SchedulingDecisionSnapshot::policy_version)
        .def_readonly("tie_break", &labios::SchedulingDecisionSnapshot::tie_break);
    py::class_<labios::ScoreSnapshot>(module, "PlacementHistory")
        .def_readonly("decision_version", &labios::ScoreSnapshot::decision_version)
        .def_readonly("decisions", &labios::ScoreSnapshot::decisions);
    py::class_<labios::LabelData>(module, "Label")
        .def(py::init<>())
        .def_readwrite("id", &labios::LabelData::id)
        .def_readwrite("type", &labios::LabelData::type)
        .def_readwrite("operation", &labios::LabelData::operation)
        .def_readwrite("priority", &labios::LabelData::priority)
        .def_readwrite("declared_dependencies", &labios::LabelData::declared_dependencies)
        .def_readwrite("intent", &labios::LabelData::intent)
        .def_property_readonly("source_resource", [](const labios::LabelData& value) -> py::object {
            return value.has_source_resource ? py::cast(value.source_resource) : py::none();
        })
        .def_property_readonly("destination_resource", [](const labios::LabelData& value) -> py::object {
            return value.has_destination_resource ? py::cast(value.destination_resource) : py::none();
        })
        .def_readwrite("data_size", &labios::LabelData::data_size)
        .def_readwrite("ttl_seconds", &labios::LabelData::ttl_seconds)
        .def_readwrite("isolation", &labios::LabelData::isolation)
        .def_readwrite("durability", &labios::LabelData::durability)
        .def_readwrite("source_uri", &labios::LabelData::source_uri)
        .def_readwrite("dest_uri", &labios::LabelData::dest_uri)
        .def_readwrite("pipeline", &labios::LabelData::pipeline)
        .def_readonly("status", &labios::LabelData::status)
        .def_readonly("created_us", &labios::LabelData::created_us)
        .def_readonly("queued_us", &labios::LabelData::queued_us)
        .def_readonly("dispatched_us", &labios::LabelData::dispatched_us)
        .def_readonly("started_us", &labios::LabelData::started_us)
        .def_readonly("completed_us", &labios::LabelData::completed_us)
        .def_readonly("placement_history", &labios::LabelData::score_snapshot);

    py::class_<labios::CompletionResult>(module, "CompletionResult")
        .def_readonly("label_id", &labios::CompletionResult::label_id)
        .def_readonly("state", &labios::CompletionResult::state)
        .def_readonly("category", &labios::CompletionResult::category)
        .def_readonly("error", &labios::CompletionResult::error)
        .def_readonly("data_key", &labios::CompletionResult::data_key)
        .def_readonly("observation_version", &labios::CompletionResult::observation_version)
        .def_readonly("worker_id", &labios::CompletionResult::worker_id)
        .def_readonly("attempt", &labios::CompletionResult::attempt)
        .def_readonly("queued_us", &labios::CompletionResult::queued_us)
        .def_readonly("dispatched_us", &labios::CompletionResult::dispatched_us)
        .def_readonly("started_us", &labios::CompletionResult::started_us)
        .def_readonly("completed_us", &labios::CompletionResult::completed_us)
        .def_readonly("queue_delay_us", &labios::CompletionResult::queue_delay_us)
        .def_readonly("service_time_us", &labios::CompletionResult::service_time_us)
        .def_readonly("park_reason", &labios::CompletionResult::park_reason)
        .def_readonly("park_attempts", &labios::CompletionResult::park_attempts)
        .def_readonly("next_retry_at_ms", &labios::CompletionResult::next_retry_at_ms)
        .def_readonly("lifecycle", &labios::CompletionResult::lifecycle)
        .def_property_readonly("terminal", &labios::CompletionResult::terminal)
        .def_property_readonly("has_execution_observation",
                               &labios::CompletionResult::has_execution_observation);
    py::class_<labios::WaitResult>(module, "WaitResult")
        .def_readonly("state", &labios::WaitResult::state)
        .def_readonly("results", &labios::WaitResult::results);
    py::class_<labios::CancellationResult>(module, "CancellationResult")
        .def_readonly("label_id", &labios::CancellationResult::label_id)
        .def_readonly("state", &labios::CancellationResult::state)
        .def_readonly("completion", &labios::CancellationResult::completion);

    py::class_<labios::Operation>(module, "Operation")
        .def(py::init<>())
        .def_property_readonly("valid", &labios::Operation::valid)
        .def_property_readonly("empty", &labios::Operation::empty)
        .def_property_readonly("kind", &labios::Operation::kind)
        .def_property_readonly("label_ids", [](const labios::Operation& value) {
            const auto ids = value.label_ids();
            return std::vector<uint64_t>(ids.begin(), ids.end());
        })
        .def("label_id", &labios::Operation::label_id, py::arg("index") = 0)
        .def("test", [](const labios::Operation& value, size_t index) {
            py::gil_scoped_release release;
            return value.test(index);
        }, py::arg("index") = 0)
        .def("wait_for", [](const labios::Operation& value, uint64_t timeout_ms) {
            py::gil_scoped_release release;
            return value.wait_for(milliseconds(timeout_ms));
        }, py::arg("timeout_ms"))
        .def("wait_any", [](const labios::Operation& value, uint64_t timeout_ms) {
            py::gil_scoped_release release;
            return value.wait_any(milliseconds(timeout_ms));
        }, py::arg("timeout_ms") = 30000)
        .def("wait_all", [](const labios::Operation& value, uint64_t timeout_ms) {
            py::gil_scoped_release release;
            return value.wait_all(milliseconds(timeout_ms));
        }, py::arg("timeout_ms") = 30000)
        .def("wait", [](const labios::Operation& value) {
            py::gil_scoped_release release;
            return value.wait();
        })
        .def("cancel", [](const labios::Operation& value) {
            py::gil_scoped_release release;
            return value.cancel();
        })
        .def("read", [](const labios::Operation& value, uint64_t timeout_ms) {
            std::vector<std::byte> result;
            {
                py::gil_scoped_release release;
                result = value.read(milliseconds(timeout_ms));
            }
            return python_bytes(result);
        }, py::arg("timeout_ms") = 30000);
    module.attr("PendingIO") = module.attr("Operation");

    py::class_<labios::Client>(module, "Client")
        .def(py::init([](const labios::Config& config) {
            py::gil_scoped_release release;
            return std::make_unique<labios::Client>(config);
        }), py::arg("config"))
        .def("write", [](labios::Client& client, const std::string& path,
                         py::bytes data, uint64_t offset) {
            const std::string value = data;
            py::gil_scoped_release release;
            client.write(path, bytes_span(value), offset);
        }, py::arg("filepath"), py::arg("data"), py::arg("offset") = 0)
        .def("read", [](labios::Client& client, const std::string& path,
                        uint64_t offset, uint64_t size) {
            std::vector<std::byte> result;
            {
                py::gil_scoped_release release;
                result = client.read(path, offset, size);
            }
            return python_bytes(result);
        }, py::arg("filepath"), py::arg("offset"), py::arg("size"))
        .def("async_write", [](labios::Client& client, const std::string& path,
                               py::bytes data, uint64_t offset) {
            const std::string value = data;
            py::gil_scoped_release release;
            return client.async_write(path, bytes_span(value), offset);
        }, py::arg("filepath"), py::arg("data"), py::arg("offset") = 0)
        .def("async_read", [](labios::Client& client, const std::string& path,
                              uint64_t offset, uint64_t size) {
            py::gil_scoped_release release;
            return client.async_read(path, offset, size);
        }, py::arg("filepath"), py::arg("offset"), py::arg("size"))
        .def("operation", [](labios::Client& client, const std::vector<uint64_t>& ids,
                             labios::OperationKind kind) {
            py::gil_scoped_release release;
            return client.operation(ids, kind);
        }, py::arg("label_ids"), py::arg("kind") = labios::OperationKind::Generic)
        .def("create_label", [](labios::Client& client, const labios::LabelParams& params) {
            py::gil_scoped_release release;
            return client.create_label(params);
        }, py::arg("params"))
        .def("publish", [](labios::Client& client, const labios::LabelData& label,
                            py::bytes data) {
            const std::string value = data;
            py::gil_scoped_release release;
            return client.publish(label, bytes_span(value));
        }, py::arg("label"), py::arg("data") = py::bytes())
        .def("write_to", [](labios::Client& client, const std::string& uri, py::bytes data) {
            const std::string value = data;
            py::gil_scoped_release release;
            client.write_to(uri, bytes_span(value));
        }, py::arg("dest_uri"), py::arg("data"))
        .def("async_write_to", [](labios::Client& client, const std::string& uri,
                                  py::bytes data) {
            const std::string value = data;
            py::gil_scoped_release release;
            return client.async_write_to(uri, bytes_span(value));
        }, py::arg("dest_uri"), py::arg("data"))
        .def("read_from", [](labios::Client& client, const std::string& uri, uint64_t size) {
            std::vector<std::byte> result;
            {
                py::gil_scoped_release release;
                result = client.read_from(uri, size);
            }
            return python_bytes(result);
        }, py::arg("source_uri"), py::arg("size"))
        .def("async_read_from", [](labios::Client& client, const std::string& uri,
                                   uint64_t size) {
            py::gil_scoped_release release;
            return client.async_read_from(uri, size);
        }, py::arg("source_uri"), py::arg("size"))
        .def("write_with_intent", [](labios::Client& client, const std::string& path,
                                     py::bytes data, labios::Intent intent,
                                     uint8_t priority) {
            const std::string value = data;
            py::gil_scoped_release release;
            return client.write_with_intent(path, bytes_span(value), intent, priority);
        }, py::arg("filepath"), py::arg("data"), py::arg("intent"),
           py::arg("priority") = 0)
        .def("execute_pipeline", [](labios::Client& client, const std::string& source,
                                    const std::string& destination,
                                    const labios::sds::Pipeline& pipeline,
                                    labios::Intent intent) {
            py::gil_scoped_release release;
            return client.execute_pipeline(source, destination, pipeline, intent);
        }, py::arg("source_uri"), py::arg("dest_uri"), py::arg("pipeline"),
           py::arg("intent") = labios::Intent::None)
        .def("publish_to_channel", [](labios::Client& client, const std::string& name,
                                      py::bytes data, uint64_t label_id) {
            const std::string value = data;
            py::gil_scoped_release release;
            return client.publish_to_channel(name, bytes_span(value), label_id);
        }, py::arg("channel"), py::arg("data"), py::arg("label_id") = 0)
        .def("workspace_put", [](labios::Client& client, const std::string& workspace,
                                 const std::string& key, py::bytes data) {
            const std::string value = data;
            py::gil_scoped_release release;
            return client.workspace_put(workspace, key, bytes_span(value));
        }, py::arg("workspace"), py::arg("key"), py::arg("data"))
        .def("workspace_get", [](labios::Client& client, const std::string& workspace,
                                 const std::string& key) -> py::object {
            std::optional<std::vector<std::byte>> result;
            {
                py::gil_scoped_release release;
                result = client.workspace_get(workspace, key);
            }
            if (!result) return py::none();
            return python_bytes(*result);
        }, py::arg("workspace"), py::arg("key"))
        .def("workspace_del", [](labios::Client& client, const std::string& workspace,
                                 const std::string& key) {
            py::gil_scoped_release release;
            return client.workspace_del(workspace, key);
        }, py::arg("workspace"), py::arg("key"))
        .def("workspace_grant", [](labios::Client& client,
                                   const std::string& workspace, uint32_t app_id) {
            py::gil_scoped_release release;
            client.workspace_grant(workspace, app_id);
        }, py::arg("workspace"), py::arg("app_id"))
        .def("observe", [](labios::Client& client, const std::string& query) {
            py::gil_scoped_release release;
            return client.observe(query);
        }, py::arg("query"))
        .def("inspect_label", [](labios::Client& client, uint64_t label_id) {
            py::gil_scoped_release release;
            return client.inspect_label(label_id);
        }, py::arg("label_id"))
        .def("get_config", [](labios::Client& client) {
            py::gil_scoped_release release;
            return client.get_config();
        });

    module.def("connect", [](const std::string& path) {
        const auto config = labios::load_config(path);
        py::gil_scoped_release release;
        return std::make_unique<labios::Client>(config);
    }, py::arg("config_path") = "conf/labios.toml");
    module.def("connect_to", [](const std::string& nats_url,
                                const std::string& redis_host, int redis_port) {
        labios::Config config;
        config.nats_url = nats_url;
        config.redis_host = redis_host;
        config.redis_port = redis_port;
        py::gil_scoped_release release;
        return std::make_unique<labios::Client>(config);
    }, py::arg("nats_url") = "nats://localhost:4222",
       py::arg("redis_host") = "localhost", py::arg("redis_port") = 6379);
}
