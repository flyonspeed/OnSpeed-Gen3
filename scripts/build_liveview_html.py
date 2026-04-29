"""build_liveview_html.py — PIO pre-build hook.

Generates `software/OnSpeed-Gen3-ESP32/Web/html_indexer.h` from the
ES module sources at `tools/liveview-prototype/lib/`.

The legacy /live page lives untouched at
software/OnSpeed-Gen3-ESP32/Web/html_liveview.h — pilots familiar
with that interface keep it. The new 5-mode SVG view replaces the
old WASM-on-device /indexer (different page, same URL slot).

Skip-if-fresh: regenerates only when any prototype source file is
newer than the output header, OR when the header is missing.

Standalone usage:
    python3 scripts/build_liveview_html.py

PlatformIO usage (auto): registered as `pre:` extra_script in
platformio.ini's shared [env] block.

Module bundling strategy
========================

The prototype's modules are bundled into a single ES module. Each
module file is concatenated in topological order; `import` statements
are stripped (the imported names already exist in the shared module
scope from earlier files); `export` keywords are stripped from
declarations (`export const X` → `const X`).

The vendored Preact bundle (lib/vendor/preact-standalone.js) is the
ONE exception. It contains its own ES module exports; we treat its
bytes as opaque and rewrite its terminal `export { ... }` block into
plain `var` aliases so all subsequent files see `html`, `render`,
`useState`, etc. as in-scope names.

The whole concatenated bundle is wrapped in a `<script type="module">`
inside the HTML scaffold, then the HTML is emitted as a C++ R-string.
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
except (NameError, Exception):
    REPO_ROOT = os.path.dirname(os.path.dirname(SCRIPT_PATH))

PROTOTYPE_DIR = os.path.join(REPO_ROOT, "tools", "liveview-prototype")
LIB_DIR       = os.path.join(PROTOTYPE_DIR, "lib")
WEB_DIR       = os.path.join(REPO_ROOT, "software", "OnSpeed-Gen3-ESP32", "Web")
OUTPUT_HEADER = os.path.join(WEB_DIR, "html_indexer.h")

# Files we DON'T bundle:
#   - lib/main.js         — entry point for the synthetic-scenario harness
#   - lib/scenarios.js    — synthetic scenarios (irrelevant in firmware)
#   - lib/frameBuilder.js — wasm-live A/B harness only
EXCLUDE_TOPLEVEL = {"main.js", "scenarios.js", "frameBuilder.js"}

# The vendored Preact bundle: must be emitted FIRST and treated specially.
PREACT_BUNDLE = os.path.join(LIB_DIR, "vendor", "preact-standalone.js")

# The firmware entry point: emitted LAST, calls start() on DOMContentLoaded.
FIRMWARE_ENTRY = os.path.join(LIB_DIR, "firmware", "App.js")


# ---------------------------------------------------------------------
# Source enumeration + topological sort.
# ---------------------------------------------------------------------

def _all_js_files():
    """Every prototype JS file we want to bundle, in walk order.

    Excludes the harness-only files at the top level. The bundler
    decides emit order based on imports (topo sort below).
    """
    out = []
    for root, _dirs, files in os.walk(LIB_DIR):
        for name in files:
            if not name.endswith(".js"):
                continue
            rel = os.path.relpath(os.path.join(root, name), LIB_DIR)
            if os.sep not in rel and name in EXCLUDE_TOPLEVEL:
                continue
            out.append(os.path.join(root, name))
    return sorted(out)


# Match an `import` declaration. Anchored to line-start with re.MULTILINE
# so we don't accidentally match the substring "import" inside a string
# literal — but conservative: a line starting with `import` followed by
# anything up to a `from '...'` clause and an optional semicolon.
_RE_IMPORT = re.compile(
    r"^import\b[^;]*?from\s*['\"]([^'\"]+)['\"]\s*;?\s*$",
    re.MULTILINE | re.DOTALL,
)


def _imports_in(text):
    """Return the list of resolved import-spec paths in `text`.

    Only `import { ... } from './x.js'` and `import * as G from ...`
    forms are recognized (which is all we use). Side-effect-only
    imports (`import './x.js'`) and dynamic imports are ignored.
    """
    return [m.group(1) for m in _RE_IMPORT.finditer(text) if m.group(1)]


def _topo_sort(files):
    """Sort files so each one's imports appear earlier in the result."""
    # Map abs-path → file metadata.
    by_path = {}
    for path in files:
        with open(path, "r", encoding="utf-8") as f:
            text = f.read()
        deps = []
        for spec in _imports_in(text):
            base = os.path.dirname(path)
            resolved = os.path.normpath(os.path.join(base, spec))
            if resolved in (set(files)):
                deps.append(resolved)
        by_path[path] = {"text": text, "deps": deps}

    visited, visiting, ordered = set(), set(), []

    def visit(p):
        if p in visited:
            return
        if p in visiting:
            raise SystemExit(f"build_liveview_html: import cycle through {p}")
        visiting.add(p)
        for dep in by_path[p]["deps"]:
            if dep in by_path:
                visit(dep)
        visiting.discard(p)
        visited.add(p)
        ordered.append(p)

    # Start with files that nothing else imports — leaves of the DAG.
    for p in sorted(files):
        visit(p)

    return ordered, by_path


