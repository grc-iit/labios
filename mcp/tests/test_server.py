"""Hermetic contract tests for MCP lowering through the public Python SDK."""
from __future__ import annotations

import base64
from enum import Enum
import inspect
from pathlib import Path
from types import SimpleNamespace

import flatbuffers
import pytest

import labios
from labios import MalformedRegistryBuffer, UnsupportedRegistryVersion
from labios.registry import Payload, PayloadKind
from labios.registry import WorkerDescriptor as FbWorker
from labios.registry import WorkerRegistryMessage as FbMessage
from labios.registry import WorkerRegistrySnapshot as FbSnapshot
from labios_mcp.server import (
    McpFrontend,
    decode_registry_snapshot,
    list_tools,
)


class State(Enum):
    PENDING = 0
    COMPLETE = 1
    FAILED = 2
    CANCELLED = 3
    PARKED = 4
    TIMEOUT = 5
    UNKNOWN = 6


class Cancellation(Enum):
    CANCELLED = 0
    TOO_LATE = 1
    TERMINAL = 2
    UNKNOWN = 3


def completion(state=State.COMPLETE, *, label_id=41, category="", error="", lifecycle=None):
    return SimpleNamespace(
        label_id=label_id,
        state=state,
        lifecycle=lifecycle or state,
        category=category,
        error=error,
        park_reason="NO_WORKERS" if state == State.PARKED else "",
        park_attempts=2 if state == State.PARKED else 0,
        next_retry_at_ms=123 if state == State.PARKED else 0,
        worker_id=7,
        attempt=1,
    )


class FakeOperation:
    def __init__(self, waited=None, data=b"", cancellation=Cancellation.CANCELLED):
        self.waited = waited or SimpleNamespace(
            state=State.COMPLETE, results=[completion()])
        self.data = data
        self.cancellation = cancellation
        self.waits = []
        self.cancel_calls = 0

    def label_id(self):
        return self.waited.results[0].label_id

    def wait_all(self, timeout_ms):
        self.waits.append(timeout_ms)
        return self.waited

    def test(self):
        return self.waited.results[0]

    def cancel(self):
        self.cancel_calls += 1
        return [SimpleNamespace(
            state=self.cancellation,
            completion=self.waited.results[0],
        )]

    def read(self, timeout_ms):
        assert timeout_ms == 0
        return self.data


class FakeClient:
    def __init__(self, operations=()):
        self.operations = list(operations)
        self.labels = []
        self.published = []
        self.observe_calls = []
        self.inspected = None

    def create_label(self, params):
        label = SimpleNamespace(
            id=41 + len(self.labels),
            ir_version=1,
            operation="core.read" if params.type == labios.LabelType.Read else "core.write",
            operation_version=1,
            type=params.type,
            source_resource=params.source_resource,
            destination_resource=params.destination_resource,
            intent=params.intent,
            priority=params.priority,
            ttl_seconds=params.ttl_seconds,
            pipeline=params.pipeline,
            data_size=0,
            placement_history=SimpleNamespace(decisions=[]),
        )
        self.labels.append(label)
        return label

    def publish(self, label, data=b""):
        self.published.append((label, bytes(data)))
        return self.operations.pop(0) if self.operations else FakeOperation()

    def observe(self, query):
        self.observe_calls.append(query)
        return '{"scheduler_policy":"round-robin","scheduler_profile":"agentic"}'

    def inspect_label(self, label_id):
        self.inspected = label_id
        label = self.labels[-1]
        label.placement_history = SimpleNamespace(decisions=[SimpleNamespace(
            decision_id=1, attempt=1, outcome="Assigned", chosen_worker_id=7,
            park_reason="", policy_name="round-robin")])
        return label

    def operation(self, ids):
        assert ids == [99]
        return FakeOperation(SimpleNamespace(
            state=State.PENDING,
            results=[completion(State.PENDING, label_id=99)],
        ))


