"""embed_sim_prebuild.py — PIO pre-build hook.

Regenerates the M5 sim's PROGMEM headers (Web/sim_index_{js,wasm}.h) when
any of their inputs has been modified more recently than the headers
themselves. On a typical edit-and-build cycle that doesn't touch M5 sim
sources, this hook does nothing — emscripten is not invoked.

If the headers are missing entirely, regeneration is forced (so a fresh
clone or after a clean does the right thing).

If `emcc` is not on PATH, we surface a clear error message pointing at
the install instructions; we do not silently skip.

The Skip-if-fresh design lets contributors who never touch M5 sim
code avoid installing emscripten altogether (after the headers have
been generated once). CI always installs emscripten and runs the hook
on every build, so released firmware is always in sync.
"""
import os
import shutil
import subprocess
import sys

Import("env")

# PIO scripts are exec'd, not imported, so __file__ is not defined.
# PROJECT_DIR is the repo root (where platformio.ini lives).
REPO_ROOT = env["PROJECT_DIR"]
WEB_DIR   = os.path.join(REPO_ROOT, "software", "OnSpeed-Gen3-ESP32", "Web")
SIM_DIR   = os.path.join(REPO_ROOT, "software", "OnSpeed-M5-Display")

HEADER_JS   = os.path.join(WEB_DIR, "sim_index_js.h")
HEADER_WASM = os.path.join(WEB_DIR, "sim_index_wasm.h")

EMBED_SCRIPT = os.path.join(REPO_ROOT, "scripts", "embed_sim_for_firmware.sh")
THIS_SCRIPT  = os.path.join(REPO_ROOT, "scripts", "embed_sim_prebuild.py")


def _walk_sources():
    """All paths whose mtime should trigger a regen.

    Two trees plus two scripts:
      * The M5 firmware source (rendered into the WASM).
      * The onspeed_core source (linked into the WASM).
      * The sim entry point + build script.
      * This hook itself.
    """
    roots = [
        os.path.join(SIM_DIR, "src"),
        os.path.join(SIM_DIR, "include"),
        os.path.join(SIM_DIR, "lib", "GaugeWidgets"),
        os.path.join(SIM_DIR, "sim"),
        os.path.join(REPO_ROOT, "software", "Libraries", "onspeed_core", "src"),
    ]
    for root in roots:
        if not os.path.isdir(root):
            continue
        for dirpath, _dirnames, filenames in os.walk(root):
            for name in filenames:
                if name.startswith(".") or name.endswith(".o"):
                    continue
                yield os.path.join(dirpath, name)
    yield EMBED_SCRIPT
    yield THIS_SCRIPT


def _newest_input_mtime():
    newest = 0.0
    for path in _walk_sources():
        try:
            mtime = os.path.getmtime(path)
        except OSError:
            continue
        if mtime > newest:
            newest = mtime
    return newest


def _headers_present_and_fresh():
    if not (os.path.isfile(HEADER_JS) and os.path.isfile(HEADER_WASM)):
        return False
    js_mtime   = os.path.getmtime(HEADER_JS)
    wasm_mtime = os.path.getmtime(HEADER_WASM)
    header_mtime = min(js_mtime, wasm_mtime)
    return _newest_input_mtime() <= header_mtime


def _run_embed_script():
    if shutil.which("emcc") is None:
        sys.stderr.write(
            "\n[embed-sim-prebuild] ERROR: emcc not found on PATH.\n"
            "  The M5 sim WASM artifacts (Web/sim_index_*.h) need to be\n"
            "  regenerated for this build. Install emscripten:\n"
            "    macOS:  brew install emscripten\n"
            "    Linux:  https://emscripten.org/docs/getting_started/downloads.html\n"
            "  Then re-run the build.\n\n"
        )
        env.Exit(1)

    print("[embed-sim-prebuild] Sources newer than headers; regenerating...")
    result = subprocess.run(["bash", EMBED_SCRIPT], cwd=REPO_ROOT)
    if result.returncode != 0:
        sys.stderr.write("[embed-sim-prebuild] embed script failed.\n")
        env.Exit(result.returncode)


if _headers_present_and_fresh():
    print("[embed-sim-prebuild] Headers up to date; skipping regen.")
else:
    _run_embed_script()