# ---------------------------------------------------------------------
# Per-file transformations.
# ---------------------------------------------------------------------

def _strip_imports(text):
    """Remove every `import` line from text."""
    return _RE_IMPORT.sub("", text)


def _strip_exports(text):
    """Drop the `export ` keyword from declarations.

    `export const X = ...`    → `const X = ...`
    `export function X(...)`  → `function X(...)`
    `export class X`          → `class X`

    `export { a, b as c }`   → emit `var c = b;` aliases for renamed
    exports; drop bare `export { x }` lines (x is already in scope).
    `export default X`        → ERROR (we don't use it).
    """
    if re.search(r"^export\s+default\b", text, re.MULTILINE):
        raise SystemExit("build_liveview_html: `export default` not supported")

    # `export const|let|var|function|class X`
    text = re.sub(r"^export\s+(const|let|var|function|class)\s+",
                  r"\1 ", text, flags=re.MULTILINE)

    # `export { a, b as c, ... };` — rewrite renamed bindings to aliases,
    # drop the rest. The minified Preact bundle has its export block on
    # the same line as the rest of the source (no leading newline), so
    # we don't anchor to ^ — match anywhere.
    def _rewrite_export_block(m):
        body = m.group(1)
        out_lines = []
        for piece in body.split(","):
            piece = piece.strip()
            if not piece:
                continue
            mm = re.match(r"^(\w+)\s+as\s+(\w+)$", piece)
            if mm:
                out_lines.append(f"var {mm.group(2)} = {mm.group(1)};")
            # Bare `export { x }` — x is already a top-level binding
            # in this module, no rewrite needed.
        return "\n" + "\n".join(out_lines)

    text = re.sub(r"export\s*\{([^}]+)\}\s*;?",
                  _rewrite_export_block, text)
    return text


def _transform_module(text):
    """Strip imports + exports, leaving plain JS in shared scope."""
    return _strip_exports(_strip_imports(text))


_RE_NAMESPACE_IMPORT = re.compile(
    r"^import\s*\*\s*as\s*(\w+)\s*from\s*['\"]([^'\"]+)['\"]\s*;?\s*$",
    re.MULTILINE,
)


