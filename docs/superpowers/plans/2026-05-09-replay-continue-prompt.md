---
date: 2026-05-09
owner: Sam (handoff prompt to next agent)
status: continue prompt — paste verbatim into a fresh agent context
related:
  - 2026-05-09-replay-retro.md
  - 2026-05-09-replay-m5-wasm.md
  - 2026-05-08-replay-INDEX.md
---

# Continue prompt: pick up the OnSpeed replay tool after context reset

Paste the prose below into a fresh Claude Code session. Everything
before this `## Prompt` line is for Sam, not the agent.

The original session ran from "what is OnSpeed" all the way through
PRs #461, #487, #490, #491, #496, #498, #502, #507, #511 merged, and
PR #512 left unmerged after a real-flight trial run exposed bugs the
test suite missed. The agent ran out of context near the end.

The retro document (`2026-05-09-replay-retro.md` on the
`sam/video-overlay` branch / PR #504) captures what failed and the
architectural correction. The continue prompt here is a 60-second
on-ramp; the retro is the deeper read.

## Prompt

You are picking up the OnSpeed Replay Tool after a context reset.
Earlier sessions have laid the foundation; recent sessions found bugs
and identified an architectural correction. Read this carefully before
writing any code.

### Where we are

The OnSpeed replay tool is a browser app that overlays a synthesized
M5 indexer onto recorded cockpit video. Implementation lives at
`tools/web/lib/pages/ReplayPage.js` plus `tools/web/lib/replay/*.js`
plus `tools/web/lib/components/svg/m5modes/*.js`. It deploys (eventually)
to the OnSpeed docs site, NOT the firmware bundle (the bundler in
`scripts/build_web_bundle.py` excludes replay code via path filters).

Architectural foundation work that has shipped:
- **PR #487/#490/#491**: firmware LogReplay parity (rate-adjusted
  accel EMA, streaming synth flapsRawADC for old logs).
- **PR #496**: `onspeed_core` compiled to WASM (Project B1).
- **PR #498**: Python `log_replay.py` migrated to host_main wrapper.
- **PR #502**: regression-test infrastructure for LogReplayEngine.
- **PR #507**: M5-Display firmware compiled to WASM (Project B2 PR 1).
  Exposes ~30 state-var accessors. Virtual `millis()` driven by JS.
- **PR #511**: LogReplayEngine wire-field completeness audit + tone_calc
  embind binding (Project B2 PR 1.5).

PR #512 (UI integration of M5 sim, Project B2 PR 2) is **OPEN AND
UNMERGED**. It has local fixes from a debugging session that you
should NOT just merge — the architecture diagnosis below will tell
you what to do with it.

### Read these in order before any code

1. `docs/superpowers/plans/2026-05-09-replay-retro.md`
   The trial-run retro. Bugs that survived the foundation, why the
   tests missed them, and the architectural correction. **This is the
   most important document.**
2. `docs/superpowers/plans/2026-05-09-replay-m5-wasm.md`
   The original B2 plan. Read knowing the retro supersedes parts of it.
3. `docs/superpowers/plans/2026-05-08-replay-INDEX.md`
   Project sequencing.
4. `software/sketch_common/src/tasks/LogReplay.cpp`
   The firmware's own log-replay task. **Your eventual lift target.**
5. `tools/web/lib/replay/wireBridge.js` and
   `tools/web/lib/pages/ReplayPage.js::rowObjAt`
   The two JS hand-derivation sites the lift eliminates.

The doc set is on `origin/sam/video-overlay` (PR #504, never merged,
living plans reference). Pull from there:

```bash
git fetch origin sam/video-overlay
git show origin/sam/video-overlay:docs/superpowers/plans/2026-05-09-replay-retro.md > /tmp/retro.md
```

### The bugs Sam reported on the trial run

After loading his real flight log + cockpit MP4 + cfg through the
replay tool with M5-accurate mode on:

1. **AOA stuck at flat values** (44% for full flaps, 39% for 16°
   flaps) regardless of actual flight maneuvering. **Root cause
   identified and partially fixed:** AOA polynomial coefficients are
   dropped on the cfg round-trip in `bindings.cpp`. Fix is the diff
   already in PR #512's local state — see "What's in PR #512" below.

2. **iasValid wrong** (always true, no taxi dashes). **Root cause
   identified:** `rowObjAt` in JS has `iasValid = iasKt > 0` instead
   of the firmware's hysteretic state machine
   (`onspeed_core/src/sensors/IasAlive.h`). PR #512 patches this in JS;
   the retro says don't fix in JS, fix by deleting JS hand-port.

