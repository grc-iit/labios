"""Verified decoder for the LABIOS worker-registry protocol v2.

This is the single Python decoder for ``worker_registry.fbs``.  It accepts only
FlatBuffers envelopes identified by ``LWR2``; there is deliberately no text or
CSV compatibility path.
"""
from __future__ import annotations

from dataclasses import dataclass
import math
import struct
from typing import Optional, Union

from .registry import Payload, PayloadKind
from .registry.Attachment import Attachment as FbAttachment
from .registry.WorkerDeregistration import WorkerDeregistration as FbDeregistration
from .registry.WorkerDescriptor import WorkerDescriptor as FbWorker
from .registry.WorkerRegistration import WorkerRegistration as FbRegistration
from .registry.WorkerRegistryMessage import WorkerRegistryMessage as FbMessage
from .registry.WorkerRegistrySnapshot import WorkerRegistrySnapshot as FbSnapshot
from .registry.WorkerResourceUpdate import WorkerResourceUpdate as FbResourceUpdate

PROTOCOL_VERSION = 2
FILE_IDENTIFIER = b"LWR2"


class RegistryV2Error(ValueError):
    """Base class for rejected registry-v2 input."""


class MalformedRegistryBuffer(RegistryV2Error):
    """The FlatBuffer envelope or one of its values is malformed."""


class UnsupportedRegistryVersion(RegistryV2Error):
    """The envelope uses a protocol version other than version 2."""


class UnexpectedRegistryPayload(RegistryV2Error):
    """The payload kind, union discriminator, or expected kind is inconsistent."""


@dataclass(frozen=True)
class Attachment:
    family: int
    backend_id: str
    scheme: str
    locality: int
    domain: str


@dataclass(frozen=True)
class WorkerDescriptor:
    id: int
    registration_epoch: int
    registry_generation: int
    available: bool
    total_capacity_bytes: int
    available_capacity_bytes: int
    capacity: float
    load: float
    speed: int
    energy: int
    tier: int
    max_ir_version: int
    operations: tuple[str, ...]
    operation_versions: tuple[int, ...]
    pipeline_operations: tuple[str, ...]
    pipeline_operation_versions: tuple[int, ...]
    attachments: tuple[Attachment, ...]
    locality_domains: tuple[str, ...]
    skills: float
    compute: float
    reasoning: int


@dataclass(frozen=True)
class WorkerRegistration:
    worker: WorkerDescriptor


@dataclass(frozen=True)
class WorkerResourceUpdate:
    worker_id: int
    registration_epoch: int
    available: bool
    total_capacity_bytes: int
    available_capacity_bytes: int
    capacity: float
    load: float
    skills: float
    compute: float
    reasoning: int


@dataclass(frozen=True)
class WorkerDeregistration:
    worker_id: int
    registration_epoch: int


@dataclass(frozen=True)
class WorkerRegistrySnapshot:
    registry_generation: int
    captured_us: int
    workers: tuple[WorkerDescriptor, ...]


RegistryPayload = Union[
    WorkerRegistration, WorkerResourceUpdate, WorkerDeregistration,
    WorkerRegistrySnapshot,
]


@dataclass(frozen=True)
class WorkerRegistryMessage:
    protocol_version: int
    kind: int
    payload: RegistryPayload


def _text(value: object) -> str:
    if not isinstance(value, (bytes, bytearray)):
        raise MalformedRegistryBuffer("MALFORMED_BUFFER: missing string")
    try:
        return bytes(value).decode("utf-8", "strict")
    except UnicodeDecodeError as exc:
        raise MalformedRegistryBuffer("MALFORMED_BUFFER: invalid UTF-8") from exc


def _strings(row: FbWorker, name: str) -> tuple[str, ...]:
    length = getattr(row, f"{name}Length")()
    return tuple(_text(getattr(row, name)(index)) for index in range(length))


def _numbers(row: FbWorker, name: str) -> tuple[int, ...]:
    length = getattr(row, f"{name}Length")()
    return tuple(int(getattr(row, name)(index)) for index in range(length))


def _finite_unit(value: float) -> bool:
    return math.isfinite(value) and 0.0 <= value <= 1.0


def _worker(row: Optional[FbWorker]) -> WorkerDescriptor:
    if row is None:
        raise MalformedRegistryBuffer("MALFORMED_BUFFER: missing worker descriptor")
    operations = _strings(row, "Operations")
    operation_versions = _numbers(row, "OperationVersions")
    pipeline = _strings(row, "PipelineOperations")
    pipeline_versions = _numbers(row, "PipelineOperationVersions")
    attachments = []
    attachment_keys: set[tuple[int, str, str]] = set()
    for index in range(row.AttachmentsLength()):
        item = row.Attachments(index)
        if item is None:
            raise MalformedRegistryBuffer("MALFORMED_BUFFER: null attachment")
        attachment = Attachment(
            int(item.Family()), _text(item.BackendId()), _text(item.Scheme()),
            int(item.Locality()), _text(item.Domain()),
        )
        key = (attachment.family, attachment.backend_id, attachment.scheme)
        if (not 0 <= attachment.family <= 10 or not attachment.backend_id or
                not attachment.scheme or not 0 <= attachment.locality <= 2 or
                (attachment.locality == 2 and not attachment.domain) or
                key in attachment_keys):
            raise MalformedRegistryBuffer("INCONSISTENT_ATTACHMENT")
        attachment_keys.add(key)
        attachments.append(attachment)
    worker = WorkerDescriptor(
        int(row.Id()), int(row.RegistrationEpoch()), int(row.RegistryGeneration()),
        bool(row.Available()), int(row.TotalCapacityBytes()),
        int(row.AvailableCapacityBytes()), float(row.Capacity()), float(row.Load()),
        int(row.Speed()), int(row.Energy()), int(row.Tier()), int(row.MaxIrVersion()),
        operations, operation_versions, pipeline, pipeline_versions,
        tuple(attachments), _strings(row, "LocalityDomains"), float(row.Skills()),
        float(row.Compute()), int(row.Reasoning()),
    )
    if (worker.id <= 0 or worker.registration_epoch == 0 or
            not _finite_unit(worker.capacity) or not _finite_unit(worker.load) or
            not _finite_unit(worker.skills) or not _finite_unit(worker.compute) or
            not 1 <= worker.speed <= 5 or not 1 <= worker.energy <= 5 or
            not 0 <= worker.tier <= 2 or worker.max_ir_version == 0 or
            (worker.total_capacity_bytes and
             worker.available_capacity_bytes > worker.total_capacity_bytes)):
        raise MalformedRegistryBuffer("INVALID_RANGE")
    if (len(set(operations)) != len(operations) or
            len(set(pipeline)) != len(pipeline) or
            len(operation_versions) != len(operations) or
            len(pipeline_versions) != len(pipeline) or
            any(version == 0 for version in operation_versions + pipeline_versions)):
        raise MalformedRegistryBuffer("INCONSISTENT_CAPABILITY")
    return worker