def _namespace_aliases(by_path):
    """Generate `const G = { CONSTANT_A, CONSTANT_B, ... };` declarations.

    For every `import * as X from './foo.js'` we find across all bundled
    files, emit a single namespace alias at the END of the bundle so the
    namespace is available everywhere it's referenced.

    The constants/functions exported by `./foo.js` get gathered from the
    file's source via a regex over its `export const/function/class`
    declarations. Inline references like `G.MODE1_HORIZON_CX` then resolve
    to the alias's properties.
    """
    aliases = []  # list of (alias-name, exporter-path)
    seen = set()
    for path, info in by_path.items():
        for m in _RE_NAMESPACE_IMPORT.finditer(info["text"]):
            alias_name = m.group(1)
            spec_path = os.path.normpath(os.path.join(os.path.dirname(path), m.group(2)))
            if (alias_name, spec_path) in seen:
                continue
            seen.add((alias_name, spec_path))
            aliases.append((alias_name, spec_path))

    out = []
    for alias_name, exporter_path in aliases:
        if exporter_path not in by_path:
            continue
        names = _exported_names(by_path[exporter_path]["text"])
        if not names:
            continue
        out.append(f"const {alias_name} = {{ {', '.join(names)} }};")
    return "\n".join(out)


_RE_EXPORTED_NAME = re.compile(
    r"^export\s+(?:const|let|var|function|class)\s+(\w+)",
    re.MULTILINE,
)


def _exported_names(text):
    """Extract every identifier exported by a module via `export const/...`."""
    return [m.group(1) for m in _RE_EXPORTED_NAME.finditer(text)]


# The vendored Preact bundle uses dozens of single-letter top-level
# variable names (e, n, t, o, h, d, v, ...). If we emit it into the
# shared module scope, our `var h = a;` alias from its terminal
# `export { a as h, ... }` would redefine `h` and break Preact's
# internal `function h(e){return e.children}` reference. So we wrap
# the Preact bundle in an IIFE that returns the named exports as an
# object, and destructure that at the call site.
def _transform_preact_bundle(text):
    text = _strip_imports(text)
    # Capture the export block, parse the rename pairs, build a return
    # statement: `return { html: fe, h: a, ... };`
    m = re.search(r"export\s*\{([^}]+)\}\s*;?", text)
    if not m:
        raise SystemExit(
            "build_liveview_html: could not find Preact bundle's export "
            "block — has the format changed since vendor?"
        )
    pairs = []  # list of (export-name, internal-name)
    for piece in m.group(1).split(","):
        piece = piece.strip()
        mm = re.match(r"^(\w+)\s+as\s+(\w+)$", piece)
        if mm:
            pairs.append((mm.group(2), mm.group(1)))
        else:
            # `export { x }` — same name in and out
            mm = re.match(r"^(\w+)$", piece)
            if mm:
                pairs.append((mm.group(1), mm.group(1)))
    return_obj = ", ".join(f"{ext}: {internal}" for ext, internal in pairs)
    body = text[:m.start()] + f"return {{ {return_obj} }};"
    iife = f"const __preact = (function () {{\n{body}\n}})();\n"
    iife += f"const {{ {', '.join(ext for ext, _ in pairs)} }} = __preact;\n"
    return iife


# ---------------------------------------------------------------------
# CSS bundling.
# ---------------------------------------------------------------------

def _bundle_css():
    parts = []
    main_css = os.path.join(PROTOTYPE_DIR, "style.css")
    fw_css   = os.path.join(LIB_DIR, "firmware", "style.css")
    for path in (main_css, fw_css):
        if os.path.exists(path):
            with open(path, "r", encoding="utf-8") as f:
                parts.append(f"/* --- {os.path.relpath(path, REPO_ROOT)} --- */")
                parts.append(f.read())
    return "\n".join(parts)


# ---------------------------------------------------------------------
# JS bundling.
# ---------------------------------------------------------------------