3. **Catch-up scramble on first sync / scrub.** PR #512's per-frame
   effect tick-steps virtual time, replaying through history at first
   sync. Visible state churns wildly. PR #512 patches with snap-on-
   large-jump; retro says delete the whole tick-stepping path.

4. **gOnset / Slip / decel jitter.** Unknown root cause. Likely real:
   the 50 Hz SD log aliases IMU vibration into the smoothable band,
   the firmware's 208 Hz IMU silently filtered it out before logging
   wasn't possible. **Open question — needs 208 Hz reference log
   (issue #485) to confirm.**

5. **Mode click during pause doesn't refresh visible mode.**
   Small JS fix; PR #512 has it (mode-id effect calls
   `setM5State(sim.read())` after `setMode()`).

6. **WebM export black shadow.** Phase 4.5 (mp4-muxer); deferred.

7. **File handles don't persist across reload.** Layer-1 task; deferred.

### What's in PR #512 right now (DO NOT JUST MERGE)

Local commits past `origin/replay-m5-wasm-ui`:
- AOA polynomial fix in `bindings.cpp` (export side emits aoaCurve;
  import side reads it). **This is correct and ships.**
- Hysteretic iasValid in `rowObjAt` (JS hand-port).
  **Symptom patch — superseded by the LogReplayTask lift.**
- PresentationSmoother class in `wireBridge.js` (Gen2-style τ).
  **Symptom patch — superseded by the lift.**
- Per-frame tick-stepping with snap-on-large-jump in
  `ReplayPage.js`. **Symptom patch — superseded.**
- Mode-id effect refresh fix.
  **Real fix; small; ships.**
- `dev-server/server.mjs`: `.wasm` MIME map.
  **Real fix; ships.**
- M5 sim `delete window.Module` → `globalThis.Module = undefined`
  to dodge strict-mode TypeError.
  **Real fix; ships.**

The merge-worthy fixes (AOA polynomial, mode-id refresh, .wasm MIME,
delete-Module workaround) should be carved out into a small standalone
PR, leaving the symptom patches behind.

### What to do, in order

#### Step 1 (REVISED per Sam): A/B comparison toggle in PR #512

**Tracked as issue #514.**

Sam's call (and a smart one): **before merging or closing PR #512,
add the C++ LogReplayTask path alongside the JS pipeline as an
A/B toggle.** This way we visually compare the two paths on real
flight data BEFORE committing to deleting the JS hand-ports.

The plan:

