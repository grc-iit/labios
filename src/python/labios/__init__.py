"""LABIOS 2.1 Python Label I/O SDK.

The native ``Operation`` is the owning asynchronous handle.  It remains valid
after its originating ``Client`` is destroyed and timeout observations never
cancel or consume it.
"""
__version__ = "2.1.0rc1"

try:
    from _labios import (
        AuthorizationError,
        BackendError,
        BindingProvenance,
        CancellationResult,
        CancellationState,
        CancelledError,
        CandidateEvaluation,
        Client,
        ClientError,
        CompletionError,
        CompletionLookupError,
        CompletionResult,
        CompletionState,
        Config,
        DependencyError,
        Durability,
        ExecutionError,
        ExpiredError,
        Intent,
        InvalidArgumentError,
        Isolation,
        Label,
        LabelParams,
        LabelType,
        LabiosError,
        LifecycleState,
        MalformedBufferError,
        Operation,
        OperationKind,
        PendingIO,
        Pipeline,
        PipelineError,
        PipelineStage,
        PlacementDecision,
        PlacementHistory,
        ProtocolError,
        ResourceError,
        ResourceFamily,
        ResourceRef,
        SessionShutdownError,
        StagedInputBinding,
        StatusCode,
        SubmissionError,
        TimeoutError,
        UnsupportedVersionError,
        ValidationError,
        WaitResult,
        connect,
        connect_to,
        load_config,
        resource_from_uri,
    )
except ImportError as error:
    raise ImportError(
        "LABIOS native module not found. Build with: "
        "cmake --preset dev && cmake --build build/dev"
    ) from error

from .registry_v2 import (
    Attachment as RegistryAttachment,
    MalformedRegistryBuffer,
    RegistryV2Error,
    UnexpectedRegistryPayload,
    UnsupportedRegistryVersion,
    WorkerDeregistration,
    WorkerDescriptor,
    WorkerRegistration,
    WorkerRegistryMessage,
    WorkerRegistrySnapshot,
    WorkerResourceUpdate,
    parse_worker_registry_message,
)

__all__ = [name for name in globals() if not name.startswith("_")]
