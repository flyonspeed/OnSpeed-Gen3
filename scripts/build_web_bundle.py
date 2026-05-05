"""build_web_bundle.py — PIO pre-build hook.

Bundles the Preact app under tools/web/lib/ into three PROGMEM headers:

    software/OnSpeed-Gen3-ESP32/Web/static_app_js.h
        Gzipped JS bundle as a `PROGMEM` byte array.
        Exports `static_app_js`, `static_app_js_len`, `static_app_js_etag`.

    software/OnSpeed-Gen3-ESP32/Web/static_app_css.h
        Gzipped CSS bundle, same shape as the JS one.

    software/OnSpeed-Gen3-ESP32/Web/html_stubs.h
        Per-page HTML stubs as `R"..."` raw string literals.  One stub
        per route (currently /indexer; /live is a server-side redirect
        to /indexer).  Each stub references
        /static/app-<etag>.{js,css} where etag is the JS bundle's
        content hash.

The bundler is regex-based — same module-aware logic as the previous
build_liveview_html.py — and emits a single concatenated bundle.  It
does NOT do ES-module tree-shaking; the dedup-via-shared-bundle win
that gets us out of inline-JS-in-C++-strings is the savings worth
having (see PLAN_WEB_PREACT_REWRITE §4i).

Skip-if-fresh: regenerates only when any source file under tools/web/
or this script itself is newer than every output header.

Standalone usage:
    python3 scripts/build_web_bundle.py

PlatformIO usage (auto): registered as `pre:` extra_script in
platformio.ini's shared [env] block.
"""
import base64
import gzip
import hashlib
import os
import re
import sys

# When loaded as a PIO extra_script, SCons exec's us inside its own
# package directory, so `__file__` is unset and `sys.argv[0]` points
# at scons.  PIO provides PROJECT_DIR via `Import("env")`, which is the
# reliable repo-root handle in that context.  When the shim
# (`build_liveview_html.py`) re-exec's this module via exec(), the
# `REPO_ROOT` it resolved is already in `globals()`; we keep it.
try:
    Import("env")  # noqa: F821 — provided by SCons in PIO context
    REPO_ROOT = env["PROJECT_DIR"]  # noqa: F821
    SCRIPT_PATH = os.path.join(REPO_ROOT, "scripts", "build_web_bundle.py")
except (NameError, Exception):
    if "REPO_ROOT" not in globals():
        try:
            SCRIPT_PATH = os.path.abspath(__file__)
        except NameError:
            SCRIPT_PATH = os.path.abspath(sys.argv[0]) if sys.argv else os.getcwd()
        REPO_ROOT = os.path.dirname(os.path.dirname(SCRIPT_PATH))
    else:
        SCRIPT_PATH = os.path.join(REPO_ROOT, "scripts", "build_web_bundle.py")

WEB_DIR        = os.path.join(REPO_ROOT, "tools", "web")
LIB_DIR        = os.path.join(WEB_DIR, "lib")
PUBLIC_DIR     = os.path.join(WEB_DIR, "public")
SHELL_CSS      = os.path.join(LIB_DIR, "shell", "PageShell.css")
LOGO_PNG       = os.path.join(PUBLIC_DIR, "onspeed-logo.png")
OUTPUT_DIR     = os.path.join(REPO_ROOT, "software", "OnSpeed-Gen3-ESP32", "Web")
OUTPUT_JS_H    = os.path.join(OUTPUT_DIR, "static_app_js.h")
OUTPUT_CSS_H   = os.path.join(OUTPUT_DIR, "static_app_css.h")
OUTPUT_STUBS_H = os.path.join(OUTPUT_DIR, "html_stubs.h")
# Legacy compat: tools/liveview-prototype-style indexer header.  We
# keep the old path empty-stubbed so existing #include sites compile
# while ConfigWebServer migrates to html_stubs.h within this same PR.
LEGACY_HEADER  = os.path.join(OUTPUT_DIR, "html_indexer.h")

