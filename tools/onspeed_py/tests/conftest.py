"""pytest config: make `onspeed_py` importable when running tests
directly out of the `tools/onspeed_py/tests/` directory."""

from __future__ import annotations

import sys
from pathlib import Path

# Add the parent of `onspeed_py` (i.e. `tools/`) to sys.path so
# `from onspeed_py.frame import ...` works without an installed package.
TESTS_DIR = Path(__file__).resolve().parent
TOOLS_DIR = TESTS_DIR.parent.parent
sys.path.insert(0, str(TOOLS_DIR))
