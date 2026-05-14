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
LEGACY_DIR     = os.path.join(WEB_DIR, "legacy-pages")

# Shared UI library — used by tools/web/ and docs/site/ alike. See
# packages/ui-core/README.md and issue #523 for the design principle.
UI_CORE_DIR    = os.path.join(REPO_ROOT, "packages", "ui-core")

SHELL_CSS      = os.path.join(LIB_DIR, "shell", "PageShell.css")
LEGACY_FORMS_CSS = os.path.join(LIB_DIR, "shell", "legacy-forms.css")
LOGO_PNG       = os.path.join(PUBLIC_DIR, "onspeed-logo.png")
OUTPUT_DIR     = os.path.join(REPO_ROOT, "software", "OnSpeed-Gen3-ESP32", "Web")
OUTPUT_JS_H    = os.path.join(OUTPUT_DIR, "static_app_js.h")
OUTPUT_CSS_H   = os.path.join(OUTPUT_DIR, "static_app_css.h")
OUTPUT_STUBS_H = os.path.join(OUTPUT_DIR, "html_stubs.h")
# Legacy /aoaconfig template + its inline JS, sourced from real files
# under tools/web/legacy-pages/.  HandleConfig in ConfigWebServer.cpp
# loads the template from this header and runs Mustache-style {{name}}
# substitutions before serving.
OUTPUT_LEGACY_H   = os.path.join(OUTPUT_DIR, "legacy_pages.h")
OUTPUT_LEGACY_CPP = os.path.join(OUTPUT_DIR, "legacy_pages.cpp")

# Files we DON'T bundle into the firmware shared bundle:
#   - lib/scenarios.js          — synthetic scenarios (offline harness only)
#   - lib/scenarios-main.js     — entry for the offline harness page
EXCLUDE_BUNDLE = {
    "lib/scenarios.js",
    "lib/scenarios-main.js",
}

# Directories under the source trees that the bundler must not walk:
#   - tools/web/lib/replay/                       — left over from
#     pre-PR-#512 replay tool. Build artifacts (M5 WASM) land here when
#     the M5 build script runs locally; they're .gitignore'd but still
#     on disk, and we don't want to pull them into the firmware PROGMEM
#     bundle.
EXCLUDE_DIRS = {
    "tools/web/lib/replay",
}

# The vendored Preact bundle: emitted FIRST, IIFE-wrapped (its
# single-letter top-level names would collide with our exports
# otherwise).
PREACT_BUNDLE = os.path.join(UI_CORE_DIR, "vendor", "preact-standalone.js")

# Entry module: the bundler appends a `start()` boot stanza referencing
# this file's exports.
ENTRY_MODULE  = os.path.join(LIB_DIR, "entry.js")

# Pages: list of `(page-id, html-title, stub-name-suffix)`.  Stubs land
# in html_stubs.h as `htmlStub_<suffix>`.  The stub references the
# `data-page="<page-id>"` attribute the bundle's entry.js reads.
PAGES = [
    ("indexer",      "Indexer",            "indexer"),
    ("calwiz",       "Calibration",        "calwiz"),
    ("home",         "Home",               "home"),
    ("reboot",       "Reboot",             "reboot"),
    ("format",       "Format SD",          "format"),
    ("upgrade",      "Firmware Upgrade",   "upgrade"),
    ("logs",         "Logs",               "logs"),
    ("sensorconfig", "Sensor Calibration", "sensorconfig"),
]

# Replay-bundle target: handled by scripts/build_replay.mjs (esbuild).
# This module's --target replay dispatcher shells out to that script.


# ---------------------------------------------------------------------
# Source enumeration + topological sort.
# ---------------------------------------------------------------------