# Files we DON'T bundle into the firmware shared bundle:
#   - lib/scenarios.js          — synthetic scenarios (offline harness only)
#   - lib/scenarios-main.js     — entry for the offline harness page
EXCLUDE_BUNDLE = {
    "lib/scenarios.js",
    "lib/scenarios-main.js",
}

# The vendored Preact bundle: emitted FIRST, IIFE-wrapped (its
# single-letter top-level names would collide with our exports
# otherwise).
PREACT_BUNDLE = os.path.join(LIB_DIR, "vendor", "preact-standalone.js")

# Entry module: the bundler appends a `start()` boot stanza referencing
# this file's exports.
ENTRY_MODULE  = os.path.join(LIB_DIR, "entry.js")

# Pages: list of `(page-id, html-title, stub-name-suffix)`.  Stubs land
# in html_stubs.h as `htmlStub_<suffix>`.  The stub references the
# `data-page="<page-id>"` attribute the bundle's entry.js reads.
PAGES = [
    ("indexer", "Indexer",          "indexer"),
    ("calwiz",  "Calibration",      "calwiz"),
    ("home",    "Home",             "home"),
    ("reboot",  "Reboot",           "reboot"),
    ("format",  "Format SD",        "format"),
    ("upgrade", "Firmware Upgrade", "upgrade"),
    ("logs",    "Logs",             "logs"),
    # /sensorconfig is deferred — its read view needs a sensors-snapshot
    # endpoint (raw IMU pitch/roll, current biases, EFIS readings) that
    # doesn't exist yet.  Lands in PR 5 once /api/sensors is added.
]


# ---------------------------------------------------------------------
# Source enumeration + topological sort.
# ---------------------------------------------------------------------

def _all_js_files():
    """Every JS file we want to bundle."""
    out = []
    for root, _dirs, files in os.walk(LIB_DIR):
        # Sort dirnames+files for deterministic walk order.
        files = sorted(files)
        for name in files:
            if not name.endswith(".js"):
                continue
            rel = os.path.relpath(os.path.join(root, name), LIB_DIR)
            rel_unix = rel.replace(os.sep, "/")
            if rel_unix in {"scenarios.js", "scenarios-main.js"}:
                continue
            out.append(os.path.join(root, name))
    return sorted(out)


_RE_IMPORT = re.compile(
    r"^import\b[^;]*?from\s*['\"]([^'\"]+)['\"]\s*;?\s*$",
    re.MULTILINE | re.DOTALL,
)

_RE_NAMESPACE_IMPORT = re.compile(
    r"^import\s*\*\s*as\s*(\w+)\s*from\s*['\"]([^'\"]+)['\"]\s*;?\s*$",
    re.MULTILINE,
)

_RE_EXPORTED_NAME = re.compile(
    r"^export\s+(?:const|let|var|function|class)\s+(\w+)",
    re.MULTILINE,
)


def _imports_in(text):
    return [m.group(1) for m in _RE_IMPORT.finditer(text) if m.group(1)]


def _topo_sort(files):
    """Sort files so each one's imports appear earlier in the result."""
    by_path = {}
    for path in files:
        with open(path, "r", encoding="utf-8") as f:
            text = f.read()
        deps = []
        for spec in _imports_in(text):
            base = os.path.dirname(path)
            resolved = os.path.normpath(os.path.join(base, spec))
            if resolved in set(files):
                deps.append(resolved)
        by_path[path] = {"text": text, "deps": deps}

    visited, visiting, ordered = set(), set(), []

    def visit(p):
        if p in visited:
            return
        if p in visiting:
            raise SystemExit(f"build_web_bundle: import cycle through {p}")
        visiting.add(p)
        for dep in by_path[p]["deps"]:
            if dep in by_path:
                visit(dep)
        visiting.discard(p)
        visited.add(p)
        ordered.append(p)

    for p in sorted(files):
        visit(p)

    return ordered, by_path


