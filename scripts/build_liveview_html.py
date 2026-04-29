"""build_liveview_html.py — PIO pre-build hook.

Generates `software/OnSpeed-Gen3-ESP32/Web/html_indexer.h` from the
ES module sources at `tools/liveview-prototype/lib/`. The prototype
is the source of truth for the SVG renderer; this script bundles its
many JS files plus a firmware-side shell (WebSocket client, datafields
table) into one PROGMEM string the firmware serves at GET /indexer.

The legacy /live page lives untouched at
software/OnSpeed-Gen3-ESP32/Web/html_liveview.h — pilots familiar
with that interface keep it. The new 5-mode SVG view replaces the
old WASM-on-device /indexer (different page, same URL slot).

Skip-if-fresh: regenerates only when any prototype source file is
newer than the output header, OR when the header is missing. On a
typical edit-and-build cycle that doesn't touch prototype sources,
this hook does nothing.

Standalone usage (outside PlatformIO):
    python3 scripts/build_liveview_html.py

PlatformIO usage (auto): registered as `pre:` extra_script in
platformio.ini's shared [env] block.

Module transformation strategy
==============================

ES modules can't run cross-imported as a single concatenated <script>.
Browsers need either separate file URLs or import maps. We can't ship
either from a single PROGMEM blob, so we transform the modules into
IIFE-wrapped factories that build a `__mod_<name>` registry, then
synthesize destructuring binders for each `import` statement.

For each module file:
    export const X = ...  →  exports.X = ...
    export function X(...) { ... }  →  exports.X = function X(...) { ... }
    export default X  →  ERROR (we don't use default exports anywhere)

Each module body is wrapped:
    const __mod_<name> = (function() {
      const exports = {};
      <transformed body>
      return exports;
    })();

Each import is rewritten:
    import { a, b } from './x.js'  →  const { a, b } = __mod_x;
    import * as G from './x.js'    →  const G = __mod_x;

Module ordering is topological by import graph. Cycles abort with a
clear error.
"""
import os
import re
import sys

# PIO scripts are exec'd, not imported, so __file__ might not be defined.
try:
    SCRIPT_PATH = os.path.abspath(__file__)
except NameError:
    SCRIPT_PATH = os.path.abspath(sys.argv[0]) if sys.argv else os.getcwd()

# When loaded as a PIO extra_script the SCons env is in scope.
try:
    Import("env")  # noqa: F821 — provided by SCons in PIO context
    REPO_ROOT = env["PROJECT_DIR"]  # noqa: F821
    _IN_PIO = True
except (NameError, Exception):
    REPO_ROOT = os.path.dirname(os.path.dirname(SCRIPT_PATH))
    _IN_PIO = False

PROTOTYPE_DIR = os.path.join(REPO_ROOT, "tools", "liveview-prototype")
LIB_DIR       = os.path.join(PROTOTYPE_DIR, "lib")
FIRMWARE_DIR  = os.path.join(LIB_DIR, "firmware")
WEB_DIR       = os.path.join(REPO_ROOT, "software", "OnSpeed-Gen3-ESP32", "Web")
OUTPUT_HEADER = os.path.join(WEB_DIR, "html_indexer.h")


def _module_name(path):
    """Path → module key.  lib/widgets/indexer.js → widgets_indexer."""
    rel = os.path.relpath(path, LIB_DIR)
    rel = rel[:-3] if rel.endswith(".js") else rel  # drop .js
    return rel.replace(os.sep, "_").replace("-", "_")


def _resolve_import(importing_path, spec):
    """Resolve a relative import like '../widgets/foo.js' to absolute path."""
    base_dir = os.path.dirname(importing_path)
    return os.path.normpath(os.path.join(base_dir, spec))


# ---------------------------------------------------------------------
# Source enumeration (JS modules to bundle).
# ---------------------------------------------------------------------

def _all_js_files():
    """Return all *.js files we want to bundle, as absolute paths.

    Excludes:
      - lib/main.js (browser harness — the firmware bundle has its
        own entry point at lib/firmware/main.js)
      - lib/scenarios.js (synthetic scenario generator — irrelevant
        in firmware where data comes from a real WebSocket)
      - lib/frameBuilder.js (used only by the wasm-live A/B harness)
    """
    EXCLUDE = {"main.js", "scenarios.js", "frameBuilder.js"}

    out = []
    for root, _dirs, files in os.walk(LIB_DIR):
        for name in files:
            if not name.endswith(".js"):
                continue
            rel = os.path.relpath(os.path.join(root, name), LIB_DIR)
            # Top-level skips. Firmware-dir files are kept.
            if os.sep not in rel and name in EXCLUDE:
                continue
            out.append(os.path.join(root, name))
    return sorted(out)


