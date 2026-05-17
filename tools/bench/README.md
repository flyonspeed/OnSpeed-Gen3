# tools/bench

Tooling for human-driven bench testing of OnSpeed firmware on real V4P / V4B hardware.

This directory holds scripts and supporting material for the **manual** bench protocol, which is the maintainer-required pre-merge check for any PR that touches SD writer code, web handlers, or anything that contends on `xWriteMutex`. The protocol is documented in:

- **The docs site**: [`docs/site/docs/contributing/bench-testing.md`](../../docs/site/docs/contributing/bench-testing.md) — public, contributor-facing.
- **The Claude skill**: [`.claude/skills/bench-stress/SKILL.md`](../../.claude/skills/bench-stress/SKILL.md) — Claude-facing, with the same content plus operating notes for the agent.

A future CI version that runs this protocol automatically on a self-hosted runner with a USB-attached V4P is tracked at [issue #553](https://github.com/flyonspeed/OnSpeed-Gen3/issues/553).

## Files

- **`stress_web_handlers.py`** — the stress script. Hammers `GET /api/logs`, the main HTML pages, the static bundles, occasional `POST /aoaconfigsave`, log file downloads, and multiple concurrent WebSocket clients, in a scripted pattern that mirrors realistic + worst-case pilot activity. PEP 723 inline metadata; runs via `uv run`.

  ```bash
  uv run ./stress_web_handlers.py --help
  uv run ./stress_web_handlers.py --duration 10                  # default profile
  uv run ./stress_web_handlers.py --duration 30 --aggressive     # tight cadence, race-catching
  uv run ./stress_web_handlers.py --duration 30 --no-downloads   # no paused_drops expected
  ```

## Related

- `tools/regression/` — host-side regression harness that runs `onspeed_core` algorithms against a recorded flight log. Different layer: this catches algorithm drift across `onspeed_core` extraction PRs, not real-hardware reliability.
- `tools/web/dev-server/` — local server that serves the firmware's PROGMEM bundle for browser testing of UI changes without flashing. Different layer: catches UI bugs without exercising SD / WiFi / FreeRTOS.