def _all_js_files():
    """Every JS file we want to bundle.

    Walks both `tools/web/lib/` (firmware-specific consumers — pages,
    shell, ws client) and `packages/ui-core/` (shared UI library —
    components, geometry, helpers). Files from both trees flow through
    the same dedup-+-topo-sort pipeline; the bundler doesn't care
    which directory they came from.
    """
    out = []
    excluded_abs = {os.path.join(REPO_ROOT, p) for p in EXCLUDE_DIRS}
    for source_dir in (LIB_DIR, UI_CORE_DIR):
        if not os.path.isdir(source_dir):
            continue
        for root, dirs, files in os.walk(source_dir):
            # Prune excluded directories in-place so os.walk doesn't descend.
            dirs[:] = sorted(d for d in dirs
                             if os.path.join(root, d) not in excluded_abs)
            files = sorted(files)
            for name in files:
                if not name.endswith(".js"):
                    continue
                rel = os.path.relpath(os.path.join(root, name), source_dir)
                rel_unix = rel.replace(os.sep, "/")
                if source_dir == LIB_DIR and rel_unix in {"scenarios.js", "scenarios-main.js"}:
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
    r"^export\s+(?:async\s+)?(?:const|let|var|function|class)\s+(\w+)",
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
    text = re.sub(r"^export\s+((?:async\s+)?function|const|let|var|class)\s+",
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


def _transform_module(text, src_path, ns_rename=None):
    _assert_supported_forms(text, src_path)
    text = _strip_exports(_strip_imports(text))
    if ns_rename:
        text = _apply_ns_rename(text, ns_rename, src_path)
    return text


def _unique_ns_name(spec):
    """Build a unique top-level identifier for a namespace-import target.

    `spec` is the resolved absolute path of the exporter module. The
    identifier is `__NS_` + path relative to REPO_ROOT with non-word
    characters squashed to underscores and a `.js` suffix dropped. The
    bundler emits `const <unique> = { ... };` per exporter and
    rewrites `<alias>.<member>` references in each consumer to
    `<unique>.<member>`. Path-derived names avoid collisions with
    vendored bundles (e.g. mediabunny declares a top-level `G`).
    """
    try:
        rel = os.path.relpath(spec, REPO_ROOT)
    except ValueError:
        rel = spec
    if rel.endswith(".js"):
        rel = rel[:-3]
    sanitized = re.sub(r"[^A-Za-z0-9_]", "_", rel)
    return "__NS_" + sanitized


def _apply_ns_rename(text, ns_rename, rel_path=""):
    """Rewrite `<alias>.<member>` references to use the path-derived
    unique name. Only matches alias usage as a property accessor
    (followed by `.`), so identical letters appearing inside string
    literals (e.g. `label="G"`), JSX text content, or other unrelated
    identifiers are not corrupted. Bare-alias references (e.g. passing
    the entire namespace object as a value) are not supported by this
    bundler; if a consumer needs that, refactor to a named import.

    Corruption-detection is done at the bundle level via
    `_assert_no_namespace_token_in_string_literals` (called once on
    the final concatenated output). Source-level scanning is brittle
    because JS comments + strings interact in ways a simple regex
    can't tokenize without becoming a parser.
    """
    for alias, unique in ns_rename.items():
        if alias == unique:
            continue
        pat = re.compile(r"\b" + re.escape(alias) + r"(?=\.)")
        text = pat.sub(unique, text)
    return text


# Conservative post-bundle assertion: the `__NS_` prefix is generated
# only by our rename pass, so it should never appear immediately
# inside a quote character in the output. A `"__NS_…"` or `'__NS_…'`
# substring means the rename pass slipped into a string literal —
# either because the alias appeared as `"G.foo"` literal text or
# because the source-format detection is broken. Either way, this
# assertion catches it before the bundle ships.
_RE_NS_IN_QUOTED = re.compile(r"""(['"])(?P<body>__NS_[A-Za-z0-9_]+)""")


def _assert_no_namespace_token_in_string_literals(bundled_js):
    m = _RE_NS_IN_QUOTED.search(bundled_js)
    if not m:
        return
    # Show ~80 chars of context so the failure points at the offending site.
    start = max(0, m.start() - 40)
    end   = min(len(bundled_js), m.end() + 40)
    snippet = bundled_js[start:end]
    raise SystemExit(
        f"build_web_bundle: namespace-rename token {m.group('body')!r} "
        f"appears immediately after a quote character — the rename pass "
        f"corrupted a string literal. Context: …{snippet!r}…"
    )


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

    # Find every namespace-import target across the bundle. After each
    # exporter module's body, emit `const __NS_<path> = { ... };` and
    # rewrite consumer references from `<alias>.<member>` to
    # `__NS_<path>.<member>`. Path-derived unique names avoid
    # collisions with vendored top-level declarations (e.g. minified
    # bundles that happen to use a short identifier as the alias).
    namespace_targets = {}  # exporter_path -> set(unique_names)
    module_ns_rename = {}   # consumer_path -> {alias: unique}
    for path, info in by_path.items():
        for m in _RE_NAMESPACE_IMPORT.finditer(info["text"]):
            alias_name = m.group(1)
            spec = os.path.normpath(os.path.join(os.path.dirname(path), m.group(2)))
            unique = _unique_ns_name(spec)
            namespace_targets.setdefault(spec, set()).add(unique)
            module_ns_rename.setdefault(path, {})[alias_name] = unique

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
                                            os.path.relpath(path, REPO_ROOT),
                                            ns_rename=module_ns_rename.get(path)))
        if path in namespace_targets:
            names = _exported_names(by_path[path]["text"])
            if names:
                obj_body = ", ".join(names)
                for unique in sorted(namespace_targets[path]):
                    chunks.append(f"const {unique} = {{ {obj_body} }};")

    chunks.append("// === bundle entry point ===")
    chunks.append("if (typeof start === 'function') {")
    chunks.append("  if (document.readyState === 'loading') {")
    chunks.append("    document.addEventListener('DOMContentLoaded', start);")
    chunks.append("  } else {")
    chunks.append("    start();")
    chunks.append("  }")
    chunks.append("}")

    bundled = "\n".join(chunks)
    _assert_no_duplicate_top_level_identifiers(bundled)
    _assert_no_namespace_token_in_string_literals(bundled)
    return bundled