@pytest.mark.anyio
async def test_tool_schemas_and_imports_are_public_label_io():
    tools = {tool.name: tool for tool in await list_tools()}
    assert set(tools) == {
        "labios_observe", "labios_store", "labios_retrieve",
        "labios_process", "labios_knowledge",
    }
    assert tools["labios_store"].inputSchema["required"] == ["destination", "data"]
    assert "scope" not in tools["labios_store"].inputSchema["properties"]
    assert tools["labios_process"].inputSchema["required"] == [
        "source", "destination", "pipeline"]
    assert labios.Operation is not None


def test_store_lowers_typed_destination_and_preserves_intent_priority_ttl():
    client = FakeClient()
    frontend = McpFrontend(client)
    result = frontend.call("labios_store", {
        "destination": "file:///mcp/unit.bin",
        "data": base64.b64encode(b"\x00exact\xff").decode(),
        "encoding": "base64",
        "intent": "tool_output",
        "priority": 213,
        "ttl_seconds": 17,
        "timeout_ms": 4321,
    })

    label, staged = client.published[0]
    assert result["status"] == "completed"
    assert staged == b"\x00exact\xff"
    assert label.type == labios.LabelType.Write
    assert label.destination_resource.family == labios.ResourceFamily.FILE_RANGE
    assert label.destination_resource.path == "/mcp/unit.bin"
    assert label.intent == labios.Intent.TOOL_OUTPUT
    assert label.priority == 213
    assert label.ttl_seconds == 17
    assert client.operations == []


def test_retrieve_lowers_typed_source_and_returns_exact_public_bytes():
    operation = FakeOperation(data=b"\x00read\xff")
    client = FakeClient([operation])
    result = McpFrontend(client).call("labios_retrieve", {
        "source": "sqlite:///mcp/item",
        "size": 7,
        "encoding": "base64",
        "intent": "cache",
        "priority": 9,
    })

    label, staged = client.published[0]
    assert staged == b""
    assert label.type == labios.LabelType.Read
    assert label.source_resource.family == labios.ResourceFamily.RELATIONAL
    assert label.source_resource.path == "/mcp/item"
    assert label.data_size == 7
    assert label.intent == labios.Intent.CACHE
    assert label.priority == 9
    assert base64.b64decode(result["data"]) == b"\x00read\xff"


def test_process_preserves_source_destination_structured_pipeline_and_intent():
    client = FakeClient()
    result = McpFrontend(client).call("labios_process", {
        "source": "file:///mcp/source",
        "destination": "sqlite:///mcp/destination",
        "pipeline": [
            {"operation": "builtin://identity", "args": ""},
            {"operation": "builtin://truncate", "args": "4", "input_stage": 0},
        ],
        "intent": "intermediate",
        "priority": 177,
    })

    label, staged = client.published[0]
    assert result["status"] == "completed"
    assert staged == b""
    assert label.source_resource.path == "/mcp/source"
    assert label.destination_resource.path == "/mcp/destination"
    assert label.intent == labios.Intent.INTERMEDIATE
    assert label.priority == 177
    assert [(s.operation, s.args, s.input_stage) for s in label.pipeline.stages] == [
        ("builtin://identity", "", -1),
        ("builtin://truncate", "4", 0),
    ]


def test_stable_admission_and_execution_error_mapping():
    class RejectingClient(FakeClient):
        def create_label(self, params):
            raise labios.ResourceError("UNKNOWN_RESOURCE: not registered")

    admission = McpFrontend(RejectingClient()).call("labios_store", {
        "destination": "file:///ignored", "data": "x"})
    assert admission["status"] == "admission_failure"
    assert admission["error"]["category"] == "UNKNOWN_RESOURCE"

    failed = FakeOperation(SimpleNamespace(
        state=State.FAILED,
        results=[completion(State.FAILED, category="EXECUTION_FAILED",
                            error="EXECUTION_FAILED: backend write failed")],
    ))
    execution = McpFrontend(FakeClient([failed])).call("labios_store", {
        "destination": "file:///failure", "data": "x"})
    assert execution["status"] == "execution_failure"
    assert execution["error"]["category"] == "EXECUTION_FAILED"


