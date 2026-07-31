"""Live MCP tool tests against the Compose reference topology."""
from __future__ import annotations

import base64
import os
import uuid

import pytest

import labios
from labios_mcp.server import McpFrontend

pytestmark = pytest.mark.live
if os.environ.get("LABIOS_MCP_LIVE") != "1":
    pytest.skip("set LABIOS_MCP_LIVE=1 for Compose tests", allow_module_level=True)


def frontend() -> McpFrontend:
    return McpFrontend(labios.connect_to(
        os.environ.get("LABIOS_NATS_URL", "nats://nats:4222"),
        os.environ.get("LABIOS_REDIS_HOST", "redis"),
        int(os.environ.get("LABIOS_REDIS_PORT", "6379")),
    ))


def test_mcp_store_then_exact_public_retrieve_bytes():
    mcp = frontend()
    uri = f"file:///mcp-live/{uuid.uuid4().hex}.bin"
    expected = b"\x00MCP exact retrieve\xff\n"
    stored = mcp.call("labios_store", {
        "destination": uri,
        "data": base64.b64encode(expected).decode(),
        "encoding": "base64",
        "intent": "tool_output",
        "priority": 201,
    })
    assert stored["status"] == "completed", stored
    read = mcp.call("labios_retrieve", {
        "source": uri, "size": len(expected), "encoding": "base64"})
    assert read["status"] == "completed", read
    assert base64.b64decode(read["data"]) == expected


def test_mcp_registered_pipeline_exact_destination_and_public_inspection():
    mcp = frontend()
    token = uuid.uuid4().hex
    source = f"file:///mcp-live/{token}.source"
    destination = f"sqlite:///mcp-live/{token}.destination"
    expected = b"MCP pipeline exact\x00\xffbytes"
    assert mcp.call("labios_store", {
        "destination": source,
        "data": base64.b64encode(expected).decode(),
        "encoding": "base64",
    })["ok"]
    processed = mcp.call("labios_process", {
        "source": source,
        "destination": destination,
        "pipeline": [{"operation": "builtin://identity"}],
        "intent": "intermediate",
        "priority": 173,
    })
    assert processed["status"] == "completed", processed
    read = mcp.call("labios_retrieve", {
        "source": destination, "size": len(expected), "encoding": "base64"})
    assert base64.b64decode(read["data"]) == expected

    lifecycle = mcp.call("labios_observe", {
        "query": "label/status", "label_id": processed["label_id"]})
    assert lifecycle["completion"]["lifecycle"] == "completed"
    inspected = mcp.call("labios_observe", {
        "query": "label/inspect", "label_id": processed["label_id"]})
    label = inspected["label"]
    assert label["ir_version"] == 1
    assert label["operation_version"] == 1
    assert label["source"]["family"] == "file_range"
    assert label["destination"]["family"] == "relational"
    assert label["pipeline"] == [{
        "operation": "builtin://identity", "args": "",
        "input_stage": -1, "output_stage": -1,
    }]
    assert label["intent"] == "intermediate"
    assert label["priority"] == 173
    assert label["placement_history"]


def test_mcp_timeout_and_cancellation_projection():
    mcp = frontend()
    timed = mcp.call("labios_store", {
        "destination": f"file:///mcp-live/{uuid.uuid4().hex}.timeout",
        "data": base64.b64encode(b"t" * (4 * 1024 * 1024)).decode(),
        "encoding": "base64",
        "timeout_ms": 0,
    })
    assert timed["status"] in {"timeout", "parked", "completed"}, timed

    cancelled = mcp.call("labios_store", {
        "destination": f"file:///mcp-live/{uuid.uuid4().hex}.cancel",
        "data": base64.b64encode(b"c" * (4 * 1024 * 1024)).decode(),
        "encoding": "base64",
        "timeout_ms": 0,
        "cancel_on_timeout": True,
    })
    assert cancelled["status"] in {
        "cancelled", "cancellation", "completed", "parked"}, cancelled
    if cancelled["status"] == "cancelled":
        assert cancelled["error"]["category"] == "CANCELED"
    if cancelled["status"] == "cancellation":
        assert cancelled["error"]["category"] == "CANCELLATION_TOO_LATE"


def test_mcp_health_and_worker_observations_via_public_observe():
    mcp = frontend()
    health = mcp.call("labios_observe", {"query": "system/health"})
    counts = mcp.call("labios_observe", {"query": "workers/count"})
    scores = mcp.call("labios_observe", {"query": "workers/scores"})
    config = mcp.call("labios_observe", {"query": "config/current"})
    assert health["observation"]["nats"] == "connected"
    assert health["observation"]["redis"] == "connected"
    assert counts["observation"]["total"] >= 1
    assert scores["observation"]["workers"]
    assert "scheduler_policy" in config["observation"]
    assert "scheduler_profile" in config["observation"]