_RE_TOP_LEVEL_DECL = re.compile(
    r"^(?:const|let|class)\s+([A-Za-z_$][A-Za-z0-9_$]*)",
    re.MULTILINE,
)


def _assert_no_duplicate_top_level_identifiers(bundled_js):
    """Fail loudly if two modules declare the same top-level lexical
    identifier (`const`, `let`, `class`).

    The bundler concatenates every JS module into a single script tag.
    Module-scope `const X = …` declarations from two different files
    end up in the same lexical scope and the browser raises
    `Uncaught SyntaxError: Identifier 'X' has already been declared`.
    The page never mounts.

    `function` and `var` declarations are intentionally excluded: both
    are redeclarable at the same scope, so duplicates parse and
    execute (the last one wins). The bundler relies on this for
    locally-scoped helpers (`safeLsGet`, `formatHms`, etc.) that
    happen to share names across modules without sharing identity.

    Catch it here, at build time, with a clear message naming the
    duplicates. The fix is to rename one (or factor the constant into
    a shared helper module).
    """
    counts = {}
    for match in _RE_TOP_LEVEL_DECL.finditer(bundled_js):
        name = match.group(1)
        counts[name] = counts.get(name, 0) + 1
    duplicates = sorted((n, c) for n, c in counts.items() if c > 1)
    if duplicates:
        msg_lines = [
            "build_web_bundle: duplicate top-level identifier(s) in JS bundle:",
        ]
        for name, count in duplicates:
            msg_lines.append(f"  {count}x  {name}")
        msg_lines.append(
            "Rename one of the colliding declarations or move it into a"
            " shared helper module under lib/core/."
        )
        raise SystemExit("\n".join(msg_lines))


# ---------------------------------------------------------------------
# CSS bundling.
# ---------------------------------------------------------------------

