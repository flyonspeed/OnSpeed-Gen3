# DecelRate consumer-side smoothing — design

> **Historical note (2026-05-11)**: this spec describes the pre-PR-#523-PR-C
> state. References to `tools/web/lib/modes.js` describe a file that no
> longer exists — its Mode 3 renderer is now
> `packages/ui-core/components/svg/m5modes/DecelMode.js` and the EMA
> is applied via `IndexerPage::useDecelEma` then routed through the
> 500 ms `useDisplaySnapshot` snapshot. The smoothing behavior
> described here is preserved; the file layout differs.

Issue: [#362](https://github.com/flyonspeed/OnSpeed-Gen3/issues/362) — M5 wire + JSON DecelRate algorithms diverge by one EMA pass.

## Problem

The firmware ships `DecelRate` two ways:

- **JSON over WebSocket** (`DataServer.cpp`): the raw output of a 15-tap Savitzky–Golay first-derivative filter on smoothed IAS, scaled to kt/s, broadcast at 20 Hz.
- **Display-serial wire to the M5** (v4.23 frame, `DisplaySerial.h`): the wire does **not** carry decel — it carries `iasKt` (smoothed IAS, ×10), and the M5 differentiates locally with its own SavGol(15) + then applies a one-pole EMA at α=0.04 to produce `SmoothedDecelRate`. The M5's hardware decel pointer reads `SmoothedDecelRate`.

The indexer's Mode 3 decel gauge in `tools/web/lib/modes.js` reads `r.decelRate` straight from JSON with no smoothing. So the M5 hardware shows a value lagged ~1.25 s behind the indexer (the EMA's time constant at 20 Hz). Same algorithm, divergent smoothing, divergent visuals.

## Background — Gen2 layering

Worth establishing what was originally there, since Gen3 inherited some constants without inheriting the surrounding design.

**Gen2 Teensy AHRS sketch:**
- `IAS → SavGol(15)` at 10 Hz cadence → `DecelRate = -SavGol.Compute() * 10` (kt/s).
- One smoothing stage. Shipped raw to two consumers over serial.

**Gen2 M5 display:**
- Recomputed locally: `IAS → SavGol(15) → EMA(α=0.04 @ 20 Hz)` ≈ ~1.25 s τ. This is the "calm pointer" value.

**Gen2 standalone Wi-Fi liveview page (`html_liveview.h`):** did not display decel at all. Decel was wizard-only.

**Gen2 cal wizard's decel step (`html_calibration.h` / `javascript_calibration.h`):**
- Browser received raw `DecelRate` over the serial CRC line.
- Applied a one-pole EMA in JS with `α = slider.value`.
- Slider: `<input type="range" min=".02" max=".5" value=".1" step=".01">` with labels "SMOOTH ↔ RESPONSIVE".
- Slider state was session-only (not persisted to config).
- Vac's working preference per recent conversation: well toward SMOOTH (~0.04–0.06).

The Gen2 design was correct: each consumer applied one EMA on a SavGol-prefiltered signal, with α tuned to the consumer's UI affordance. The bug today is that the indexer skipped its EMA layer entirely.

## Design

