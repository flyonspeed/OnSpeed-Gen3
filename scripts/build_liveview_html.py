"""build_liveview_html.py — compatibility shim.

The bundler logic moved to scripts/build_web_bundle.py.  This file
delegates so any stale platformio.ini snippet, CI step, or developer
muscle memory referencing the old name keeps working.  Delete after
every consumer has migrated to build_web_bundle.py.
"""
import os
import sys

# When loaded as a PIO extra_script, SCons exec's us inside its own
# package directory, so `__file__` is unset and `sys.argv[0]` points
# at scons.  PIO does provide PROJECT_DIR via `Import("env")`, which
# is the only reliable handle to the repo root.
try:
    Import("env")  # noqa: F821 — provided by SCons in PIO context
    REPO_ROOT = env["PROJECT_DIR"]  # noqa: F821
except (NameError, Exception):
    try:
        REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    except NameError:
        REPO_ROOT = os.getcwd()

NEW_SCRIPT = os.path.join(REPO_ROOT, "scripts", "build_web_bundle.py")

with open(NEW_SCRIPT, "r", encoding="utf-8") as f:
    code = f.read()

# Re-exec the new bundler in the current globals (preserves the
# `env`/`REPO_ROOT` we resolved above).
exec(compile(code, NEW_SCRIPT, "exec"), globals())