def _bundle_css():
    parts = []
    if os.path.exists(SHELL_CSS):
        with open(SHELL_CSS, "r", encoding="utf-8") as f:
            parts.append(f"/* --- {os.path.relpath(SHELL_CSS, REPO_ROOT)} --- */")
            parts.append(f.read())
    # Form / button / layout vocabulary used by the Preact pages and
    # the still-server-rendered /aoaconfig template.  Sourced from the
    # legacy /css/main.css.  Lives in its own file so the chrome-only
    # rules in PageShell.css stay separated from the form-vocabulary
    # rules.
    if os.path.exists(LEGACY_FORMS_CSS):
        with open(LEGACY_FORMS_CSS, "r", encoding="utf-8") as f:
            parts.append(f"/* --- {os.path.relpath(LEGACY_FORMS_CSS, REPO_ROOT)} --- */")
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
    body.append("// Source: tools/web/lib/ and packages/ui-core/.  Run `python3 scripts/build_web_bundle.py`")
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


# Static fallback nav for users with JS disabled.  Wrapped in
# `<noscript>` so the entire block is invisible to browsers with JS on
# (per HTML spec, `<noscript>` content is not rendered when scripting is
# enabled), and lives as a sibling of `<div id="app">` rather than a
# child so PageShell's mount can never inherit it.
_STATIC_FALLBACK_NAV = (
    '<noscript>'
    '<p>OnSpeed configuration requires JavaScript. '
    'Use one of the links below to navigate.</p>'
    '<nav class="onspeed-fallback-nav" aria-label="fallback">'
    '<a href="/">Home</a> | '
    '<a href="/indexer">Indexer</a> | '
    '<a href="/aoaconfig">Config</a> | '
    '<a href="/calwiz">Calibration</a> | '
    '<a href="/logs">Logs</a> | '
    '<a href="/upgrade">Upgrade</a> | '
    '<a href="/reboot">Reboot</a>'
    '</nav>'
    '</noscript>'
)


def _build_stub_html(page_id, title, etag_js, etag_css):
    # `{{onspeedVersion}}` is substituted at request time — by the
    # firmware's ServePageStub (using BuildInfo::version) and by the
    # dev-server (literal "dev").  PageShell reads the meta tag at
    # first paint so the version banner has no post-mount flash.
    return (
        '<!DOCTYPE html>\n'
        '<html lang="en">\n'
        '<head>\n'
        '<meta charset="utf-8">\n'
        '<meta name="viewport" content="width=device-width, initial-scale=1">\n'
        '<meta name="onspeed-version" content="{{onspeedVersion}}">\n'
        f'<title>OnSpeed — {title}</title>\n'
        # Inline critical CSS: paint the body bg + base text color before
        # the bundled stylesheet downloads.  The deferred <script> tag
        # holds the JS bundle until parse finishes; without this the
        # browser flashes white between HTML parse and stylesheet load.
        '<style>body{margin:0;background:#cccccc;color:#000088;'
        'font-family:Arial,Helvetica,Sans-Serif}</style>\n'
        f'<link rel="stylesheet" href="/static/app-{etag_css}.css">\n'
        '</head>\n'
        '<body>\n'
        f'{_STATIC_FALLBACK_NAV}'
        f'<div id="app" data-page="{page_id}"></div>\n'
        f'<script src="/static/app-{etag_js}.js" defer></script>\n'
        '</body>\n'
        '</html>'
    )


_RAW_DELIMS = ["=====", "stub=", "stub==", "deadbeef"]


def _pick_raw_delim_global():
    # Stubs are tiny and contain no R-string artifacts in practice.
    return _RAW_DELIMS[0]


def _pick_raw_delim_for(text):
    """Pick a delimiter that doesn't collide with `)<delim>"` in text."""
    for d in _RAW_DELIMS:
        marker_dq = f"){d}\""
        marker_other = f"){d})"
        if marker_dq not in text and marker_other not in text:
            return d
    raise SystemExit(
        "build_web_bundle: every candidate raw-string delimiter collides "
        "with text content; extend _RAW_DELIMS."
    )