def _union(message: FbMessage, cls: type) -> object:
    table = message.Payload()
    if table is None:
        raise UnexpectedRegistryPayload("WRONG_KIND: missing union payload")
    value = cls()
    value.Init(table.Bytes, table.Pos)
    return value


def parse_worker_registry_message(
    buffer: bytes | bytearray | memoryview,
    *,
    expected_kind: Optional[int] = None,
) -> WorkerRegistryMessage:
    """Decode and validate one registry-v2 envelope.

    ``expected_kind`` may constrain a NATS subject to its assigned PayloadKind.
    All malformed/truncated input, protocol skew, and kind/union mismatch is
    rejected before a decoded value is returned.
    """
    data = bytes(buffer)
    if len(data) < 12 or data[4:8] != FILE_IDENTIFIER:
        raise MalformedRegistryBuffer("MALFORMED_BUFFER: missing LWR2 identifier")
    try:
        root = struct.unpack_from("<I", data, 0)[0]
        if root < 8 or root >= len(data):
            raise MalformedRegistryBuffer("MALFORMED_BUFFER: invalid root offset")
        message = FbMessage.GetRootAs(data, 0)
        version = int(message.ProtocolVersion())
        if version != PROTOCOL_VERSION:
            raise UnsupportedRegistryVersion(
                f"UNSUPPORTED_VERSION: expected 2, received {version}")
        kind = int(message.Kind())
        if kind not in (PayloadKind.PayloadKind.Registration,
                        PayloadKind.PayloadKind.ResourceUpdate,
                        PayloadKind.PayloadKind.Deregistration,
                        PayloadKind.PayloadKind.Snapshot):
            raise UnexpectedRegistryPayload(f"INVALID_KIND: {kind}")
        if expected_kind is not None and kind != int(expected_kind):
            raise UnexpectedRegistryPayload(
                f"UNEXPECTED_KIND: expected {expected_kind}, received {kind}")
        expected_union = kind + 1
        if int(message.PayloadType()) != expected_union:
            raise UnexpectedRegistryPayload("WRONG_KIND: union discriminator mismatch")

        if kind == PayloadKind.PayloadKind.Registration:
            registration = _union(message, FbRegistration)
            payload = WorkerRegistration(_worker(registration.Worker()))
        elif kind == PayloadKind.PayloadKind.ResourceUpdate:
            update = _union(message, FbResourceUpdate)
            payload = WorkerResourceUpdate(
                int(update.WorkerId()), int(update.RegistrationEpoch()),
                bool(update.Available()), int(update.TotalCapacityBytes()),
                int(update.AvailableCapacityBytes()), float(update.Capacity()),
                float(update.Load()), float(update.Skills()), float(update.Compute()),
                int(update.Reasoning()),
            )
            if (payload.worker_id <= 0 or payload.registration_epoch == 0 or
                    not _finite_unit(payload.capacity) or
                    not _finite_unit(payload.load) or
                    not _finite_unit(payload.skills) or
                    not _finite_unit(payload.compute) or
                    (payload.total_capacity_bytes and
                     payload.available_capacity_bytes > payload.total_capacity_bytes)):
                raise MalformedRegistryBuffer("INVALID_RANGE")
        elif kind == PayloadKind.PayloadKind.Deregistration:
            deregistration = _union(message, FbDeregistration)
            payload = WorkerDeregistration(
                int(deregistration.WorkerId()), int(deregistration.RegistrationEpoch()))
            if payload.worker_id <= 0 or payload.registration_epoch == 0:
                raise MalformedRegistryBuffer("INVALID_RANGE")
        else:
            snapshot = _union(message, FbSnapshot)
            captured_us = int(snapshot.CapturedUs())
            if captured_us == 0:
                raise MalformedRegistryBuffer("INVALID_RANGE")
            workers = tuple(_worker(snapshot.Workers(i))
                            for i in range(snapshot.WorkersLength()))
            ids = [worker.id for worker in workers]
            if len(set(ids)) != len(ids):
                raise MalformedRegistryBuffer("DUPLICATE_WORKER_ID")
            payload = WorkerRegistrySnapshot(
                int(snapshot.RegistryGeneration()), captured_us, workers)
        return WorkerRegistryMessage(version, kind, payload)
    except RegistryV2Error:
        raise
    except (AttributeError, IndexError, OverflowError, struct.error, TypeError,
            ValueError) as exc:
        raise MalformedRegistryBuffer("MALFORMED_BUFFER: truncated or invalid table") from exc