def test_timeout_and_cancellation_are_distinct_and_operation_owned():
    timeout_wait = SimpleNamespace(
        state=State.TIMEOUT,
        results=[completion(State.PENDING, lifecycle=State.PENDING)],
    )
    active = FakeOperation(timeout_wait)
    timed_out = McpFrontend(FakeClient([active])).call("labios_store", {
        "destination": "file:///timeout", "data": "x", "timeout_ms": 0,
    })
    assert timed_out["status"] == "timeout"
    assert timed_out["error"]["retryable"] is True
    assert active.cancel_calls == 0

    cancelled = FakeOperation(timeout_wait, cancellation=Cancellation.CANCELLED)
    projected = McpFrontend(FakeClient([cancelled])).call("labios_store", {
        "destination": "file:///cancel", "data": "x", "timeout_ms": 0,
        "cancel_on_timeout": True,
    })
    assert projected["status"] == "cancelled"
    assert projected["error"]["category"] == "CANCELED"
    assert cancelled.cancel_calls == 1

    too_late = FakeOperation(timeout_wait, cancellation=Cancellation.TOO_LATE)
    raced = McpFrontend(FakeClient([too_late])).call("labios_store", {
        "destination": "file:///race", "data": "x", "timeout_ms": 0,
        "cancel_on_timeout": True,
    })
    assert raced["status"] == "cancellation"
    assert raced["error"]["category"] == "CANCELLATION_TOO_LATE"


def test_parked_and_unknown_completion_projection():
    parked_wait = SimpleNamespace(
        state=State.TIMEOUT,
        results=[completion(State.PARKED, lifecycle=State.PARKED)],
    )
    parked = McpFrontend(FakeClient([FakeOperation(parked_wait)])).call(
        "labios_store", {"destination": "file:///parked", "data": "x"})
    assert parked["status"] == "parked"
    assert parked["completion"]["park_reason"] == "NO_WORKERS"

    unknown_wait = SimpleNamespace(
        state=State.UNKNOWN, results=[completion(State.UNKNOWN)])
    unknown = McpFrontend(FakeClient([FakeOperation(unknown_wait)])).call(
        "labios_store", {"destination": "file:///unknown", "data": "x"})
    assert unknown["status"] == "completion_unknown"


@pytest.mark.parametrize("arguments", [
    {},
    {"destination": "file:///x", "data": "%%%", "encoding": "base64"},
    {"destination": "file:///x", "data": "x", "priority": 256},
    {"destination": "file:///x", "data": "x", "timeout_ms": "soon"},
    {"destination": "file:///x", "data": "x", "scope": "project/legacy"},
])
def test_malformed_store_requests_are_rejected_before_client_use(arguments):
    client = FakeClient()
    result = McpFrontend(client).call("labios_store", arguments)
    assert result["status"] == "malformed_request"
    assert result["error"]["category"] == "MALFORMED_REQUEST"
    assert not client.published


def test_unsupported_arbitrary_transformations_are_explicitly_rejected():
    client = FakeClient()
    for operation in ("grep:TODO", "shell://rm", "repo://arbitrary"):
        result = McpFrontend(client).call("labios_process", {
            "source": "file:///source",
            "destination": "file:///destination",
            "pipeline": [{"operation": operation}],
        })
        assert result["status"] == "malformed_request"
        assert "not a registered Label I/O transform" in result["error"]["message"]
    assert not client.published


def test_observe_uses_only_public_observe_and_inspection_apis():
    client = FakeClient()
    frontend = McpFrontend(client)
    active = frontend.call("labios_observe", {"query": "config/current"})
    assert active["observation"]["scheduler_policy"] == "round-robin"
    assert client.observe_calls == ["config/current"]

    frontend.call("labios_store", {"destination": "file:///inspect", "data": "x"})
    inspected = frontend.call("labios_observe", {
        "query": "label/inspect", "label_id": 41})
    assert inspected["label"]["ir_version"] == 1
    assert inspected["label"]["destination"]["family"] == "file_range"
    assert inspected["label"]["placement_history"][0]["chosen_worker_id"] == 7
    assert client.inspected == 41


