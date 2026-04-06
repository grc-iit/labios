"""Basic tests for LABIOS Python SDK."""
import sys
import os

# Add the build output to path
build_dir = os.path.join(os.path.dirname(__file__), '..', '..', 'build', 'dev', 'python')
if os.path.isdir(build_dir):
    sys.path.insert(0, build_dir)


def test_import():
    """Module loads and exposes expected symbols."""
    import _labios
    assert hasattr(_labios, 'Client')
    assert hasattr(_labios, 'Intent')
    assert hasattr(_labios, 'LabelType')
    assert hasattr(_labios, 'connect')
    assert hasattr(_labios, 'connect_to')


def test_enums():
    """Enum values are accessible."""
    import _labios
    assert _labios.Intent.CHECKPOINT is not None
    assert _labios.LabelType.Write is not None
    assert _labios.Isolation.AGENT is not None
    assert _labios.Durability.EPHEMERAL is not None


def test_config():
    """Config object can be created."""
    import _labios
    cfg = _labios.Config()
    cfg.nats_url = "nats://localhost:4222"
    cfg.redis_host = "localhost"
    cfg.redis_port = 6379
    assert cfg.nats_url == "nats://localhost:4222"