# ---------------------------------------------------------------------
# Import / export transformations.
# ---------------------------------------------------------------------

# Regex helpers.  These are intentionally conservative: each matches a
# *line-start* form so a string literal containing the keyword inside
# code won't be touched.  All prototype files use the line-start
# convention because the LESSONS doc enforces it.
_RE_IMPORT_NAMED   = re.compile(r"^import\s*\{\s*([^}]+)\s*\}\s*from\s*['\"]([^'\"]+)['\"]\s*;?\s*$", re.MULTILINE)
_RE_IMPORT_NAMESPACE = re.compile(r"^import\s*\*\s*as\s*(\w+)\s*from\s*['\"]([^'\"]+)['\"]\s*;?\s*$", re.MULTILINE)
_RE_IMPORT_MULTILINE = re.compile(
    r"^import\s*\{\s*\n([^}]+?)\n\s*\}\s*from\s*['\"]([^'\"]+)['\"]\s*;?\s*$",
    re.MULTILINE,
)
_RE_EXPORT_DEFAULT = re.compile(r"^export\s+default\b", re.MULTILINE)
_RE_EXPORT_DECL    = re.compile(r"^export\s+(const|let|function|class)\s+", re.MULTILINE)


def _parse_imports(text, importing_path):
    """Extract all imports from a module's source.

    Returns a list of (kind, spec_path, names) where:
      kind = 'named'      → names is a list of identifiers
      kind = 'namespace'  → names is a single identifier (the alias)

    Multi-line imports are matched and then stripped from `text` before
    single-line matching runs, so a single `import { ... } from` block
    isn't double-counted by both regexes.
    """
    imports = []
    work = text

    for m in _RE_IMPORT_MULTILINE.finditer(work):
        names = [n.strip() for n in m.group(1).split(",") if n.strip()]
        spec = _resolve_import(importing_path, m.group(2))
        imports.append(("named", spec, names))
    work = _RE_IMPORT_MULTILINE.sub("", work)

    for m in _RE_IMPORT_NAMED.finditer(work):
        names = [n.strip() for n in m.group(1).split(",") if n.strip()]
        spec = _resolve_import(importing_path, m.group(2))
        imports.append(("named", spec, names))

    for m in _RE_IMPORT_NAMESPACE.finditer(work):
        spec = _resolve_import(importing_path, m.group(2))
        imports.append(("namespace", spec, m.group(1)))

    return imports


def _strip_imports(text):
    """Remove all import lines (multi-line first to avoid partial matches)."""
    text = _RE_IMPORT_MULTILINE.sub("", text)
    text = _RE_IMPORT_NAMED.sub("", text)
    text = _RE_IMPORT_NAMESPACE.sub("", text)
    return text


def _transform_exports(text, src_path):
    """Rewrite `export <decl>` into `exports.<name> = ...` form.

    `export default` is forbidden (we don't use it; bail with an error).
    """
    if _RE_EXPORT_DEFAULT.search(text):
        raise SystemExit(
            f"build_liveview_html: `export default` not supported in {src_path}. "
            f"Use named exports."
        )

    out = []
    pos = 0
    for m in _RE_EXPORT_DECL.finditer(text):
        out.append(text[pos:m.start()])
        kind = m.group(1)
        # Find the identifier that immediately follows.
        rest_start = m.end()
        ident_match = re.match(r"(\w+)", text[rest_start:])
        if not ident_match:
            raise SystemExit(
                f"build_liveview_html: malformed export at offset {m.start()} in {src_path}"
            )
        ident = ident_match.group(1)

        # Drop the `export` keyword (and one whitespace separator); leave
        # the kind and identifier in place. The identifier is already in
        # `text` starting at `rest_start`, so we resume reading from there.
        if kind in ("const", "let", "function", "class"):
            out.append(f"{kind} ")
            pos = rest_start  # resume at the identifier; original text takes over
        else:
            raise SystemExit(
                f"build_liveview_html: unexpected export kind '{kind}' at {src_path}"
            )

    out.append(text[pos:])
    return "".join(out)


def _collect_exported_names(original_text):
    """Find every identifier the original module exports, before stripping."""
    names = []
    for m in _RE_EXPORT_DECL.finditer(original_text):
        rest_start = m.end()
        ident_match = re.match(r"(\w+)", original_text[rest_start:])
        if ident_match:
            names.append(ident_match.group(1))
    return names


