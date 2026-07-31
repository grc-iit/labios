#!/usr/bin/env python3
"""Public MCP frontend for LABIOS Label I/O.

Core tools lower requests through the packaged Python ``Client`` and its owning
``Operation``.  This module deliberately contains no Redis, NATS, or worker
volume adapter.
"""
from __future__ import annotations

import asyncio
import base64
import binascii
from dataclasses import dataclass
import json
import os
import threading
from typing import Any, Mapping

import labios
from labios import (
    WorkerRegistrySnapshot,
    parse_worker_registry_message,
)
from labios.registry import PayloadKind
from mcp.server import Server
from mcp.server.stdio import stdio_server
from mcp.types import TextContent, Tool

app = Server("labios")

_INTENTS = {
    "none": labios.Intent.NONE,
    "checkpoint": labios.Intent.CHECKPOINT,
    "cache": labios.Intent.CACHE,
    "tool_output": labios.Intent.TOOL_OUTPUT,
    "final_result": labios.Intent.FINAL_RESULT,
    "intermediate": labios.Intent.INTERMEDIATE,
    "shared_state": labios.Intent.SHARED_STATE,
    "embedding": labios.Intent.EMBEDDING,
    "model_weight": labios.Intent.MODEL_WEIGHT,
    "kv_cache": labios.Intent.KV_CACHE,
    "reasoning_trace": labios.Intent.REASONING_TRACE,
}
_REGISTERED_PIPELINE_OPERATIONS = frozenset({
    "builtin://identity",
    "builtin://compress_rle",
    "builtin://decompress_rle",
    "builtin://filter_bytes",
    "builtin://sum_uint64",
    "builtin://sort_uint64",
    "builtin://sample",
    "builtin://truncate",
    "builtin://deduplicate",
    "builtin://median_uint64",
    "builtin://format_convert",
})
_PUBLIC_OBSERVATIONS = frozenset({
    "system/health",
    "queue/depth",
    "workers/scores",
    "workers/count",
    "channels/list",
    "workspaces/list",
    "config/current",
})
_COMMON_PROPERTIES = {
    "intent": {
        "type": "string", "enum": list(_INTENTS), "default": "none",
        "description": "Sealed Label I/O intent.",
    },
    "priority": {
        "type": "integer", "minimum": 0, "maximum": 255, "default": 0,
        "description": "Sealed scheduling priority; it is not an ordering edge.",
    },
    "ttl_seconds": {
        "type": "integer", "minimum": 0, "default": 0,
        "description": "Latest-start TTL carried by the label.",
    },
    "timeout_ms": {
        "type": "integer", "minimum": 0, "default": 30000,
        "description": "MCP wait deadline. Timeout does not cancel the label.",
    },
    "cancel_on_timeout": {
        "type": "boolean", "default": False,
        "description": "Request public Operation cancellation only after timeout.",
    },
}


class MalformedRequest(ValueError):
    """An MCP request does not conform to the public tool schema."""


def _text(data: Mapping[str, Any]) -> list[TextContent]:
    return [TextContent(type="text", text=json.dumps(dict(data), sort_keys=True))]


def _enum_name(value: object) -> str:
    return str(getattr(value, "name", value)).lower()


def _category(message: str, default: str) -> str:
    prefix = message.split(":", 1)[0].strip()
    if prefix and prefix.replace("_", "").isalnum() and prefix.upper() == prefix:
        return prefix
    return default


def _error(
    kind: str,
    category: str,
    message: str,
    *,
    label_id: int | None = None,
    retryable: bool = False,
    **extra: Any,
) -> dict[str, Any]:
    result: dict[str, Any] = {
        "ok": False,
        "status": kind,
        "error": {
            "kind": kind,
            "category": category,
            "message": message,
            "retryable": retryable,
        },
    }
    if label_id is not None:
        result["label_id"] = label_id
    result.update(extra)
    return result


def _require_object(arguments: object) -> dict[str, Any]:
    if not isinstance(arguments, dict):
        raise MalformedRequest("arguments must be a JSON object")
    return arguments


def _reject_unknown(args: Mapping[str, Any], allowed: set[str]) -> None:
    unknown = sorted(set(args) - allowed)
    if unknown:
        raise MalformedRequest(f"unknown field(s): {', '.join(unknown)}")