Mirror the Gen2 layering on Gen3. Each surface owns its own EMA on the JSON `DecelRate` (which is the SavGol-prefiltered "raw" decel, exactly the role the Teensy's wire field played in Gen2). No firmware changes; no wire-format changes; no M5 changes.

### Layering after this fix

| Surface | Source | Layer 1 | Layer 2 | τ |
|---|---|---|---|---|
| Firmware → JSON DecelRate | `IAS` (smoothed in `SensorIO`) | SavGol(15) at 50 Hz | none | ~SavGol intrinsic |
| Firmware → display-serial `iasKt` | `IAS` (smoothed in `SensorIO`) | none — ships raw IAS | (M5 differentiates) | n/a |
| **M5 hardware gauge** | `iasKt` from wire | SavGol(15) at 20 Hz (already there) | EMA(α=0.04 @ 20 Hz) (already there) | ~1.25 s |
| **Indexer Mode 3** | JSON `DecelRate` | (none — already SavGol'd by firmware) | **EMA(α=0.04 @ 20 Hz) — NEW** | ~1.25 s — matches M5 |
| **Cal wizard decel page** | JSON `DecelRate` | (none — already SavGol'd by firmware) | **EMA(α=slider, default 0.05)** — **NEW** | tunable, default ~1.0 s |

The "match M5 exactly" promise is met by construction: both gauge consumers run at 20 Hz and use literal α=0.04 → identical time constants. The wizard reads the same JSON stream at the same rate, so its slider's α range (0.02–0.50) ports verbatim from Gen2 with the same time-constant meaning.

### Key choice: M5 stays as-is

The display-serial v4.23 frame does not carry decel. Adding a `decelRate` field would be a v4.24 wire-format bump, requiring a simultaneous flash of both firmwares. The wire format just bumped to v4.23 (PR #386, 2026-05-05) and we want to preserve the wire-budget for fields that aren't reconstructible.

Since `iasKt` *is* reconstructible into decel via the same SavGol(15) the firmware uses on its smoothed-IAS source, the M5's local pipeline already produces a value bit-equivalent in time-constant to what the firmware ships in JSON. Leaving the M5 alone is correct.

### α convention

Both the M5 and the new indexer/wizard EMAs use the convention:

```
smoothed_n = α * x_n + (1 - α) * smoothed_{n-1}
```

i.e. larger α = more weight on the new sample = more responsive. Smaller α = heavier smoothing. This matches Gen2's M5 (`SerialRead.ino:195`) and the Gen2 wizard slider's labels ("SMOOTH ↔ RESPONSIVE" with value increasing left-to-right). The slider in this PR keeps the same convention and the same `min=0.02 max=0.50 step=0.01` range.

### Default slider value

Gen2 default: 0.10. Vac's working preference: well toward SMOOTH. Default for this PR: **0.05** (≈ 1.0 s τ at 20 Hz). Documented in the PR description and release notes.

### Persistence

Slider value is **not** persisted to config. Gen2 didn't persist it either. Slider resets to default on page reload. Out of scope for this PR; a separate config-persistence question if it comes up.

### File ledger

| File | Change |
|---|---|
| `tools/web/lib/core/ema.js` | **NEW.** Stateful single-pole EMA helper. NaN-safe seeding, first-sample initialization. |
| `tools/web/lib/modes.js` | Mode 3 decel-gauge path: instantiate an `Ema(0.04)`, run `r.decelRate` through it, pass smoothed value to `<DecelGauge>` and the numeric readout. |
| `tools/web/lib/pages/CalWizardPage.js` | Decel step: add range slider (min=0.02, max=0.50, step=0.01, value=0.05) with SMOOTH/RESPONSIVE labels, instantiate an `Ema(slider.value)`, run `r.decelRate` through it. Display smoothed value next to gauge, plus the current α (so the pilot can see what they've selected). Slider input handler updates the EMA's α live (no re-seeding). |
| `tools/web/test/ema.test.mjs` | **NEW.** Unit tests: step response, NaN handling, seeding, α-mutation. |
| `docs/site/docs/reference/websocket-protocol.md` | Fix existing line 134 (currently claims "Smoothed IAS-decel rate" — the JSON value is the raw SavGol output, not smoothed). Restate as: "Raw SavGol(15) derivative of IAS, kt/s. Consumers smooth per-surface — M5 hardware gauge and indexer Mode 3 use EMA(α=0.04, ~1.25 s τ); cal wizard exposes a slider (0.02–0.50, default 0.05)." |

### What's NOT changed

- **Firmware.** No SensorIO, DataServer, DisplaySerial, or onspeed_core changes.
- **M5.** No SerialRead.cpp or main.cpp changes.
- **Wire format.** No v4.24 bump.
- **Existing C++ tests.** Continue to cover the SavGol math.

### Test plan

- `tools/web/test/ema.test.mjs` covers EMA correctness in isolation.
- Manual bench: connect a recorded NDJSON stream to the dev-server, open `/indexer` Mode 3, confirm the decel pointer/numeric track the M5 simulator at α=0.04. (`tools/m5-replay/` per `MEMORY.md`.)
- Manual bench on cal wizard decel page: confirm slider updates smoothing live, default lands at 0.05, gauge response feels like Vac's working preference.
- Existing `pio test -e native` continues to pass (no firmware changes).

## Consequences and follow-ups

- **Indexer Mode 3 gets ~1.25 s laggier than today.** That's the point — it's now lagging by the same amount as the M5 gauge it's mirroring. Pilots accustomed to the indexer being snappier will notice; the release note should call this out.
- **EMA helper in `lib/core/`** is the first general-purpose filter to live there. Keeps it tiny and testable. If we later want a shared `IasDecelEstimator` (SavGol + EMA) for parity testing against the C++ side, that builds on this naturally.
- **Wizard slider persistence** is left as a follow-up if it comes up. Likely not needed — Gen2 ran fine without it.
- **Time-constant equivalence is currently rate-tied.** All consumers happen to run at 20 Hz, so literal α matches. If a future consumer runs at a different rate (e.g., a future analysis page replaying at a different cadence), it will need to convert α from time-constant — not an issue for this PR, but worth a comment in `ema.js`.
