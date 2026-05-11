# @onspeed/ui-core

Shared OnSpeed web UI primitives. Pure functions of a canonical `M5State`;
no data sources, no fetches, no WebSocket clients, no log parsing.

## What lives here

- `components/svg/` — SVG primitives (Indexer, SlipBall, EdgeTape, etc.).
- `core/` — geometry constants (`mapPct2Display`, panel dimensions),
  color tables, formatting helpers, smoothing primitives.
- `vendor/` — third-party deps (Preact standalone).

## What does NOT live here

- Data sources (WebSocket clients, log parsers, mock fixtures) — those
  belong in **adapters** at the page level.
- Page-specific composition (`ReplayPage`, `IndexerPage`) — those belong
  in their respective consumer trees.
- WASM artifacts (`onspeed_core.wasm`, `onspeed_m5.wasm`) — those are
  built from `software/Libraries/onspeed_core/` and loaded by adapter
  code, not by ui-core.

## Consumers

- `tools/web/` — the firmware-served web UI (live `/indexer`, etc.).
  Bundled into PROGMEM by `scripts/build_web_bundle.py`.
- `docs/site/docs/data-and-logs/replay/` — the docs-site replay page.
  Served static via MkDocs.

Both consumers import from `packages/ui-core/` via relative path.
There is no build step; ES modules resolve directly.

## Design principle

See `docs/superpowers/plans/2026-05-08-replay-INDEX.md` "Design
principle: lift, don't copy" — one canonical state shape (`M5State`),
adapters per input source, ui-core/ used by every page. Tracked as
issue #523.