# ---------------------------------------------------------------------
# Per-file transformations.
# ---------------------------------------------------------------------

def _strip_imports(text):
    return _RE_IMPORT.sub("", text)


def _assert_supported_forms(text, src_path):
    if re.search(r"^export\s+default\b", text, re.MULTILINE):
        raise SystemExit(
            f"build_web_bundle: {src_path}: `export default` not supported."
        )
    if re.search(r"^import\s+['\"]", text, re.MULTILINE):
        raise SystemExit(
            f"build_web_bundle: {src_path}: side-effect import not supported."
        )
    if re.search(r"^export\s+async\b", text, re.MULTILINE):
        raise SystemExit(
            f"build_web_bundle: {src_path}: `export async function` not "
            f"supported. Declare async then export separately."
        )
    for m in re.finditer(r"^export\s+(?:const|let|var)\s+(.+?);", text,
                         re.MULTILINE | re.DOTALL):
        decl = m.group(1)
        stripped = re.sub(r"'[^']*'|\"[^\"]*\"|`[^`]*`", "''", decl)
        depth = 0
        for ch in stripped:
            if ch in "([{": depth += 1
            elif ch in ")]}": depth -= 1
            elif ch == "," and depth == 0:
                raise SystemExit(
                    f"build_web_bundle: {src_path}: multi-binding "
                    f"`export const A, B;` not supported."
                )
    if re.search(r"^export\s+(?:const|let|var)\s*[\[\{]", text, re.MULTILINE):
        raise SystemExit(
            f"build_web_bundle: {src_path}: destructuring export not supported."
        )


def _strip_exports(text):
    """Drop the `export` keyword from declarations."""
    text = re.sub(r"^export\s+(const|let|var|function|class)\s+",
                  r"\1 ", text, flags=re.MULTILINE)

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
        return "\n" + "\n".join(out_lines)

    text = re.sub(r"export\s*\{([^}]+)\}\s*;?",
                  _rewrite_export_block, text)
    return text


def _transform_module(text, src_path):
    _assert_supported_forms(text, src_path)
    return _strip_exports(_strip_imports(text))


def _exported_names(text):
    return [m.group(1) for m in _RE_EXPORTED_NAME.finditer(text)]


def _transform_preact_bundle(text):
    """Wrap the vendored Preact bundle in an IIFE that returns its
    named exports as an object so its single-letter internals don't
    leak into the shared bundle scope."""
    text = _strip_imports(text)
    m = re.search(r"export\s*\{([^}]+)\}\s*;?", text)
    if not m:
        raise SystemExit(
            "build_web_bundle: could not find Preact bundle's export block — "
            "has the format changed since vendor?"
        )
    trailing = text[m.end():].strip()
    if trailing:
        raise SystemExit(
            f"build_web_bundle: Preact bundle has unexpected trailing content "
            f"after export block (first 80 chars: {trailing[:80]!r})."
        )
    pairs = []
    for piece in m.group(1).split(","):
        piece = piece.strip()
        mm = re.match(r"^(\w+)\s+as\s+(\w+)$", piece)
        if mm:
            pairs.append((mm.group(2), mm.group(1)))
        else:
            mm = re.match(r"^(\w+)$", piece)
            if mm:
                pairs.append((mm.group(1), mm.group(1)))
    return_obj = ", ".join(f"{ext}: {internal}" for ext, internal in pairs)
    body = text[:m.start()] + f"return {{ {return_obj} }};"
    iife = f"const __preact = (function () {{\n{body}\n}})();\n"
    iife += f"const {{ {', '.join(ext for ext, _ in pairs)} }} = __preact;\n"
    return iife


# ---------------------------------------------------------------------
# JS bundling.
# ---------------------------------------------------------------------