def test_normal_core_paths_have_no_direct_store_transport_or_volume_adapter():
    import labios_mcp.server as server

    source = inspect.getsource(server)
    forbidden = (
        "import redis", "redis.asyncio", "import nats", "nats.connect",
        "labios.manager.workers", "/labios/data", "pathlib.Path",
    )
    assert all(token not in source for token in forbidden)

    client = FakeClient([FakeOperation(data=b"x") for _ in range(3)])
    frontend = McpFrontend(client)
    assert frontend.call("labios_store", {
        "destination": "file:///proof", "data": "x"})["ok"]
    assert frontend.call("labios_retrieve", {
        "source": "file:///proof", "size": 1})["ok"]
    assert frontend.call("labios_process", {
        "source": "file:///proof", "destination": "sqlite:///proof",
        "pipeline": [{"operation": "builtin://identity"}],
    })["ok"]
    assert len(client.published) == 3


def test_knowledge_is_explicitly_pending_prompt_14():
    result = McpFrontend(FakeClient()).call("labios_knowledge", {})
    assert result["status"] == "unsupported_feature"
    assert result["error"]["category"] == "WORKSPACE_KNOWLEDGE_UNAVAILABLE"


def _worker(builder, worker_id=7):
    FbWorker.WorkerDescriptorStart(builder)
    FbWorker.WorkerDescriptorAddId(builder, worker_id)
    FbWorker.WorkerDescriptorAddRegistrationEpoch(builder, 9)
    FbWorker.WorkerDescriptorAddAvailable(builder, True)
    FbWorker.WorkerDescriptorAddTotalCapacityBytes(builder, 4096)
    FbWorker.WorkerDescriptorAddAvailableCapacityBytes(builder, 2048)
    FbWorker.WorkerDescriptorAddCapacity(builder, 0.5)
    FbWorker.WorkerDescriptorAddLoad(builder, 0.25)
    FbWorker.WorkerDescriptorAddSpeed(builder, 4)
    FbWorker.WorkerDescriptorAddEnergy(builder, 2)
    FbWorker.WorkerDescriptorAddTier(builder, 1)
    FbWorker.WorkerDescriptorAddMaxIrVersion(builder, 1)
    return FbWorker.WorkerDescriptorEnd(builder)


def _snapshot_buffer(*, version=2):
    builder = flatbuffers.Builder(512)
    row = _worker(builder)
    FbSnapshot.WorkerRegistrySnapshotStartWorkersVector(builder, 1)
    builder.PrependUOffsetTRelative(row)
    workers = builder.EndVector()
    FbSnapshot.WorkerRegistrySnapshotStart(builder)
    FbSnapshot.WorkerRegistrySnapshotAddRegistryGeneration(builder, 1)
    FbSnapshot.WorkerRegistrySnapshotAddCapturedUs(builder, 123456)
    FbSnapshot.WorkerRegistrySnapshotAddWorkers(builder, workers)
    snapshot = FbSnapshot.WorkerRegistrySnapshotEnd(builder)
    FbMessage.WorkerRegistryMessageStart(builder)
    FbMessage.WorkerRegistryMessageAddProtocolVersion(builder, version)
    FbMessage.WorkerRegistryMessageAddKind(builder, PayloadKind.PayloadKind.Snapshot)
    FbMessage.WorkerRegistryMessageAddPayloadType(
        builder, Payload.Payload.WorkerRegistrySnapshot)
    FbMessage.WorkerRegistryMessageAddPayload(builder, snapshot)
    message = FbMessage.WorkerRegistryMessageEnd(builder)
    builder.Finish(message, file_identifier=b"LWR2")
    return bytes(builder.Output())


def test_mcp_registry_snapshot_uses_shared_verified_lwr2_parser_only():
    valid = _snapshot_buffer()
    assert [row.id for row in decode_registry_snapshot(valid).workers] == [7]
    with pytest.raises(MalformedRegistryBuffer):
        decode_registry_snapshot(valid[:9])
    with pytest.raises(MalformedRegistryBuffer):
        decode_registry_snapshot(b"7,1,0.5,0.2,4,2\n")
    with pytest.raises(UnsupportedRegistryVersion):
        decode_registry_snapshot(_snapshot_buffer(version=3))