1. **Lift LogReplayTask to onspeed_core** (full work item — same as
   what was Step 2 below, but done as a PARALLEL path, not a replacement).
   - `software/sketch_common/src/tasks/LogReplay.cpp::Process_()` is
     the firmware's existing log-replay code. Lift the per-row
     "CSV row → wire bytes" path into
     `software/Libraries/onspeed_core/src/replay/LogReplayTask.{h,cpp}`.
   - Wraps `LogReplayEngine` + `BuildDisplayFrame` + the iasValid
     hysteretic state (UpdateIasAlive). Stateful per-replay-session.
     Resettable.
   - Expose via embind as `LogReplayTaskHandle.process_row(rowBytes, cfg)`
     → 77-byte Uint8Array.
   - CSV parsing already in `onspeed_core/src/proto/LogCsv.cpp`. Either
     expose `parse_log_row` to JS, or have `process_row` accept a
     pre-parsed `LogRow` struct and let JS keep using `parseLog.js` for
     CSV bytes → row[] (CSV parsing isn't where the bugs are).

2. **Add a new UI toggle**: "Engine: JS pipeline | C++ LogReplayTask"
   (third toggle, alongside "M5-accurate mode" and "Show overlay").
   Default to JS pipeline (existing behavior unchanged unless you
   click). When C++ is on, the per-frame effect routes through
   `task.process_row()` instead of `rowObjAt + LogReplayEngine.step
   + buildWireFrame`.

3. **Keep both paths working**. The JS path keeps its hysteretic
   iasValid, its PresentationSmoother, etc. Sam clicks back and forth
   and visually compares.

4. **The merge-worthy fixes from current PR #512 stay**:
   - `bindings.cpp` aoaCurve / kFit round-trip (real bug — affects
     ALL consumers of `parseConfigVal`, not just replay).
   - `ReplayPage.js` mode-id effect refresh.
   - `dev-server/server.mjs` `.wasm` MIME entry.
   - `m5sim.js` `delete window.Module` workaround.

5. **Sam does an A/B trial run**:
   - Loads files, syncs.
   - Clicks JS path: see what it looks like.
   - Clicks C++ path: see what it looks like.
   - Reports back: identical / different-and-here's-what-changed.

#### After Step 1 reports back

- **If C++ path looks correct** (AOA tracks, no flat values, smooth):
  delete the JS-side hand-ports in a follow-up. Replace
  `rowObjAt`/`buildDisplayInputs`/JS hysteretic iasValid/JS
  PresentationSmoother with single-source C++ flow.
- **If C++ and JS paths look identical** (both buggy or both
  smooth): we have multiple bugs and need to dig further;
  the lift was at minimum useful as ground truth.
- **If C++ path looks WORSE**: there's something wrong with
  the lift itself; investigate before deleting JS path.

This A/B approach replaces "trust the test, delete the JS code"
with "trust your eyes on real data." Lower risk; same end state.

#### Step 2 (was Step 2 in original): trim out hand-ports

After Step 1's A/B confirms the C++ path is correct, this becomes
the "delete the JS path" PR. The JS-side replay tool deletes
`rowObjAt`, `wireBridge.js::buildDisplayInputs`, the JS hysteretic
iasValid, the JS PresentationSmoother. The toggle goes away.

#### Step 3: add the three test types

After the lift, add tests that would have caught Bugs 1-3 on day 1:

- **End-to-end real-flight fixture replay.** A 30-row real-flight CSV
  snippet with the cfg used to record it, plus a known-good wire-byte
  reference sequence. Test runs CSV → C++ pipeline → wire bytes → M5
  sim → state, asserts state matches reference at each row.
  Depends on Sam recording a 208 Hz bench flight (issue #485) so we
  have ground truth. If 208 Hz isn't available, use a 50 Hz log with
  the firmware's existing log-replay-mode wire output captured.

- **Cfg round-trip.** Parse XML → emit JSON via `parse_config` → parse
  JSON via `parseConfigVal` → drive engine, assert AOA matches direct
  `parseConfigXml`'s engine output. Captures Bug 2 class.

- **iasValid hysteresis fixture.** Synthetic IAS ramp 0→25→10→25 over
  50 rows. Pass through engine. Assert iasValid follows
  `UpdateIasAlive` exactly. Captures Bug 1 class.

#### Step 4: re-trial-run

Sam loads his real flight + cockpit MP4 + cfg. Verify:
- AOA tracks log AOA at each video time (not flat).
- iasValid goes false during taxi (IAS dashes appear).
- Mode click during pause swaps mode immediately.
- No catch-up scramble on first sync or scrub.
- Decel and gOnset are smooth (or, if jumpy, file as #485-blocked).
- Smooth ball (or, if jumpy, file as #485-blocked).

Only after this is clean does PR 3 (delete legacy JS-side rec path)
become relevant.

### Process notes

- **Sam's fixed-point bulldog rule still applies**: two consecutive
  clean reviews before merge.
- **Worktrees + master safety**: never commit directly to master, see
  CLAUDE.md root.
- **Plans live on PR #504**, not master. Update there.
- **Bulldogs sometimes review the wrong tree.** Pre-flight must verify
  branch state and file existence before reviewing. PR #512's bulldog
  cycle had multiple false-negative process failures.
- **The retro is gold.** When Sam reports a bug, ask whether the
  symptom is in a JS-side hand-port, and if so, prefer to delete the
  port not patch it.

### Files Sam uses for trial-run

- `~/Downloads/Replay/sam_onspeed_aoa_4_11_2026.csv` (50 Hz log)
- `~/Downloads/Replay/onspeed2_latest.cfg` (RV-10, N720AK)
- `~/Downloads/cleaned_4_11_2026_sam_aoa.mp4` (cockpit video)

(Symlinks under `test-data/` in the worktree have come and gone; check
before assuming they exist.)

### One more thing

The PresentationSmoother concept (Gen2-style τ=2.5s lateral, 1s vertical)
is a real architectural question worth thinking about, separate from
the lift. Gen2 firmware always smoothed at the wire-output stage; Gen3
dropped that layer assuming 208 Hz IMU made it unnecessary. For 50 Hz
log replay, the firmware's AHRS-side smoothing doesn't compensate for
the aliasing. Decision deferred until 208 Hz reference log lands; if
real-flight at 208 Hz looks smooth without presentation smoothing,
we don't need it. If 50 Hz logs are visually unwatchable, we add a
PresentationSmoother in C++ (NOT in JS — see retro) on the LogReplayTask
output.

### Final framing

Sam said: "I love where we've gotten to and it's really pro. but I
want it all to be correct by construction and I don't feel like we've
gotten there."

The retro answers why we haven't, and Step 2 above is the path. Don't
ship more JS-side patches; lift to C++ and make the JS layer pure glue.
That's the work.

— end prompt —