def _logo_data_url():
    """Read the OnSpeed PNG logo from public/ and return a data: URL.
    Embedded once in the JS bundle as `globalThis.ONSPEED_LOGO_DATA_URL`
    so PageShell can reference it without per-stub duplication."""
    if not os.path.exists(LOGO_PNG):
        raise SystemExit(
            f"build_web_bundle: missing logo source {LOGO_PNG} — "
            f"extract it from Web/html_header.h before running the bundler."
        )
    with open(LOGO_PNG, "rb") as f:
        png = f.read()
    return "data:image/png;base64," + base64.b64encode(png).decode("ascii")


def _bundle_js():
    files = _all_js_files()
    if PREACT_BUNDLE not in files:
        raise SystemExit(f"build_web_bundle: missing {PREACT_BUNDLE}")
    if ENTRY_MODULE not in files:
        raise SystemExit(f"build_web_bundle: missing {ENTRY_MODULE}")

    ordered, by_path = _topo_sort(files)

    # Find every namespace-import target across the bundle.  After
    # each exporter module's body, emit `const X = { ... };` aliases
    # so `import * as X from './<exporter>'` references resolve.
    namespace_targets = {}
    for path, info in by_path.items():
        for m in _RE_NAMESPACE_IMPORT.finditer(info["text"]):
            alias_name = m.group(1)
            spec = os.path.normpath(os.path.join(os.path.dirname(path), m.group(2)))
            namespace_targets.setdefault(spec, set()).add(alias_name)

    chunks = ["// ===== OnSpeed web app bundle ====="]
    # Inline the OnSpeed PNG logo as a data URL the chrome can read at
    # render time.  Embedded once here, referenced by PageShell, rather
    # than duplicated per page stub.
    logo_url = _logo_data_url()
    chunks.append("// === OnSpeed logo (from tools/web/public/onspeed-logo.png) ===")
    # Data URLs are pure base64 + ASCII, so `'<url>'` is always safe.
    chunks.append(f"globalThis.ONSPEED_LOGO_DATA_URL = '{logo_url}';")
    for path in ordered:
        rel = os.path.relpath(path, REPO_ROOT)
        chunks.append(f"// === {rel} ===")
        if path == PREACT_BUNDLE:
            chunks.append(_transform_preact_bundle(by_path[path]["text"]))
        else:
            chunks.append(_transform_module(by_path[path]["text"],
                                            os.path.relpath(path, REPO_ROOT)))
        if path in namespace_targets:
            names = _exported_names(by_path[path]["text"])
            if names:
                obj_body = ", ".join(names)
                for alias in sorted(namespace_targets[path]):
                    chunks.append(f"const {alias} = {{ {obj_body} }};")

    chunks.append("// === bundle entry point ===")
    chunks.append("if (typeof start === 'function') {")
    chunks.append("  if (document.readyState === 'loading') {")
    chunks.append("    document.addEventListener('DOMContentLoaded', start);")
    chunks.append("  } else {")
    chunks.append("    start();")
    chunks.append("  }")
    chunks.append("}")

    return "\n".join(chunks)


# ---------------------------------------------------------------------
# CSS bundling.
# ---------------------------------------------------------------------

def _bundle_css():
    parts = []
    if os.path.exists(SHELL_CSS):
        with open(SHELL_CSS, "r", encoding="utf-8") as f:
            parts.append(f"/* --- {os.path.relpath(SHELL_CSS, REPO_ROOT)} --- */")
            parts.append(f.read())
    # Vendor CSS (chartist, etc.) — every .css file under lib/vendor/
    # is concatenated into the shared bundle.  Chartist is the only
    # current consumer (used by the cal wizard's review chart).
    vendor_css_dir = os.path.join(LIB_DIR, "vendor")
    if os.path.isdir(vendor_css_dir):
        for name in sorted(os.listdir(vendor_css_dir)):
            if not name.endswith(".css"):
                continue
            path = os.path.join(vendor_css_dir, name)
            with open(path, "r", encoding="utf-8") as f:
                parts.append(f"/* --- {os.path.relpath(path, REPO_ROOT)} --- */")
                parts.append(f.read())
    return "\n".join(parts)


