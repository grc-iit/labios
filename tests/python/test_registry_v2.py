"""Hermetic registry-v2 verification and skew rejection."""
import flatbuffers
import pytest

from labios import (
    MalformedRegistryBuffer,
    UnexpectedRegistryPayload,
    UnsupportedRegistryVersion,
    WorkerRegistrySnapshot,
    parse_worker_registry_message,
)
from labios.registry import Payload, PayloadKind
from labios.registry import WorkerDescriptor as FbWorker
from labios.registry import WorkerRegistryMessage as FbMessage
from labios.registry import WorkerRegistrySnapshot as FbSnapshot


def worker(builder, worker_id=7):
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


def snapshot_buffer(*, version=2, worker_ids=(), kind=None, payload_type=None):
    builder = flatbuffers.Builder(512)
    rows = [worker(builder, worker_id) for worker_id in worker_ids]
    workers_vector = None
    if rows:
        FbSnapshot.WorkerRegistrySnapshotStartWorkersVector(builder, len(rows))
        for row in reversed(rows):
            builder.PrependUOffsetTRelative(row)
        workers_vector = builder.EndVector()
    FbSnapshot.WorkerRegistrySnapshotStart(builder)
    FbSnapshot.WorkerRegistrySnapshotAddRegistryGeneration(builder, len(rows))
    FbSnapshot.WorkerRegistrySnapshotAddCapturedUs(builder, 123456)
    if workers_vector is not None:
        FbSnapshot.WorkerRegistrySnapshotAddWorkers(builder, workers_vector)
    snapshot = FbSnapshot.WorkerRegistrySnapshotEnd(builder)

    actual_kind = PayloadKind.PayloadKind.Snapshot if kind is None else kind
    actual_payload = (Payload.Payload.WorkerRegistrySnapshot
                      if payload_type is None else payload_type)
    FbMessage.WorkerRegistryMessageStart(builder)
    FbMessage.WorkerRegistryMessageAddProtocolVersion(builder, version)
    FbMessage.WorkerRegistryMessageAddKind(builder, actual_kind)
    FbMessage.WorkerRegistryMessageAddPayloadType(builder, actual_payload)
    FbMessage.WorkerRegistryMessageAddPayload(builder, snapshot)
    message = FbMessage.WorkerRegistryMessageEnd(builder)
    builder.Finish(message, file_identifier=b"LWR2")
    return bytes(builder.Output())


def test_lwr2_empty_and_nonempty_snapshots():
    empty = parse_worker_registry_message(snapshot_buffer())
    assert isinstance(empty.payload, WorkerRegistrySnapshot)
    assert empty.payload.registry_generation == 0
    assert empty.payload.workers == ()

    populated = parse_worker_registry_message(snapshot_buffer(worker_ids=(7, 8)))
    assert [worker.id for worker in populated.payload.workers] == [7, 8]
    assert populated.payload.workers[0].registration_epoch == 9
    assert populated.payload.workers[0].available_capacity_bytes == 2048


def test_identifier_truncation_and_text_are_rejected_without_fallback():
    valid = snapshot_buffer(worker_ids=(7,))
    with pytest.raises(MalformedRegistryBuffer):
        parse_worker_registry_message(valid[:9])
    with pytest.raises(MalformedRegistryBuffer):
        parse_worker_registry_message(b"7,1,0.5,0.2,4,2\n")
    corrupt = bytearray(valid)
    corrupt[4:8] = b"NOPE"
    with pytest.raises(MalformedRegistryBuffer):
        parse_worker_registry_message(corrupt)


def test_protocol_version_skew_is_rejected():
    with pytest.raises(UnsupportedRegistryVersion):
        parse_worker_registry_message(snapshot_buffer(version=3))


def test_payload_kind_and_union_must_agree():
    with pytest.raises(UnexpectedRegistryPayload):
        parse_worker_registry_message(
            snapshot_buffer(kind=PayloadKind.PayloadKind.Registration))
    with pytest.raises(UnexpectedRegistryPayload):
        parse_worker_registry_message(
            snapshot_buffer(payload_type=Payload.Payload.WorkerRegistration))
    with pytest.raises(UnexpectedRegistryPayload):
        parse_worker_registry_message(
            snapshot_buffer(), expected_kind=PayloadKind.PayloadKind.Registration)


def test_duplicate_worker_ids_are_rejected():
    with pytest.raises(MalformedRegistryBuffer):
        parse_worker_registry_message(snapshot_buffer(worker_ids=(7, 7)))