def _string(args: Mapping[str, Any], name: str, *, required: bool = False) -> str:
    if name not in args:
        if required:
            raise MalformedRequest(f"missing required field: {name}")
        return ""
    value = args[name]
    if not isinstance(value, str) or (required and not value):
        raise MalformedRequest(f"{name} must be a non-empty string")
    return value


def _integer(
    args: Mapping[str, Any], name: str, default: int, minimum: int, maximum: int
) -> int:
    value = args.get(name, default)
    if isinstance(value, bool) or not isinstance(value, int):
        raise MalformedRequest(f"{name} must be an integer")
    if not minimum <= value <= maximum:
        raise MalformedRequest(f"{name} must be between {minimum} and {maximum}")
    return value


def _boolean(args: Mapping[str, Any], name: str, default: bool = False) -> bool:
    value = args.get(name, default)
    if not isinstance(value, bool):
        raise MalformedRequest(f"{name} must be a boolean")
    return value


def _common(args: Mapping[str, Any]) -> tuple[object, int, int, int, bool]:
    intent_name = args.get("intent", "none")
    if not isinstance(intent_name, str) or intent_name not in _INTENTS:
        raise MalformedRequest("intent is not a supported Label I/O intent")
    return (
        _INTENTS[intent_name],
        _integer(args, "priority", 0, 0, 255),
        _integer(args, "ttl_seconds", 0, 0, 2**32 - 1),
        _integer(args, "timeout_ms", 30000, 0, 86_400_000),
        _boolean(args, "cancel_on_timeout"),
    )


def _decode_data(value: str, encoding: str) -> bytes:
    if encoding == "utf-8":
        return value.encode("utf-8")
    if encoding == "base64":
        try:
            return base64.b64decode(value, validate=True)
        except (binascii.Error, ValueError) as exc:
            raise MalformedRequest("data is not valid base64") from exc
    raise MalformedRequest("encoding must be 'utf-8' or 'base64'")


def _encode_data(value: bytes, encoding: str) -> str:
    if encoding == "base64":
        return base64.b64encode(value).decode("ascii")
    if encoding == "utf-8":
        try:
            return value.decode("utf-8", "strict")
        except UnicodeDecodeError as exc:
            raise MalformedRequest(
                "retrieved bytes are not UTF-8; request base64 encoding"
            ) from exc
    raise MalformedRequest("encoding must be 'utf-8' or 'base64'")


def _completion(item: object) -> dict[str, Any]:
    return {
        "label_id": int(getattr(item, "label_id", 0)),
        "state": _enum_name(getattr(item, "state", "unknown")),
        "lifecycle": _enum_name(getattr(item, "lifecycle", "unknown")),
        "category": str(getattr(item, "category", "")),
        "message": str(getattr(item, "error", "")),
        "park_reason": str(getattr(item, "park_reason", "")),
        "park_attempts": int(getattr(item, "park_attempts", 0)),
        "next_retry_at_ms": int(getattr(item, "next_retry_at_ms", 0)),
        "worker_id": int(getattr(item, "worker_id", -1)),
        "attempt": int(getattr(item, "attempt", 0)),
    }


def _terminal_failure(item: object) -> dict[str, Any]:
    completion = _completion(item)
    label_id = completion["label_id"]
    state = completion["state"]
    category = completion["category"] or _category(completion["message"], "EXECUTION_FAILED")
    if state == "cancelled" or category == "CANCELED":
        return _error(
            "cancelled", "CANCELED", completion["message"] or "operation cancelled",
            label_id=label_id, completion=completion,
        )
    if state == "unknown":
        return _error(
            "completion_unknown", "COMPLETION_UNKNOWN_OR_EXPIRED",
            completion["message"] or "completion is unknown or expired",
            label_id=label_id, completion=completion,
        )
    admission_categories = {
        "MALFORMED_BUFFER", "LIMIT_EXCEEDED", "UNSUPPORTED_IR_VERSION",
        "INVALID_ENUM", "INVALID_IDENTIFIER", "AUTHORIZATION_FAILED",
        "UNKNOWN_REFINEMENT", "UNKNOWN_RESOURCE", "FORBIDDEN_INTERNAL_RESOURCE",
        "ADDRESS_CONFLICT", "AMBIGUOUS_INPUT", "MISSING_FIELD",
        "ILLEGAL_COMBINATION", "INVALID_RESOURCE", "UNSAFE_MEMORY_REFERENCE",
        "INVALID_PIPELINE", "INVALID_DEPENDENCY", "DUPLICATE_ID",
        "UNAUTHORIZED_MUTATION", "INVALID_STATE_TRANSITION",
    }
    kind = "admission_failure" if category in admission_categories else "execution_failure"
    return _error(
        kind, category, completion["message"] or "Label I/O operation failed",
        label_id=label_id, completion=completion,
    )