# ---------------------------------------------------------------------
# C-header emission.
# ---------------------------------------------------------------------

def _gzip_deterministic(data_bytes):
    """gzip with mtime=0 so output is reproducible across runs."""
    import io
    buf = io.BytesIO()
    with gzip.GzipFile(fileobj=buf, mode="wb", mtime=0,
                       compresslevel=9) as gz:
        gz.write(data_bytes)
    return buf.getvalue()


def _format_byte_array(name, data, width=16):
    """Emit `static const uint8_t name[] PROGMEM = { 0x.., 0x.., ... };`."""
    lines = []
    lines.append(f"static const uint8_t {name}[] PROGMEM = {{")
    for i in range(0, len(data), width):
        row = ", ".join(f"0x{b:02x}" for b in data[i:i+width])
        lines.append(f"    {row},")
    lines.append("};")
    return "\n".join(lines)


def _emit_static_header(out_path, var_prefix, content_text, content_type):
    raw_bytes = content_text.encode("utf-8")
    etag = hashlib.sha256(raw_bytes).hexdigest()[:12]
    gz = _gzip_deterministic(raw_bytes)
    body = []
    body.append("// AUTO-GENERATED by scripts/build_web_bundle.py — DO NOT EDIT.")
    body.append("//")
    body.append("// Source: tools/web/lib/.  Run `python3 scripts/build_web_bundle.py`")
    body.append("// to regenerate.  PlatformIO auto-runs as a pre-build hook.")
    body.append("")
    body.append("#pragma once")
    body.append("#include <pgmspace.h>")
    body.append("")
    body.append(f"static const size_t {var_prefix}_len = {len(gz)};")
    body.append(f'static const char   {var_prefix}_etag[] = "{etag}";')
    body.append(f'static const char   {var_prefix}_content_type[] = "{content_type}";')
    body.append("")
    body.append(_format_byte_array(var_prefix, gz))
    with open(out_path, "w", encoding="utf-8") as f:
        f.write("\n".join(body) + "\n")
    return etag


def _emit_stubs_header(out_path, etag_js, etag_css):
    """Write html_stubs.h with one R-string-literal stub per page."""
    body = []
    body.append("// AUTO-GENERATED by scripts/build_web_bundle.py — DO NOT EDIT.")
    body.append("//")
    body.append("// Per-page HTML stubs for Preact-served pages.  Each stub is a")
    body.append("// complete document; ConfigWebServer's ServePageStub() handler")
    body.append("// sends it as-is.  The shared JS / CSS bundles live at")
    body.append("// /static/app-<etag>.{js,css} with immutable cache.")
    body.append("")
    body.append("#pragma once")
    body.append("#include <pgmspace.h>")
    body.append("")
    delim = _pick_raw_delim_global()
    for page_id, title, suffix in PAGES:
        stub = _build_stub_html(page_id, title, etag_js, etag_css)
        if f"){delim}\"" in stub or f"){delim})" in stub:
            raise SystemExit(
                f"build_web_bundle: stub for {page_id} contains delimiter "
                f"'){delim}'; bump _RAW_DELIMS."
            )
        body.append(
            f"static const char htmlStub_{suffix}[] PROGMEM = "
            f"R\"{delim}({stub}){delim}\";"
        )
        body.append("")
    with open(out_path, "w", encoding="utf-8") as f:
        f.write("\n".join(body) + "\n")


# A small set of static fallback nav links for users with JS disabled.
# `<noscript>`-friendly; replaced by PageShell once the bundle mounts.
_STATIC_FALLBACK_NAV = (
    '<noscript>OnSpeed configuration requires JavaScript. '
    'Use one of the links below to navigate.</noscript>'
    '<nav class="onspeed-fallback-nav" aria-label="fallback">'
    '<a href="/">Home</a> | '
    '<a href="/indexer">Indexer</a> | '
    '<a href="/aoaconfig">Config</a> | '
    '<a href="/calwiz">Calibration</a> | '
    '<a href="/logs">Logs</a> | '
    '<a href="/upgrade">Upgrade</a> | '
    '<a href="/reboot">Reboot</a>'
    '</nav>'
)