def _emit_legacy_pages_header(header_path, cpp_path):
    """Emit the PROGMEM blobs for tools/web/legacy-pages/*.

    The .h declares the symbols (`extern const char ... PROGMEM;`); the
    paired .cpp owns the definitions.  Splitting like this prevents
    cross-translation-unit duplication of the ~50 KB blobs if another
    file ever picks up `#include "Web/legacy_pages.h"` in addition to
    ConfigWebServer.cpp.  Templates keep their `{{name}}` markers;
    substitution happens at request time in C++ (see
    ConfigWebServer.cpp::HandleConfig)."""
    # (var-name, source filename, html-vs-other-suffix).  The html
    # symbols get the `_html` suffix the firmware already references.
    pages = [
        ("aoaconfig_html",     "aoaconfig.html"),
        ("aoaconfig_js",       "aoaconfig.js"),
        ("aoaconfig_post_js",  "aoaconfig-post.js"),
    ]

    h_body = [
        "// AUTO-GENERATED by scripts/build_web_bundle.py — DO NOT EDIT.",
        "//",
        "// Source: tools/web/legacy-pages/.  Each template keeps its",
        "// {{name}} markers; ConfigWebServer.cpp substitutes them per",
        "// request before sending the page.",
        "",
        "// The .h declares; the paired legacy_pages.cpp defines.  This",
        "// keeps the ~50 KB PROGMEM blobs from duplicating across",
        "// translation units if another source ever pulls this header",
        "// in alongside ConfigWebServer.cpp.",
        "",
        "#pragma once",
        "#include <pgmspace.h>",
        "",
    ]

    cpp_body = [
        "// AUTO-GENERATED by scripts/build_web_bundle.py — DO NOT EDIT.",
        "//",
        "// Definitions for the legacy /aoaconfig template + its inline",
        "// JS bundles.  Declared by Web/legacy_pages.h.",
        "",
        "#include \"Web/legacy_pages.h\"",
        "",
    ]

    for var, fname in pages:
        path = os.path.join(LEGACY_DIR, fname)
        if not os.path.exists(path):
            raise SystemExit(f"build_web_bundle: missing legacy asset {path}")
        with open(path, "r", encoding="utf-8") as f:
            text = f.read()
        delim = _pick_raw_delim_for(text)
        h_body.append(f"extern const char legacy_{var}[] PROGMEM;")
        cpp_body.append(
            f"const char legacy_{var}[] PROGMEM = "
            f"R\"{delim}({text}){delim}\";"
        )
        cpp_body.append("")

    h_body.append("")

    with open(header_path, "w", encoding="utf-8") as f:
        f.write("\n".join(h_body) + "\n")
    with open(cpp_path, "w", encoding="utf-8") as f:
        f.write("\n".join(cpp_body) + "\n")


# ---------------------------------------------------------------------
# Skip-if-fresh.
# ---------------------------------------------------------------------

def _walk_inputs():
    yield SCRIPT_PATH
    for root, _dirs, files in os.walk(LIB_DIR):
        for name in files:
            yield os.path.join(root, name)
    if os.path.exists(UI_CORE_DIR):
        for root, _dirs, files in os.walk(UI_CORE_DIR):
            for name in files:
                yield os.path.join(root, name)
    if os.path.exists(LEGACY_DIR):
        for root, _dirs, files in os.walk(LEGACY_DIR):
            for name in files:
                yield os.path.join(root, name)
    if os.path.exists(SHELL_CSS):
        yield SHELL_CSS
    if os.path.exists(LEGACY_FORMS_CSS):
        yield LEGACY_FORMS_CSS
    if os.path.exists(LOGO_PNG):
        yield LOGO_PNG


