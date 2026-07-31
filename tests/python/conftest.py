"""Python SDK test configuration."""
from __future__ import annotations

from pathlib import Path
import os
import sys

ROOT = Path(__file__).resolve().parents[2]
BUILD_PYTHON = Path(os.environ.get(
    "LABIOS_BUILD_PYTHON", ROOT / "build" / "dev" / "python"))
SOURCE_PYTHON = ROOT / "src" / "python"

# Prefer the freshly built package, then allow source-only registry tests.
for path in (SOURCE_PYTHON, BUILD_PYTHON):
    if path.is_dir():
        sys.path.insert(0, str(path))


def pytest_configure(config):
    config.addinivalue_line("markers", "live: requires the running Compose topology")


def live_enabled() -> bool:
    return os.environ.get("LABIOS_PYTHON_LIVE") == "1"