def _build_stub_html(page_id, title, etag_js, etag_css):
    return (
        '<!DOCTYPE html>\n'
        '<html lang="en">\n'
        '<head>\n'
        '<meta charset="utf-8">\n'
        '<meta name="viewport" content="width=device-width, initial-scale=1">\n'
        f'<title>OnSpeed — {title}</title>\n'
        f'<link rel="stylesheet" href="/static/app-{etag_css}.css">\n'
        '</head>\n'
        '<body>\n'
        f'<div id="app" data-page="{page_id}">'
        f'{_STATIC_FALLBACK_NAV}'
        '</div>\n'
        f'<script src="/static/app-{etag_js}.js" defer></script>\n'
        '</body>\n'
        '</html>'
    )


_RAW_DELIMS = ["=====", "stub=", "stub==", "deadbeef"]


def _pick_raw_delim_global():
    # Stubs are tiny and contain no R-string artifacts in practice.
    return _RAW_DELIMS[0]


def _emit_legacy_indexer_shim(out_path, etag_js, etag_css):
    """Keep html_indexer.h around but reduced to a thin alias of the
    new htmlStub_indexer.  Source files still including it compile;
    the new ServePageStub path is what actually serves the page."""
    stub = _build_stub_html("indexer", "Indexer", etag_js, etag_css)
    delim = _pick_raw_delim_global()
    body = (
        "// AUTO-GENERATED by scripts/build_web_bundle.py — DO NOT EDIT.\n"
        "//\n"
        "// Legacy include path.  The page stub now lives in html_stubs.h\n"
        "// as htmlStub_indexer; this header is preserved as a thin\n"
        "// definition so any stale `#include \"Web/html_indexer.h\"` keeps\n"
        "// compiling during the migration.  Delete after PR 1 has\n"
        "// settled.\n"
        "\n"
        "#pragma once\n"
        "#include <pgmspace.h>\n"
        "\n"
        f"static const char htmlIndexer[] PROGMEM = R\"{delim}({stub}){delim}\";\n"
    )
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
    if os.path.exists(SHELL_CSS):
        yield SHELL_CSS
    if os.path.exists(LOGO_PNG):
        yield LOGO_PNG


def _needs_rebuild():
    outputs = [OUTPUT_JS_H, OUTPUT_CSS_H, OUTPUT_STUBS_H, LEGACY_HEADER]
    if not all(os.path.exists(p) for p in outputs):
        return True
    out_mtime = min(os.path.getmtime(p) for p in outputs)
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

    os.makedirs(OUTPUT_DIR, exist_ok=True)
    etag_js  = _emit_static_header(OUTPUT_JS_H,  "static_app_js",
                                   js,  "application/javascript")
    etag_css = _emit_static_header(OUTPUT_CSS_H, "static_app_css",
                                   css, "text/css")
    _emit_stubs_header(OUTPUT_STUBS_H, etag_js, etag_css)
    _emit_legacy_indexer_shim(LEGACY_HEADER, etag_js, etag_css)

    js_size  = os.path.getsize(OUTPUT_JS_H)
    css_size = os.path.getsize(OUTPUT_CSS_H)
    stubs_sz = os.path.getsize(OUTPUT_STUBS_H)
    print(f"build_web_bundle: js={js_size:,} css={css_size:,} stubs={stubs_sz:,}")

    PROGMEM_BUDGET = 512 * 1024
    if js_size + css_size + stubs_sz > PROGMEM_BUDGET:
        raise SystemExit(
            f"build_web_bundle: outputs exceed {PROGMEM_BUDGET // 1024} KB "
            f"PROGMEM budget — review before committing."
        )


main()
