"""Hermetic public-surface tests for the LABIOS Python SDK."""
import pytest

import labios


def test_imports_and_owning_operation_alias():
    assert labios.Operation is labios.PendingIO
    assert labios.Client is not None
    assert hasattr(labios.Client, "publish_to_channel")
    assert hasattr(labios.Client, "workspace_put")
    assert labios.parse_worker_registry_message is not None


def test_enums_and_completion_states():
    assert labios.Intent.CHECKPOINT is not None
    assert labios.LabelType.Write is not None
    assert labios.ResourceFamily.FILE_RANGE is not None
    assert labios.CompletionState.TIMEOUT is not None
    assert labios.LifecycleState.PARKED is not None
    assert labios.CancellationState.TOO_LATE is not None


def test_typed_resources_labels_intent_priority_and_pipeline():
    source = labios.resource_from_uri("file:///python/source.bin")
    destination = labios.resource_from_uri("sqlite:///python/result")
    assert source.family == labios.ResourceFamily.FILE_RANGE
    assert source.path == "/python/source.bin"
    assert destination.family == labios.ResourceFamily.RELATIONAL

    params = labios.LabelParams()
    params.type = labios.LabelType.Write
    params.source_resource = source
    params.destination_resource = destination
    params.intent = labios.Intent.INTERMEDIATE
    params.priority = 211
    params.pipeline = labios.Pipeline()
    params.pipeline.stages = [labios.PipelineStage("builtin://identity")]

    assert params.source_resource.path == "/python/source.bin"
    assert params.destination_resource.family == labios.ResourceFamily.RELATIONAL
    assert params.intent == labios.Intent.INTERMEDIATE
    assert params.priority == 211
    assert params.pipeline.stages[0].operation == "builtin://identity"


def test_exception_categories_are_stable_and_specific():
    assert issubclass(labios.TimeoutError, labios.CompletionError)
    assert issubclass(labios.CancelledError, labios.CompletionError)
    assert issubclass(labios.MalformedBufferError, labios.ProtocolError)
    assert issubclass(labios.UnsupportedVersionError, labios.ProtocolError)
    assert issubclass(labios.ResourceError, labios.SubmissionError)
    assert issubclass(labios.PipelineError, labios.SubmissionError)
    assert issubclass(labios.DependencyError, labios.SubmissionError)
    assert issubclass(labios.ExecutionError, labios.CompletionError)
    with pytest.raises(labios.ResourceError, match="UNKNOWN_RESOURCE"):
        labios.resource_from_uri("unknown://python-sdk/resource")


def test_invalid_operation_uses_documented_exception():
    operation = labios.Operation()
    assert not operation.valid
    with pytest.raises(labios.InvalidArgumentError):
        operation.test()