def decode_registry_snapshot(payload: bytes) -> WorkerRegistrySnapshot:
    """Decode only a verified LWR2 protocol-v2 worker snapshot.

    Normal MCP observations use public Observe labels.  This helper exists for
    callers that already receive a registry snapshot from a supported public
    inspection surface; it never requests a private NATS subject.
    """
    message = parse_worker_registry_message(
        payload, expected_kind=PayloadKind.PayloadKind.Snapshot
    )
    if not isinstance(message.payload, WorkerRegistrySnapshot):
        raise ValueError("WRONG_KIND: registry payload is not a snapshot")
    return message.payload


@dataclass
class McpFrontend:
    """Synchronous, testable lowering layer behind the async MCP callbacks."""

    client: Any

    def call(self, name: str, arguments: object) -> dict[str, Any]:
        try:
            args = _require_object(arguments)
            if name == "labios_store":
                return self.store(args)
            if name == "labios_retrieve":
                return self.retrieve(args)
            if name == "labios_process":
                return self.process(args)
            if name == "labios_observe":
                return self.observe(args)
            if name == "labios_knowledge":
                return _error(
                    "unsupported_feature", "PENDING_PROMPT_14",
                    "workspace-backed knowledge is pending Prompt 14",
                )
            raise MalformedRequest(f"unknown tool: {name}")
        except MalformedRequest as exc:
            return _error("malformed_request", "MALFORMED_REQUEST", str(exc))
        except labios.CompletionLookupError as exc:
            return _error(
                "completion_unknown", "COMPLETION_UNKNOWN_OR_EXPIRED", str(exc)
            )
        except (labios.SubmissionError, labios.ValidationError,
                labios.ResourceError, labios.PipelineError,
                labios.DependencyError, labios.AuthorizationError) as exc:
            message = str(exc)
            return _error(
                "admission_failure", _category(message, "ADMISSION_REJECTED"), message
            )
        except (labios.ExecutionError, labios.BackendError,
                labios.CompletionError) as exc:
            message = str(exc)
            return _error(
                "execution_failure", _category(message, "EXECUTION_FAILED"), message
            )
        except (labios.MalformedBufferError, labios.UnsupportedVersionError,
                labios.ProtocolError) as exc:
            message = str(exc)
            return _error(
                "malformed_request", _category(message, "MALFORMED_BUFFER"), message
            )
        except labios.LabiosError as exc:
            message = str(exc)
            return _error("execution_failure", _category(message, "LABIOS_ERROR"), message)
        except Exception as exc:  # keep the MCP process alive on adapter/runtime faults
            return _error("execution_failure", "FRONTEND_FAILURE", str(exc))

    def _params(
        self,
        args: Mapping[str, Any],
        label_type: object,
        *,
        source: str = "",
        destination: str = "",
        pipeline: object | None = None,
    ) -> tuple[object, int, bool]:
        intent, priority, ttl, timeout, cancel = _common(args)
        params = labios.LabelParams()
        params.type = label_type
        params.intent = intent
        params.priority = priority
        params.ttl_seconds = ttl
        if source:
            params.source_resource = labios.resource_from_uri(source)
        if destination:
            params.destination_resource = labios.resource_from_uri(destination)
        if pipeline is not None:
            params.pipeline = pipeline
        return params, timeout, cancel

    def _wait(self, operation: object, timeout_ms: int, cancel: bool) -> dict[str, Any]:
        waited = operation.wait_all(timeout_ms)
        items = list(getattr(waited, "results", ()))
        state = _enum_name(getattr(waited, "state", "unknown"))
        if state == "complete":
            return {
                "ok": True,
                "status": "completed",
                "label_id": int(operation.label_id()),
                "completion": _completion(items[0]) if items else {},
            }
        if state in {"failed", "cancelled", "unknown"}:
            item = items[0] if items else operation.test()
            return _terminal_failure(item)
        if state == "parked":
            item = items[0] if items else operation.test()
            return _error(
                "parked", "PARKED", "label is admitted and parked",
                label_id=int(operation.label_id()), retryable=True,
                completion=_completion(item),
            )
        if state != "timeout":
            return _error(
                "execution_failure", "INVALID_COMPLETION_STATE",
                f"unexpected completion state: {state}",
                label_id=int(operation.label_id()),
            )

        last = items[0] if items else operation.test()
        last_payload = _completion(last)
        if cancel:
            outcomes = list(operation.cancel())
            if not outcomes:
                return _error(
                    "completion_unknown", "COMPLETION_UNKNOWN_OR_EXPIRED",
                    "cancellation returned no label outcome",
                    label_id=int(operation.label_id()), completion=last_payload,
                )
            outcome = outcomes[0]
            cancellation = _enum_name(getattr(outcome, "state", "unknown"))
            if cancellation == "cancelled":
                return _error(
                    "cancelled", "CANCELED", "cancellation won before execution",
                    label_id=int(operation.label_id()),
                    cancellation="cancelled", completion=last_payload,
                )
            if cancellation == "unknown":
                return _error(
                    "completion_unknown", "COMPLETION_UNKNOWN_OR_EXPIRED",
                    "completion is unknown or expired",
                    label_id=int(operation.label_id()),
                    cancellation="unknown", completion=last_payload,
                )
            if cancellation == "terminal":
                terminal = getattr(outcome, "completion", None) or operation.test()
                projected = _terminal_failure(terminal)
                if _enum_name(getattr(terminal, "state", "")) == "complete":
                    return {
                        "ok": True, "status": "completed",
                        "label_id": int(operation.label_id()),
                        "cancellation": "terminal",
                        "completion": _completion(terminal),
                    }
                return projected
            return _error(
                "cancellation", "CANCELLATION_TOO_LATE",
                "worker execution won the cancellation race",
                label_id=int(operation.label_id()), retryable=True,
                cancellation="too_late", completion=last_payload,
            )

        if last_payload["state"] == "parked" or last_payload["lifecycle"] == "parked":
            return _error(
                "parked", "PARKED", "label is admitted and parked",
                label_id=int(operation.label_id()), retryable=True,
                completion=last_payload,
            )
        return _error(
            "timeout", "TIMEOUT", "wait deadline elapsed; operation remains active",
            label_id=int(operation.label_id()), retryable=True,
            completion=last_payload,
        )

    def store(self, args: Mapping[str, Any]) -> dict[str, Any]:
        allowed = {"destination", "data", "encoding", *_COMMON_PROPERTIES}
        _reject_unknown(args, allowed)
        destination = _string(args, "destination", required=True)
        data_text = _string(args, "data", required=True)
        encoding = args.get("encoding", "utf-8")
        if not isinstance(encoding, str):
            raise MalformedRequest("encoding must be a string")
        data = _decode_data(data_text, encoding)
        params, timeout, cancel = self._params(
            args, labios.LabelType.Write, destination=destination
        )
        label = self.client.create_label(params)
        operation = self.client.publish(label, data)
        result = self._wait(operation, timeout, cancel)
        result.update({"destination": destination, "size_bytes": len(data)})
        return result

    def retrieve(self, args: Mapping[str, Any]) -> dict[str, Any]:
        allowed = {"source", "size", "encoding", *_COMMON_PROPERTIES}
        _reject_unknown(args, allowed)
        source = _string(args, "source", required=True)
        size = _integer(args, "size", 0, 0, 2**63 - 1)
        encoding = args.get("encoding", "base64")
        if not isinstance(encoding, str):
            raise MalformedRequest("encoding must be a string")
        params, timeout, cancel = self._params(args, labios.LabelType.Read, source=source)
        label = self.client.create_label(params)
        label.data_size = size
        operation = self.client.publish(label)
        result = self._wait(operation, timeout, cancel)
        if result.get("ok"):
            data = bytes(operation.read(0))
            result.update({
                "source": source,
                "size_bytes": len(data),
                "encoding": encoding,
                "data": _encode_data(data, encoding),
            })
        return result

    def process(self, args: Mapping[str, Any]) -> dict[str, Any]:
        allowed = {"source", "destination", "pipeline", *_COMMON_PROPERTIES}
        _reject_unknown(args, allowed)
        source = _string(args, "source", required=True)
        destination = _string(args, "destination", required=True)
        rows = args.get("pipeline")
        if not isinstance(rows, list) or not rows:
            raise MalformedRequest("pipeline must be a non-empty array of stage objects")
        pipeline = labios.Pipeline()
        stages = []
        for index, row in enumerate(rows):
            if not isinstance(row, dict):
                raise MalformedRequest(f"pipeline[{index}] must be an object")
            _reject_unknown(row, {"operation", "args", "input_stage", "output_stage"})
            operation_name = _string(row, "operation", required=True)
            if operation_name not in _REGISTERED_PIPELINE_OPERATIONS:
                raise MalformedRequest(
                    f"pipeline[{index}] operation is not a registered Label I/O transform"
                )
            stage_args = row.get("args", "")
            if not isinstance(stage_args, str):
                raise MalformedRequest(f"pipeline[{index}].args must be a string")
            input_stage = _integer(row, "input_stage", -1, -1, index - 1)
            output_stage = _integer(row, "output_stage", -1, -1, len(rows) - 1)
            stages.append(labios.PipelineStage(
                operation_name, stage_args, input_stage, output_stage
            ))
        pipeline.stages = stages
        params, timeout, cancel = self._params(
            args, labios.LabelType.Write, source=source,
            destination=destination, pipeline=pipeline,
        )
        operation = self.client.publish(self.client.create_label(params))
        result = self._wait(operation, timeout, cancel)
        result.update({
            "source": source,
            "destination": destination,
            "stages": len(stages),
        })
        return result

    def observe(self, args: Mapping[str, Any]) -> dict[str, Any]:
        _reject_unknown(args, {"query", "label_id"})
        query = _string(args, "query", required=True)
        if query in {"label/status", "label/inspect"}:
            label_id = _integer(args, "label_id", 0, 1, 2**64 - 1)
            if query == "label/status":
                item = self.client.operation([label_id]).test()
                payload = _completion(item)
                if payload["state"] == "unknown":
                    return _error(
                        "completion_unknown", "COMPLETION_UNKNOWN_OR_EXPIRED",
                        payload["message"] or "completion is unknown or expired",
                        label_id=label_id, completion=payload,
                    )
                return {"ok": True, "status": "observed", "completion": payload}
            return {
                "ok": True,
                "status": "observed",
                "label": self._label(self.client.inspect_label(label_id)),
            }
        if query.startswith("data/location?"):
            raw = self.client.observe(query)
        elif query in _PUBLIC_OBSERVATIONS:
            raw = self.client.observe(query)
        else:
            raise MalformedRequest("query is not a public Observe/inspection route")
        try:
            observed = json.loads(raw)
        except (TypeError, json.JSONDecodeError) as exc:
            raise labios.ProtocolError("Observe returned malformed JSON") from exc
        return {"ok": True, "status": "observed", "query": query, "observation": observed}

    @staticmethod
    def _resource(resource: object | None) -> dict[str, Any] | None:
        if resource is None:
            return None
        return {
            "family": _enum_name(getattr(resource, "family", "unknown")),
            "backend_id": str(getattr(resource, "backend_id", "")),
            "logical_id": str(getattr(resource, "logical_id", "")),
            "namespace": str(getattr(resource, "namespace_name", "")),
            "database": str(getattr(resource, "database", "")),
            "path": str(getattr(resource, "path", "")),
            "key": str(getattr(resource, "key", "")),
            "offset": int(getattr(resource, "offset", 0)),
            "length": int(getattr(resource, "length", 0)),
        }

    @classmethod
    def _label(cls, label: object) -> dict[str, Any]:
        history = getattr(getattr(label, "placement_history", None), "decisions", ())
        decisions = [{
            "decision_id": int(getattr(row, "decision_id", 0)),
            "attempt": int(getattr(row, "attempt", 0)),
            "outcome": str(getattr(row, "outcome", "")),
            "chosen_worker_id": int(getattr(row, "chosen_worker_id", -1)),
            "park_reason": str(getattr(row, "park_reason", "")),
            "policy": str(getattr(row, "policy_name", "")),
        } for row in history]
        return {
            "id": int(getattr(label, "id", 0)),
            "ir_version": int(getattr(label, "ir_version", 0)),
            "operation": str(getattr(label, "operation", "")),
            "operation_version": int(getattr(label, "operation_version", 0)),
            "type": _enum_name(getattr(label, "type", "unknown")),
            "intent": _enum_name(getattr(label, "intent", "none")),
            "priority": int(getattr(label, "priority", 0)),
            "ttl_seconds": int(getattr(label, "ttl_seconds", 0)),
            "source": cls._resource(getattr(label, "source_resource", None)),
            "destination": cls._resource(getattr(label, "destination_resource", None)),
            "pipeline": [{
                "operation": str(stage.operation),
                "args": str(stage.args),
                "input_stage": int(stage.input_stage),
                "output_stage": int(stage.output_stage),
            } for stage in getattr(getattr(label, "pipeline", None), "stages", ())],
            "placement_history": decisions,
        }


