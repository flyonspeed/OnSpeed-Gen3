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


def extract_tags_from_emit(source: str) -> tuple[dict[str, str], dict[str, str]]:
    """
    Parse ConfigXmlEmit.cpp and return:
      parents:  {local_var_name -> parent_element_tag_name}
                e.g. {"root": "CONFIG2", "vol": "VOLUME", "flap": "FLAP_POSITION"}
      leaf_tags: {tag_name -> parent_tag_name_or_"" if top-level}
                 e.g. {"AOA_SMOOTHING": "", "ENABLED": "VOLUME",
                       "X3": "CAS_CURVE" or "AOA_CURVE" (last-wins is fine
                       because we document nested separately when needed)}

    The approach walks `Add*` calls in source order. `AddElem(p, "NAME")`
    defines a new parent variable; subsequent Add* calls on that variable
    attach their NAME underneath it.
    """
    parents: dict[str, str] = {}
    leaf_tags: dict[str, str] = {}

    # Seed the root parent. In ConfigXmlEmit the top-level root is the var
    # passed into EmitXml; callers use "root" by convention.
    parents["root"] = "CONFIG2"

    for fn, parent_var, name in _CALL_RE.findall(source):
        if fn == "Elem":
            # AddElem(parent_var, "CONTAINER_NAME") — record that the
            # returned pointer is a new parent. But the source is written
            # as e.g. `XMLElement* vol = AddElem(root, "VOLUME");` — the
            # assigned variable name is to the LEFT of =, not captured by
            # our regex. Walk back through the source to find the
            # enclosing assignment.
            # Handled below via a second pass.
            continue
        else:
            leaf_tags[name] = parents.get(parent_var, "<unknown>")

    # Second pass: associate each AddElem call's returned XMLElement* with
    # its variable name, so downstream `Add*(vol, "LEAF", ...)` calls can
    # be resolved. Match lines like:
    #   XMLElement* vol = AddElem(root, "VOLUME");
    _ELEM_ASSIGN_RE = re.compile(
        r'XMLElement\s*\*\s*(\w+)\s*=\s*AddElem\s*\(\s*(\w+)\s*,\s*"([^"]+)"',
    )
    for var_name, parent_var, container_name in _ELEM_ASSIGN_RE.findall(source):
        parents[var_name] = container_name

    # Now re-walk with parents populated.
    leaf_tags.clear()
    for fn, parent_var, name in _CALL_RE.findall(source):
        if fn == "Elem":
            continue
        parent_tag = parents.get(parent_var, "<unknown>")
        # Top-level tags sit under root == CONFIG2; they're written in docs
        # without a "PARENT > " prefix. Everything else is nested.
        leaf_tags[name] = "" if parent_tag == "CONFIG2" else parent_tag

    return parents, leaf_tags


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

    _, leaf_tags = extract_tags_from_emit(emit_source)

    missing: list[tuple[str, str]] = []
    for tag, parent in sorted(leaf_tags.items()):
        if not documented_in_md(docs_source, tag, parent):
            missing.append((tag, parent))

    if missing:
        print(f"✗ {len(missing)} XML tag(s) written by ConfigXmlEmit are "
              f"missing from {DOCS_MD.relative_to(REPO_ROOT)}:")
        for tag, parent in missing:
            name = f"`{parent} > {tag}`" if parent else f"`{tag}`"
            print(f"  {name}")
        print()
        print("Every XML tag that ConfigXmlEmit writes must be documented in "
              "the reference doc under the exact form shown above. Fix the "
              "doc or (if the tag shouldn't be written) remove it from "
              "ConfigXmlEmit.cpp.")
        return 1

    print(f"✓ all {len(leaf_tags)} XML tag(s) from ConfigXmlEmit are "
          f"documented in {DOCS_MD.relative_to(REPO_ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