def _bundle_js():
    files = _all_js_files()
    ordered, by_path = _topo_sort(files)

    # Verify Preact bundle is first (sanity check — it has no imports
    # and many other files import from it, so topo sort should put it
    # first naturally).
    if PREACT_BUNDLE not in ordered:
        raise SystemExit(f"build_liveview_html: missing {PREACT_BUNDLE}")

    # Find every namespace-import target across the bundle. After each
    # exporter module's body, we emit `const X = { ... };` aliases so
    # `import * as X from './<exporter>'` references resolve.
    namespace_targets = {}  # exporter_path → set of alias names
    for path, info in by_path.items():
        for m in _RE_NAMESPACE_IMPORT.finditer(info["text"]):
            alias_name = m.group(1)
            spec = os.path.normpath(os.path.join(os.path.dirname(path), m.group(2)))
            namespace_targets.setdefault(spec, set()).add(alias_name)

    chunks = ["// ===== OnSpeed LiveView bundle ====="]
    for path in ordered:
        rel = os.path.relpath(path, REPO_ROOT)
        chunks.append(f"// === {rel} ===")
        if path == PREACT_BUNDLE:
            chunks.append(_transform_preact_bundle(by_path[path]["text"]))
        else:
            chunks.append(_transform_module(by_path[path]["text"]))
        # Emit namespace aliases immediately after this module so any
        # subsequent file that imports `* as X` from it has X in scope.
        if path in namespace_targets:
            names = _exported_names(by_path[path]["text"])
            if names:
                obj_body = ", ".join(names)
                for alias in sorted(namespace_targets[path]):
                    chunks.append(f"const {alias} = {{ {obj_body} }};")

    # Boot: call start() on DOMContentLoaded. The firmware/App.js
    # module exports a `start` function that mounts <App /> into
    # #app. After concat, `start` is just an in-scope binding.
    chunks.append("// === firmware entry point ===")
    chunks.append("if (typeof start === 'function') {")
    chunks.append("  if (document.readyState === 'loading') {")
    chunks.append("    document.addEventListener('DOMContentLoaded', start);")
    chunks.append("  } else {")
    chunks.append("    start();")
    chunks.append("  }")
    chunks.append("}")

    return "\n".join(chunks)


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
<div id="app"></div>
<script>
{js}
</script>
</body>
</html>
"""


# ---------------------------------------------------------------------
# C-header emitter.
# ---------------------------------------------------------------------

_RAW_DELIMS = ["=====", "lvw=", "lvw==", "lvw===", "deadbeef"]


def _pick_raw_delim(content):
    for d in _RAW_DELIMS:
        if f"){d}\"" not in content and f"){d})" not in content:
            return d
    raise SystemExit(
        "build_liveview_html: every preset R-string delimiter appears "
        "in the bundled content. Add a more exotic delimiter to "
        "_RAW_DELIMS."
    )


def _emit_header(html, out_path):
    delim = _pick_raw_delim(html)
    body = f"""\
// AUTO-GENERATED by scripts/build_liveview_html.py — DO NOT EDIT BY HAND.
//
// Source of truth: tools/liveview-prototype/ (Preact components, CSS,
// firmware shell). Run `python3 scripts/build_liveview_html.py` to
// regenerate. PlatformIO auto-runs the regenerator as a pre-build hook.

const char htmlIndexer[] PROGMEM = R"{delim}({html}){delim}";
"""
    with open(out_path, "w", encoding="utf-8") as f:
        f.write(body)


# ---------------------------------------------------------------------
# Skip-if-fresh.
# ---------------------------------------------------------------------

def _walk_inputs():
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
        return

    js  = _bundle_js()
    css = _bundle_css()
    html = HTML_TEMPLATE.format(css=css, js=js)

    os.makedirs(os.path.dirname(OUTPUT_HEADER), exist_ok=True)
    _emit_header(html, OUTPUT_HEADER)

    size = os.path.getsize(OUTPUT_HEADER)
    print(f"build_liveview_html: wrote {OUTPUT_HEADER} ({size:,} bytes)")
    if size > 256 * 1024:
        raise SystemExit(
            f"build_liveview_html: output is {size} bytes (>256 KB). "
            "Verify PROGMEM headroom before committing."
        )


main()
