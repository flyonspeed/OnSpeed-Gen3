#!/usr/bin/env python3
"""
check_docs_completeness.py — assert docs/site/docs/reference/config-parameters.md
documents every XML tag that ConfigXmlEmit writes to a saved config file.

Motivation
----------
Configuration is stored as XML (`config.cfg`); `ConfigXmlEmit.cpp` is the
wire-format source of truth for what fields appear in the file. The user-
facing reference doc at `docs/site/docs/reference/config-parameters.md`
must stay in sync with that source — otherwise pilots see fields in
their config file that aren't documented, or worse, they edit by hand
based on stale docs and break things.

This script iterates every `Add{Bool,Int,Float,String,Unsigned}()` call
in ConfigXmlEmit.cpp, extracts the XML tag name and its parent container,
and asserts each one appears in config-parameters.md in one of these
forms:

  `TAG`                — for top-level tags (direct children of <CONFIG2>)
  `PARENT > TAG`       — for nested tags (e.g. `VOLUME > ENABLED`)

Fails CI with a precise list if anything's undocumented.

Usage
-----
  ./scripts/check_docs_completeness.py

Exit codes
----------
  0 — every XML tag is documented
  1 — one or more XML tags are missing from config-parameters.md
  2 — file not found / input malformed

Sibling scripts
---------------
  scripts/check_core_purity.sh    — onspeed_core has no platform headers
  scripts/check_board_flags.sh    — HW_V4* only in HardwareMap.h
"""
from __future__ import annotations

import pathlib
import re
import sys


REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
EMIT_SRC  = REPO_ROOT / "software" / "Libraries" / "onspeed_core" / "src" / "config" / "ConfigXmlEmit.cpp"
DOCS_MD   = REPO_ROOT / "docs" / "site" / "docs" / "reference" / "config-parameters.md"

# Match:  AddElem(parent, "NAME")           → captures parent var + NAME
# Match:  AddBool(parent, "NAME", ...)      → captures parent var + NAME
# Match:  AddInt (parent, "NAME", ...)      → captures parent var + NAME
# Match:  AddFloat, AddString, AddUnsigned similarly
#
# The parent variable name identifies which XMLElement* it was called on.
# Top-level adds use "root"; nested adds use a local like "vol" or "flap".
_CALL_RE = re.compile(
    r'Add(Elem|Bool|Int|Float|String|Unsigned)\s*\(\s*(\w+)\s*,\s*"([^"]+)"',
)


_COMMENT_RE = re.compile(r'//[^\n]*|/\*.*?\*/', re.DOTALL)


def _strip_comments(source: str) -> str:
    """Remove C/C++ comments so example `AddBool(...)` snippets in comments
    don't produce spurious leaf entries."""
    return _COMMENT_RE.sub('', source)


def extract_leaves_from_emit(source: str) -> set[tuple[str, str]]:
    """
    Parse ConfigXmlEmit.cpp and return the set of (parent, leaf) pairs
    written by Add{Bool,Int,Float,String,Unsigned} calls.

    `parent` is the XML container tag ("VOLUME", "CAS_CURVE", etc.) or
    "" for top-level (direct children of <CONFIG2>).

    The same leaf name can appear under multiple parents (e.g. "X3"
    under both AOA_CURVE and CAS_CURVE, "ENABLED" under both VOLUME
    and CAS_CURVE). The set-of-pairs shape preserves both — a flat
    {leaf: parent} dict would silently lose the first occurrence.
    """
    source = _strip_comments(source)

    # First pass: map every local XMLElement* var name to the container
    # tag it represents. Match lines like:
    #   XMLElement* vol = AddElem(root, "VOLUME");
    parents: dict[str, str] = {"root": "CONFIG2"}
    elem_assign_re = re.compile(
        r'XMLElement\s*\*\s*(\w+)\s*=\s*AddElem\s*\(\s*(\w+)\s*,\s*"([^"]+)"',
    )
    for var_name, _parent_var, container_name in elem_assign_re.findall(source):
        parents[var_name] = container_name

    # Second pass: every Add{Bool,Int,Float,String,Unsigned} call is a
    # leaf under whatever parent variable it targets.
    leaves: set[tuple[str, str]] = set()
    for fn, parent_var, name in _CALL_RE.findall(source):
        if fn == "Elem":
            continue
        parent_tag = parents.get(parent_var, "<unknown>")
        # Top-level tags sit under root == CONFIG2 and are written in docs
        # without a "PARENT > " prefix. Everything else is nested.
        parent_for_docs = "" if parent_tag == "CONFIG2" else parent_tag
        leaves.add((parent_for_docs, name))

    return leaves


# Some doc sections introduce a parent container in the heading
# ("## Per-Flap Position Settings ... Each `FLAP_POSITION` element
# contains:") and then list leaves bare (`` `LDMAXAOA` ``) without the
# `FLAP_POSITION >` prefix. Map parent → list of section-header
# substrings that count as "declares this parent's context". If any
# header is present in the doc, bare leaves under that parent are
# accepted. Keep this small and explicit — a drift check shouldn't be
# loose.
_BARE_LEAF_CONTEXT_HEADERS: dict[str, tuple[str, ...]] = {
    "FLAP_POSITION": ("Per-Flap Position Settings",),
}


def documented_in_md(doc: str, tag: str, parent: str) -> bool:
    """
    Returns True if `tag` is documented in the Markdown reference doc.

    Accepted forms:
      top-level:      `TAG`
      nested:         `PARENT > TAG`
      nested (bare):  `TAG` — ONLY when the parent has a section header
                      listed in _BARE_LEAF_CONTEXT_HEADERS AND that
                      header appears in the doc. This matches the
                      existing doc's "Per-Flap Position Settings" style.
    """
    if not parent:
        return f"`{tag}`" in doc
    if f"`{parent} > {tag}`" in doc:
        return True
    bare_ok_headers = _BARE_LEAF_CONTEXT_HEADERS.get(parent, ())
    if bare_ok_headers and any(h in doc for h in bare_ok_headers):
        return f"`{tag}`" in doc
    return False


def main() -> int:
    try:
        emit_source = EMIT_SRC.read_text()
    except FileNotFoundError:
        print(f"✗ ConfigXmlEmit.cpp not found at {EMIT_SRC}", file=sys.stderr)
        return 2

    try:
        docs_source = DOCS_MD.read_text()
    except FileNotFoundError:
        print(f"✗ config-parameters.md not found at {DOCS_MD}", file=sys.stderr)
        return 2

    leaves = extract_leaves_from_emit(emit_source)

    missing: list[tuple[str, str]] = []
    for parent, tag in sorted(leaves):
        if not documented_in_md(docs_source, tag, parent):
            missing.append((parent, tag))

    if missing:
        print(f"✗ {len(missing)} XML tag(s) written by ConfigXmlEmit are "
              f"missing from {DOCS_MD.relative_to(REPO_ROOT)}:")
        for parent, tag in missing:
            name = f"`{parent} > {tag}`" if parent else f"`{tag}`"
            print(f"  {name}")
        print()
        print("Every XML tag that ConfigXmlEmit writes must be documented in "
              "the reference doc under the exact form shown above. Fix the "
              "doc or (if the tag shouldn't be written) remove it from "
              "ConfigXmlEmit.cpp.")
        return 1

    print(f"✓ all {len(leaves)} XML tag(s) from ConfigXmlEmit are "
          f"documented in {DOCS_MD.relative_to(REPO_ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