# ---------------------------------------------------------------------
# Topological sort.
# ---------------------------------------------------------------------

def _topo_sort(modules):
    """modules is dict { path: { 'imports': [path, ...], ... } }.

    Returns ordered list of paths so dependencies come first.
    Raises on cycles.
    """
    visited = set()
    visiting = set()
    order = []

    def visit(path):
        if path in visited:
            return
        if path in visiting:
            raise SystemExit(
                f"build_liveview_html: import cycle detected involving {path}"
            )
        visiting.add(path)
        for dep in modules[path]["imports"]:
            if dep in modules:
                visit(dep)
            else:
                # Imported file is outside the bundle (shouldn't happen
                # given _all_js_files semantics).
                raise SystemExit(
                    f"build_liveview_html: {path} imports {dep} which is "
                    f"not in the module set."
                )
        visiting.discard(path)
        visited.add(path)
        order.append(path)

    for p in sorted(modules.keys()):
        visit(p)
    return order


# ---------------------------------------------------------------------
# Bundle generation.
# ---------------------------------------------------------------------

def _build_import_binders(path, original_text):
    """Synthesize destructuring binders for each of `path`'s imports.

    `import { X }` becomes `const { X } = ...`; `import { X as Y }`
    becomes `const { X: Y } = ...` (ES module rename → JS destructure
    rename).
    """
    imports = _parse_imports(original_text, path)
    out = []
    for kind, spec, names in imports:
        spec_name = _module_name(spec)
        if kind == "named":
            translated = []
            for nm in names:
                # "X as Y" → "X: Y" for destructuring rename.
                m = re.match(r"^(\w+)\s+as\s+(\w+)$", nm)
                if m:
                    translated.append(f"{m.group(1)}: {m.group(2)}")
                else:
                    translated.append(nm)
            joined = ", ".join(translated)
            out.append(f"const {{ {joined} }} = __mod_{spec_name};")
        else:  # namespace
            out.append(f"const {names} = __mod_{spec_name};")
    return "\n".join(out)


def _bundle_javascript():
    """Emit the entire JS bundle as a single string.

    Strategy: each module gets an IIFE, but the IIFE itself sees the
    namespace bindings of its imports BEFORE its own body runs. We
    achieve this by emitting the import binders inside the IIFE, just
    before the module body — so each module's local scope has access
    to its imports' exports.
    """
    paths = _all_js_files()

    # Read every file, parse imports.
    modules = {}
    for path in paths:
        with open(path, "r", encoding="utf-8") as f:
            text = f.read()
        imports = _parse_imports(text, path)
        modules[path] = {
            "text": text,
            "imports": [spec for (_kind, spec, _names) in imports],
        }

    ordered = _topo_sort(modules)

    chunks = ["// ===== Liveview JS bundle ====="]
    for path in ordered:
        info = modules[path]
        name = _module_name(path)
        binders = _build_import_binders(path, info["text"])
        # Strip imports + transform exports.
        body = _strip_imports(info["text"])
        body = _transform_exports(body, path)
        exported = _collect_exported_names(info["text"])
        bindings = "\n".join(f"  exports.{n} = {n};" for n in exported)

        chunks.append(f"""\
// === {os.path.relpath(path, REPO_ROOT)} ===
const __mod_{name} = (function() {{
  const exports = {{}};
  {indent(binders, 2)}
{body}
{bindings}
  return exports;
}})();
""")

    # Finally, run the firmware entry point. By convention the file at
    # lib/firmware/main.js exports a top-level `start()` function we
    # call here.
    if any(p.endswith(os.path.join("firmware", "main.js")) for p in ordered):
        chunks.append("// === firmware entry point ===")
        chunks.append("if (__mod_firmware_main && __mod_firmware_main.start) {")
        chunks.append("  document.addEventListener('DOMContentLoaded', __mod_firmware_main.start);")
        chunks.append("}")

    return "\n".join(chunks)


def indent(text, n):
    """Indent each line of `text` by `n` spaces (skipping empty lines)."""
    pad = " " * n
    return "\n".join((pad + line if line else line) for line in text.splitlines())


# ---------------------------------------------------------------------
# CSS bundling.
# ---------------------------------------------------------------------

def _bundle_css():
    """Concatenate prototype CSS + firmware-specific CSS."""
    parts = ["/* ===== Liveview CSS bundle ===== */"]
    main_css = os.path.join(PROTOTYPE_DIR, "style.css")
    fw_css   = os.path.join(FIRMWARE_DIR, "style.css")
    for path in (main_css, fw_css):
        if os.path.exists(path):
            with open(path, "r", encoding="utf-8") as f:
                parts.append(f"/* --- {os.path.relpath(path, REPO_ROOT)} --- */")
                parts.append(f.read())
    return "\n".join(parts)


