"""Live Python SDK tests against the reference Compose topology."""
from __future__ import annotations

import os
import threading
import uuid

import pytest

import labios

pytestmark = pytest.mark.live

if os.environ.get("LABIOS_PYTHON_LIVE") != "1":
    pytest.skip("set LABIOS_PYTHON_LIVE=1 for Compose tests", allow_module_level=True)


def client():
    return labios.connect_to(
        os.environ.get("LABIOS_NATS_URL", "nats://localhost:4222"),
        os.environ.get("LABIOS_REDIS_HOST", "localhost"),
        int(os.environ.get("LABIOS_REDIS_PORT", "6379")),
    )


def test_completion_timeout_reuse_and_repeatable_read():
    sdk = client()
    uri = f"file:///python-live/{uuid.uuid4().hex}.bin"
    data = b"repeatable Python read"
    sdk.write_to(uri, data)
    operation = sdk.async_read_from(uri, len(data))
    first = operation.wait_for(0)
    assert first.state in (labios.CompletionState.TIMEOUT,
                           labios.CompletionState.COMPLETE)
    assert operation.read(30_000) == data
    assert operation.read(30_000) == data


def test_label_level_submission_and_public_placement_observation():
    sdk = client()
    uri = f"file:///python-live/{uuid.uuid4().hex}.label"
    data = b"typed label submission"
    params = labios.LabelParams()
    params.type = labios.LabelType.Write
    params.destination_resource = labios.resource_from_uri(uri)
    params.intent = labios.Intent.TOOL_OUTPUT
    params.priority = 199
    operation = sdk.publish(sdk.create_label(params), data)
    result = operation.wait_all(30_000)
    assert result.state == labios.CompletionState.COMPLETE
    completion = operation.test()
    assert completion.lifecycle == labios.LifecycleState.COMPLETED
    label = sdk.inspect_label(operation.label_id())
    assert label.intent == labios.Intent.TOOL_OUTPUT
    assert label.priority == 199
    assert label.placement_history.decisions
    assert sdk.read_from(uri, len(data)) == data


def test_exact_source_pipeline_destination_bytes():
    sdk = client()
    token = uuid.uuid4().hex
    source = f"file:///python-live/{token}.source"
    destination = f"sqlite:///python-live/{token}.result"
    data = b"\x00Python pipeline\xff exact bytes\n"
    sdk.write_to(source, data)
    pipeline = labios.Pipeline()
    pipeline.stages = [labios.PipelineStage("builtin://identity")]
    operation = sdk.execute_pipeline(source, destination, pipeline)
    assert operation.wait_all(30_000).state == labios.CompletionState.COMPLETE
    assert sdk.read_from(destination, len(data)) == data


def test_operation_survives_client_and_wait_cancel_release_the_gil():
    sdk = client()
    operation = sdk.async_write_to(
        f"file:///python-live/{uuid.uuid4().hex}.race", b"x" * (4 * 1024 * 1024))
    del sdk
    waited = []
    waiter = threading.Thread(target=lambda: waited.append(operation.wait_all(30_000)))
    waiter.start()
    cancellation = operation.cancel()
    waiter.join(35)
    assert not waiter.is_alive()
    assert cancellation[0].state in (
        labios.CancellationState.CANCELLED,
        labios.CancellationState.TOO_LATE,
        labios.CancellationState.TERMINAL,
    )
    assert waited and waited[0].state in (
        labios.CompletionState.COMPLETE,
        labios.CompletionState.CANCELLED,
        labios.CompletionState.FAILED,
    )
