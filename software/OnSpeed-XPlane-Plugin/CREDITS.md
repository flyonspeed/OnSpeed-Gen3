# Credits

This plugin began life as [`flyonspeed/OnSpeed-XPlane`](https://github.com/flyonspeed/OnSpeed-XPlane),
a standalone repo written by Topher Timemachine and Mrcoole7890. It was imported
into `OnSpeed-Gen3` via `git subtree add` in
[PR #256](https://github.com/flyonspeed/OnSpeed-Gen3/pull/256) so the plugin
lives alongside the firmware and shares `onspeed_core`, CI, versioning, and
releases.

The upstream commit at import time was `fff96202fc82a9e8e0e2fda7d62ee92237c18a05`
(imported 2026-04-23).

## Authors

- **Topher Timemachine** — [@TopherTimeMachine](https://github.com/TopherTimeMachine)
  (original author)
- **Mrcoole7890** — [@Mrcoole7890](https://github.com/Mrcoole7890)
  (co-author)

## License

MIT, matching the repo root [LICENSE](../../LICENSE). The vendored X-Plane SDK
under `SDK/` carries its own license — see `SDK/license.txt`.

## Post-import changes

- [PR #256](https://github.com/flyonspeed/OnSpeed-Gen3/pull/256) — Import the
  plugin into this repo, reshape to `src/` + `scripts/`, unify versioning via
  `buildinfo.h`.
- [PR #259](https://github.com/flyonspeed/OnSpeed-Gen3/pull/259) — Bump the
  vendored X-Plane SDK from 4.1.1 to 4.3.0.
- [PR #260](https://github.com/flyonspeed/OnSpeed-Gen3/pull/260) — Hotfix:
  restore X-Plane SDK Linux libraries dropped in #259.
- [PR #261](https://github.com/flyonspeed/OnSpeed-Gen3/pull/261) — Route audio
  decisions through `onspeed_core::audio::ToneCalc` so the plugin and firmware
  share the same tone logic.
- [PR #263](https://github.com/flyonspeed/OnSpeed-Gen3/pull/263) — Add Windows
  to the GitHub Actions build matrix alongside Linux and macOS.