def _needs_rebuild():
    outputs = [OUTPUT_JS_H, OUTPUT_CSS_H, OUTPUT_STUBS_H, OUTPUT_LEGACY_H,
               OUTPUT_LEGACY_CPP]
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
# Replay-bundle target.
#
# The replay target now builds via esbuild (scripts/build_replay.mjs).
# This Python entry point just shells out to Node — it stays in the
# `--target replay` dispatcher so PlatformIO / CI / docs-build hooks
# keep their existing invocation contracts. The firmware target keeps
# the regex-based Python bundler (PROGMEM C-header emission is the
# bit esbuild can't produce).
#
# See scripts/build_replay.mjs for the esbuild config and issue #547
# for the swap rationale.
# ---------------------------------------------------------------------


def _run_replay_via_esbuild():
    import subprocess
    script = os.path.join(REPO_ROOT, "scripts", "build_replay.mjs")
    if not os.path.exists(script):
        raise SystemExit(
            f"build_web_bundle: missing {script}; checkout incomplete?")
    # Resolve node from PATH; let it fail with the OS's PATH error if
    # absent. CI sets up Node before invoking this; local devs need it
    # installed too.
    proc = subprocess.run(
        ["node", script], cwd=REPO_ROOT, check=False)
    if proc.returncode != 0:
        raise SystemExit(
            f"build_web_bundle: build_replay.mjs failed (exit "
            f"{proc.returncode}). Did `npm install` run at the repo root?")


# ---------------------------------------------------------------------
# Main.
# ---------------------------------------------------------------------

def _parse_target():
    target = "firmware"
    args = sys.argv[1:]
    for i, a in enumerate(args):
        if a == "--target" and i + 1 < len(args):
            target = args[i + 1]
    if target not in ("firmware", "replay"):
        raise SystemExit(
            f"build_web_bundle: unknown --target {target!r}; "
            f"use 'firmware' or 'replay'.")
    return target


def main():
    target = _parse_target()
    if target == "replay":
        # esbuild does its own skip-if-fresh check via incremental
        # builds when reused, but we invoke it as a one-shot so just
        # run it unconditionally — the build is ~50 ms.
        _run_replay_via_esbuild()
        return

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
    _emit_legacy_pages_header(OUTPUT_LEGACY_H, OUTPUT_LEGACY_CPP)

    js_size    = os.path.getsize(OUTPUT_JS_H)
    css_size   = os.path.getsize(OUTPUT_CSS_H)
    stubs_sz   = os.path.getsize(OUTPUT_STUBS_H)
    legacy_sz  = os.path.getsize(OUTPUT_LEGACY_H) + os.path.getsize(OUTPUT_LEGACY_CPP)
    print(f"build_web_bundle: js={js_size:,} css={css_size:,} "
          f"stubs={stubs_sz:,} legacy={legacy_sz:,}")

    # Soft guard against runaway bundle growth.  Measured against the
    # generated .h source size (each gzipped byte expands to ~6 chars
    # of `0x..,` text), so the threshold is roughly 6× the actual flash
    # footprint.  The link-time bytes are the gzip lengths printed
    # above (`static_app_js_len`, `static_app_css_len`); those drive
    # real flash usage and stay well under 200 KB.
    #
    # 800 KB cap = ~133 KB of actual flash. We have 22 MB of partition,
    # so this is a "did something accidentally double the bundle?"
    # tripwire, not a hardware constraint. Bumped from 640 KB in PR-C
    # because m5modes/ + the wsRecordToState adapter + useDisplaySnapshot
    # hook pushed us into ~590 KB of gzip-as-text, well under 200 KB of
    # actual flash. If you find yourself bumping this again, ask whether
    # the new consumer should also live in /static/ as a separate URL
    # (see #525 for the bundler-extension proposal).
    PROGMEM_BUDGET = 800 * 1024
    if js_size + css_size + stubs_sz > PROGMEM_BUDGET:
        raise SystemExit(
            f"build_web_bundle: outputs exceed {PROGMEM_BUDGET // 1024} KB "
            f"PROGMEM budget — review before committing."
        )


main()
