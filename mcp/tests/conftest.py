"""MCP pytest configuration."""
from __future__ import annotations

from pathlib import Path
import os
import sys

ROOT = Path(__file__).resolve().parents[2]
for path in (
    ROOT / "mcp",
    Path(os.environ.get("LABIOS_BUILD_PYTHON", ROOT / "build" / "dev" / "python")),
):
    if path.is_dir():
        sys.path.insert(0, str(path))


def pytest_configure(config):
    config.addinivalue_line("markers", "live: requires the running Compose topology")
