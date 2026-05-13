"""Cache-bust the replay-bundle.js + replay.css URLs in replay.md.

The replay tool's bundle URL is stable (`replay-bundle.js`) which means
browsers — especially Brave / Chrome with HTTP cache enabled — serve
the previous version across mkdocs autoreloads. Pilots see "old
behavior" long after the bundle was rebuilt.

This hook computes a SHA-256 prefix of each asset's content on every
build and substitutes the stable URL with `replay-bundle.js?v=<hash>`
in the rendered HTML. The query string changes whenever the bundle
content changes, so the browser cache is naturally invalidated.

It also force-copies the assets from `docs/` to `site/` on every
build because mkdocs's incremental autoreload otherwise skips
non-markdown files — leaving stale assets in `site_dir` even after
the source on disk changed.

Wired via `hooks: [hooks/cache_bust_replay_bundle.py]` in mkdocs.yml.
"""

import hashlib
import os
import shutil


REPLAY_PAGE_SRC_PATH = "data-and-logs/replay.md"
REPLAY_ASSETS_DIR_REL = "data-and-logs/replay"
REPLAY_ASSETS = ("replay-bundle.js", "replay.css")


def _hash_file(path):
    """8-char SHA-256 prefix of file contents, or empty string if absent."""
    if not os.path.exists(path):
        return ""
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()[:8]


def _docs_dir(config):
    return config["docs_dir"]


def _site_dir(config):
    return config["site_dir"]


# Cache the per-build hashes so `on_page_markdown` doesn't re-read the
# files for every page. Populated by `on_files`.
_hashes = {}


def on_files(files, config):
    """Compute fresh content hashes once per build, and force-copy the
    replay assets from docs/ to site/ so autoreload doesn't serve a
    stale built copy.
    """
    docs = _docs_dir(config)
    site = _site_dir(config)
    for name in REPLAY_ASSETS:
        src = os.path.join(docs, REPLAY_ASSETS_DIR_REL, name)
        _hashes[name] = _hash_file(src)
        # Mirror into site_dir. mkdocs would otherwise skip this for
        # non-markdown files during autoreload.
        dst = os.path.join(site, REPLAY_ASSETS_DIR_REL, name)
        if os.path.exists(src):
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copy2(src, dst)
    return files


def on_page_markdown(markdown, page, config, files):
    """Replace `replay-bundle.js` / `replay.css` references on the
    replay page with their hashed variants. Only the replay page is
    touched — every other page has stable, hash-free asset URLs.
    """
    if page.file.src_path != REPLAY_PAGE_SRC_PATH:
        return markdown
    out = markdown
    for name in REPLAY_ASSETS:
        h = _hashes.get(name)
        if not h:
            continue
        # Match both `href="replay.css"` and `src="replay-bundle.js"`
        # without overmatching the already-hashed forms.
        plain = name
        hashed = f"{name}?v={h}"
        # Replace bare references (no query string already attached).
        # Use a simple substring swap; the asset names are distinctive
        # enough that false positives are unlikely on the replay page.
        out = out.replace(f'src="{plain}"', f'src="{hashed}"')
        out = out.replace(f'href="{plain}"', f'href="{hashed}"')
    return out
