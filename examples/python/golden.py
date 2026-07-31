#!/usr/bin/env python3
"""Compose golden path for the LABIOS 2.1 Python Label I/O SDK.

The example uses only public SDK calls.  It never addresses a NATS subject,
DragonflyDB key, or worker-mounted host path.
"""
from __future__ import annotations

import json
import os
import threading
import time
import uuid

import labios


def require_complete(result: labios.WaitResult) -> None:
    if result.state != labios.CompletionState.COMPLETE:
        details = [(item.label_id, item.state, item.category, item.error)
                   for item in result.results]
        raise RuntimeError(f"operation did not complete: {details}")


def main() -> int:
    client = labios.connect_to(
        os.environ.get("LABIOS_NATS_URL", "nats://localhost:4222"),
        os.environ.get("LABIOS_REDIS_HOST", "localhost"),
        int(os.environ.get("LABIOS_REDIS_PORT", "6379")),
    )
    run_id = uuid.uuid4().hex
    source = f"file:///python-golden/{run_id}.source"
    destination = f"sqlite:///python-golden/{run_id}.pipeline"
    label_destination = f"file:///python-golden/{run_id}.label"
    lifetime_destination = f"file:///python-golden/{run_id}.lifetime"
    expected = b"LABIOS Python Label I/O\x00pipeline\xffbytes\n"

    # URI convenience submission followed by a real source -> stage -> destination.
    client.write_to(source, expected)
    pipeline = labios.Pipeline()
    pipeline.stages = [labios.PipelineStage("builtin://identity")]
    operation = client.execute_pipeline(
        source, destination, pipeline, labios.Intent.INTERMEDIATE)
    first = operation.wait_for(0)
    assert first.state in (labios.CompletionState.TIMEOUT,
                           labios.CompletionState.COMPLETE)
    require_complete(operation.wait_all(30_000))
    actual = client.read_from(destination, len(expected))
    assert actual == expected, (actual, expected)

    # Typed label-level publication with sealed intent and priority.
    params = labios.LabelParams()
    params.type = labios.LabelType.Write
    params.destination_resource = labios.resource_from_uri(label_destination)
    params.intent = labios.Intent.CHECKPOINT
    params.priority = 203
    label = client.create_label(params)
    label_operation = client.publish(label, expected)
    require_complete(label_operation.wait_all(30_000))
    assert client.read_from(label_destination, len(expected)) == expected

    # Public lifecycle, parking, placement history, and active profile surfaces.
    completion = label_operation.test()
    assert completion.lifecycle == labios.LifecycleState.COMPLETED
    inspected = client.inspect_label(label_operation.label_id())
    assert inspected.placement_history.decisions
    active = json.loads(client.observe("config/current"))
    assert "scheduler_policy" in active and "scheduler_profile" in active

    # Read retrieval is repeatable and timeout did not consume the handle.
    read_operation = client.async_read_from(destination, len(expected))
    assert read_operation.read(30_000) == expected
    assert read_operation.read(30_000) == expected

    # A retained owning Operation survives destruction of its originating Client.
    survivor = client.async_write_to(lifetime_destination, expected)
    del client
    require_complete(survivor.wait_all(30_000))

    # Concurrent wait/cancel is safe.  The observed race winner is explicit.
    race_client = labios.connect_to(
        os.environ.get("LABIOS_NATS_URL", "nats://localhost:4222"),
        os.environ.get("LABIOS_REDIS_HOST", "localhost"),
        int(os.environ.get("LABIOS_REDIS_PORT", "6379")),
    )
    race = race_client.async_write_to(
        f"file:///python-golden/{run_id}.race", b"r" * (4 * 1024 * 1024))
    waited: list[labios.WaitResult] = []
    thread = threading.Thread(target=lambda: waited.append(race.wait_all(30_000)))
    thread.start()
    # This call must run while the wait releases the GIL.
    cancellation = race.cancel()
    thread.join(35)
    assert not thread.is_alive()
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

    print(f"LABIOS Python golden: verified {len(expected)} exact pipeline bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
