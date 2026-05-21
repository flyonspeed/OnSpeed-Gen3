# Make the parent directory importable so tests can `import capture_perf_report`.
# pyserial is imported at module top of the script; install it via:
#   uv run --with pytest --with pyserial python -m pytest tools/perf-report/tests
# or via the system pytest with `pip install pyserial pytest`.

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