# ---------------------------------------------------------------------
# HTML scaffold.
# ---------------------------------------------------------------------

HTML_TEMPLATE = """\
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, user-scalable=no">
<title>OnSpeed LiveView</title>
<style>
{css}
</style>
</head>
<body>
<header id="liveview-header">
  <div id="status-line">
    <span id="connectionstatus">CONNECTING...</span>
    <span id="age-indicator"></span>
  </div>
</header>
<nav id="mode-nav">
  <button data-mode="aoa" type="button">AOA</button>
  <button data-mode="attitude" type="button">Attitude</button>
  <button data-mode="indexer-only" type="button">Indexer</button>
  <button data-mode="energy" type="button">Energy</button>
  <button data-mode="ghistory" type="button">G-Hist</button>
</nav>
<main id="liveview-main">
  <div id="mode-container">
    <div data-mode-panel="aoa"></div>
    <div data-mode-panel="attitude" style="display:none;"></div>
    <div data-mode-panel="indexer-only" style="display:none;"></div>
    <div data-mode-panel="energy" style="display:none;"></div>
    <div data-mode-panel="ghistory" style="display:none;"></div>
  </div>
  <div id="datafields-wrap">
    <button id="datafields-toggle" type="button">Show data fields</button>
    <div id="datafields" style="display:none;"></div>
  </div>
</main>
<footer id="liveview-footer">
  <div id="footer-warning">For diagnostic purposes only. NOT SAFE FOR FLIGHT</div>
</footer>
<script>
{js}
</script>
</body>
</html>
"""


# ---------------------------------------------------------------------
# Output: PROGMEM C header.
# ---------------------------------------------------------------------

# C++ raw-string delimiters we'll try in order. The first one whose
# closing sequence isn't present in the content wins.
_RAW_DELIMS = ["=====", "lvw=", "lvw==", "lvw===", "deadbeef"]


def _pick_raw_delim(content):
    for d in _RAW_DELIMS:
        if f"){d}\"" not in content and f"){d})" not in content:
            return d
    raise SystemExit(
        "build_liveview_html: every preset R-string delimiter appears in "
        "the bundled content. Add a more exotic delimiter to _RAW_DELIMS."
    )


def _emit_header(html, out_path):
    delim = _pick_raw_delim(html)
    header = f"""\
// AUTO-GENERATED by scripts/build_liveview_html.py — DO NOT EDIT BY HAND.
//
// Source of truth: tools/liveview-prototype/ (CSS, JS modules, firmware
// shell). Run `python3 scripts/build_liveview_html.py` to regenerate
// after editing prototype source. PlatformIO auto-runs the regenerator
// as a pre-build hook.

const char htmlIndexer[] PROGMEM = R"{delim}({html}){delim}";
"""
    with open(out_path, "w", encoding="utf-8") as f:
        f.write(header)


# ---------------------------------------------------------------------
# Skip-if-fresh.
# ---------------------------------------------------------------------

def _walk_inputs():
    """Every file whose mtime should trigger a regen."""
    yield SCRIPT_PATH
    for root, _dirs, files in os.walk(LIB_DIR):
        for name in files:
            yield os.path.join(root, name)
    css = os.path.join(PROTOTYPE_DIR, "style.css")
    if os.path.exists(css):
        yield css


def _needs_rebuild():
    if not os.path.exists(OUTPUT_HEADER):
        return True
    out_mtime = os.path.getmtime(OUTPUT_HEADER)
    for p in _walk_inputs():
        try:
            if os.path.getmtime(p) > out_mtime:
                return True
        except FileNotFoundError:
            continue
    return False


# ---------------------------------------------------------------------
# Main.
# ---------------------------------------------------------------------

def main():
    if not _needs_rebuild():
        return  # Nothing to do.

    js  = _bundle_javascript()
    css = _bundle_css()
    html = HTML_TEMPLATE.format(css=css, js=js)

    os.makedirs(os.path.dirname(OUTPUT_HEADER), exist_ok=True)
    _emit_header(html, OUTPUT_HEADER)

    size = os.path.getsize(OUTPUT_HEADER)
    print(f"build_liveview_html: wrote {OUTPUT_HEADER} ({size:,} bytes)")
    if size > 256 * 1024:
        raise SystemExit(
            f"build_liveview_html: output is {size} bytes (>256 KB). "
            f"Verify PROGMEM headroom before committing."
        )


main()