_frontend: McpFrontend | None = None
_frontend_lock = threading.Lock()


def _default_frontend() -> McpFrontend:
    global _frontend
    with _frontend_lock:
        if _frontend is None:
            client = labios.connect_to(
                os.environ.get("LABIOS_NATS_URL", "nats://nats:4222"),
                os.environ.get("LABIOS_REDIS_HOST", "redis"),
                int(os.environ.get("LABIOS_REDIS_PORT", "6379")),
            )
            _frontend = McpFrontend(client)
    return _frontend


@app.list_tools()
async def list_tools() -> list[Tool]:
    return [
        Tool(
            name="labios_observe",
            description=(
                "Observe health, queue, workers, active scheduler profile, or a "
                "label lifecycle/residual through public LABIOS inspection APIs."
            ),
            inputSchema={
                "type": "object", "additionalProperties": False,
                "properties": {
                    "query": {"type": "string"},
                    "label_id": {"type": "integer", "minimum": 1},
                },
                "required": ["query"],
            },
        ),
        Tool(
            name="labios_store",
            description="Write exact bytes to an external backend through typed Label I/O.",
            inputSchema={
                "type": "object", "additionalProperties": False,
                "properties": {
                    "destination": {"type": "string", "description": "External backend URI."},
                    "data": {"type": "string"},
                    "encoding": {"type": "string", "enum": ["utf-8", "base64"], "default": "utf-8"},
                    **_COMMON_PROPERTIES,
                },
                "required": ["destination", "data"],
            },
        ),
        Tool(
            name="labios_retrieve",
            description="Read exact bytes from an external backend through typed Label I/O.",
            inputSchema={
                "type": "object", "additionalProperties": False,
                "properties": {
                    "source": {"type": "string", "description": "External backend URI."},
                    "size": {"type": "integer", "minimum": 0, "default": 0},
                    "encoding": {"type": "string", "enum": ["base64", "utf-8"], "default": "base64"},
                    **_COMMON_PROPERTIES,
                },
                "required": ["source"],
            },
        ),
        Tool(
            name="labios_process",
            description=(
                "Execute a structured, registered bytes-to-bytes pipeline from a "
                "source URI to a destination URI through one LABIOS worker."
            ),
            inputSchema={
                "type": "object", "additionalProperties": False,
                "properties": {
                    "source": {"type": "string"},
                    "destination": {"type": "string"},
                    "pipeline": {
                        "type": "array", "minItems": 1,
                        "items": {
                            "type": "object", "additionalProperties": False,
                            "properties": {
                                "operation": {"type": "string", "enum": sorted(_REGISTERED_PIPELINE_OPERATIONS)},
                                "args": {"type": "string", "default": ""},
                                "input_stage": {"type": "integer", "minimum": -1, "default": -1},
                                "output_stage": {"type": "integer", "minimum": -1, "default": -1},
                            },
                            "required": ["operation"],
                        },
                    },
                    **_COMMON_PROPERTIES,
                },
                "required": ["source", "destination", "pipeline"],
            },
        ),
        Tool(
            name="labios_knowledge",
            description="Reserved for workspace-backed knowledge in Prompt 14; currently returns a stable pending response.",
            inputSchema={"type": "object", "additionalProperties": False, "properties": {}},
        ),
    ]


@app.call_tool()
async def call_tool(name: str, arguments: dict[str, Any]) -> list[TextContent]:
    result = await asyncio.to_thread(_default_frontend().call, name, arguments)
    return _text(result)


async def main() -> None:
    async with stdio_server() as (read_stream, write_stream):
        await app.run(read_stream, write_stream, app.create_initialization_options())


if __name__ == "__main__":
    asyncio.run(main())
