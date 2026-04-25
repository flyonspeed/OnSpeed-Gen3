# Browser IMU + live tones: phone-driven OnSpeed demo

**Status:** design only. No implementation yet.
**Date:** 2026-04-24
**Owner:** TBD
**Related:** PR #268 (M5 WASM simulator), follow-up bullet "Audio — pipe `ToneCalc` through Web Audio."

## Goal

Turn the embedded WASM simulator on `installation/external-display.md` into a full sensory demo: a visitor on their phone tilts the phone forward to "fly slower," and hears the **exact same tones** the airplane plays. Optionally, the indexer needle and attitude indicator track the phone's orientation in real time.

This is the closest a non-owner can get to "fly OnSpeed without owning OnSpeed" — much higher-fidelity than reading the docs page or watching a synthetic ramp on the simulator.

## What's already in place

- **`onspeed_core/audio/ToneCalc`** — `calculateTone(fAOA, ToneThresholds)` returns `{EnToneType::None|Low|High, fPulseFreq}`. Platform-independent. Already compiles under Emscripten as part of the M5 WASM build.
- **Tone constants** — low tone 400 Hz, high tone 1600 Hz, sample rate 16000 Hz (matches firmware). Pulse PPS bands: low 1.5–8.2, high 1.5–6.2, stall 20.0.
- **WASM simulator** — `software/OnSpeed-M5-Display/sim/` already builds the M5 renderer to a 320×240 canvas via Emscripten + SDL2. The page is embedded on the docs site at `installation/external-display.md`.
- **Stall flash + chevron rendering** — the M5 renderer already draws color-coded chevrons keyed off body angle in degrees. No work needed there.

## What's new

Three pieces:

1. **Phone IMU input** — read `DeviceOrientationEvent.beta` (forward/back tilt) and feed it as DerivedAOA into the existing M5 renderer's data path.
2. **Web Audio output** — a small JS module that consumes the `ToneResult` from `ToneCalc::calculateTone()` (passed through the WASM boundary) and synthesizes the corresponding 400 Hz / 1600 Hz square wave gated at the requested PPS rate. Same audible output the firmware produces.
3. **Permission flow** — a "Tap to enable tilt + sound" button on the docs page that triggers the iOS-Safari permission prompt and the audio-context unlock in one user gesture.

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│  Browser (phone or desktop with mouse-drag fallback)            │
│                                                                  │
│  DeviceOrientationEvent  ──beta──► JS shim ──► AOA degrees       │
│                                                  │                │
│                                                  ▼                │
│         ┌───────────────────────────────────────────────────┐   │
│         │  WASM module (existing M5 sim + onspeed_core)     │   │
│         │                                                    │   │
│         │  AOA → renderer (chevrons, donut, percent-lift)   │   │
│         │  AOA → ToneCalc::calculateTone()                  │   │
│         │           │                                        │   │
│         │           ▼                                        │   │
│         │       {EnToneType, PPS}  ──► JS callback          │   │
│         └───────────────────────────────────────────────────┘   │
│                                                  │                │
│                                                  ▼                │
│         ┌───────────────────────────────────────────────────┐   │
│         │  Web Audio (AudioWorklet or Oscillator + Gain)    │   │
│         │                                                    │   │
│         │  carrier osc: 400 or 1600 Hz square wave          │   │
│         │  envelope:    on/off at PPS Hz (50% duty cycle)   │   │
│         │  output:      AudioContext.destination            │   │
│         └───────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
```

## Component design

### 1. Phone IMU → AOA

```js
// docs/site/docs/assets/sim/imu_input.js
let aoaDeg = 0;          // body angle, sourced from device tilt
let aoaSourceLive = false;

async function enableImu() {
  // iOS Safari requires gesture-triggered permission request.
  if (typeof DeviceOrientationEvent.requestPermission === 'function') {
    const state = await DeviceOrientationEvent.requestPermission();
    if (state !== 'granted') return false;
  }
  window.addEventListener('deviceorientation', onTilt);
  aoaSourceLive = true;
  return true;
}

function onTilt(ev) {
  // beta is forward/back tilt in degrees, [-180, 180].
  // Phone flat on table → beta ≈ 0.
  // Phone vertical with screen facing pilot → beta ≈ 90.
  // We map "phone tilted upward 90° = level flight (AOA = 0)" so the
  // visitor's natural reading position is the cruise condition.
  // Tilting forward (beta < 90) raises AOA; tilting back lowers it.
  const tilt = ev.beta;          // -180..180
  aoaDeg = (90 - tilt);          // 0 at vertical, +ve as you tilt forward
  // Smooth via EMA so jitter doesn't strobe the chevrons.
  // alpha = 0.2 → ~5-frame time constant at 60 Hz event rate.
  aoaSmoothed = aoaSmoothed * 0.8 + aoaDeg * 0.2;
}
```

**Why beta = 90 maps to AOA = 0:** the natural way a visitor holds their phone upright (screen facing them, top edge up) is `beta ≈ 90`. Mapping that to "wing at zero AOA, fuselage roughly level" lets them tilt forward like pulling the nose up to enter the slow side of the tone map. Reversing the sign so "tilt up = pull = AOA increases" matches stick-and-rudder intuition.

**Range clamping:** clamp `aoaSmoothed` to `[alpha_0, alpha_stall + 4]` so the visitor's phone tilt covers the full firmware tone range. With the existing dummy frame's setpoints (`alpha_0 = -4`, `StallWarn = 20`, `alpha_stall = 18`), the clamp is `[-4, 24]` — exactly what PR #283 established for the synthetic ramp.

**Desktop fallback:** for visitors without a phone IMU, expose a draggable slider from `-4` to `+24` that drives `aoaDeg` directly. Same data path; no IMU required.

### 2. WASM-side: expose ToneCalc to JS

`ToneCalc::calculateTone()` already runs inside the WASM bundle today (the M5 sim links `onspeed_core`). What's missing is a JS-callable surface that returns the `ToneResult` per frame.

```c++
// software/OnSpeed-M5-Display/sim/SimMain.cpp (additions)
extern "C" {
    EMSCRIPTEN_KEEPALIVE
    int onspeed_tone_for_aoa(float aoaDeg,
                             float ldmaxDeg,
                             float onspeedFastDeg,
                             float onspeedSlowDeg,
                             float stallWarnDeg,
                             float* outPps) {
        const onspeed::ToneThresholds th = {
            ldmaxDeg, onspeedFastDeg, onspeedSlowDeg, stallWarnDeg
        };
        const auto r = onspeed::calculateTone(aoaDeg, th);
        *outPps = r.fPulseFreq;
        return static_cast<int>(r.enTone);  // 0=None, 1=Low, 2=High
    }
}
```

JS calls this once per render frame (60 Hz max, but the renderer ticks at the `dummy data` cadence, currently 10 Hz — that's fine for tone gating).

### 3. JS-side Web Audio

```js
// docs/site/docs/assets/sim/audio_engine.js
let audioCtx = null;
let carrier = null;     // OscillatorNode
let gateGain = null;    // GainNode driven on/off by PPS
let masterGain = null;  // user-controllable volume
let currentTone = 0;    // 0=None, 1=Low(400Hz), 2=High(1600Hz)
let gateTimer = null;

async function enableAudio() {
  if (audioCtx) return true;
  audioCtx = new (window.AudioContext || window.webkitAudioContext)();
  await audioCtx.resume();  // user-gesture unlock
  masterGain = audioCtx.createGain();
  masterGain.gain.value = 0.15;  // conservative default; user knob optional
  masterGain.connect(audioCtx.destination);
  return true;
}

function setTone(toneType, pps) {
  if (!audioCtx) return;
  if (toneType === 0) {
    stopTone();
    return;
  }
  const wantHz = (toneType === 2) ? 1600 : 400;
  if (currentTone !== toneType) {
    stopTone();
    carrier = audioCtx.createOscillator();
    carrier.type = 'square';   // matches the firmware's PCM source
    carrier.frequency.value = wantHz;
    gateGain = audioCtx.createGain();
    gateGain.gain.value = 1.0; // gate envelope follows the PPS schedule
    carrier.connect(gateGain).connect(masterGain);
    carrier.start();
    currentTone = toneType;
  }
  // Re-schedule the gate envelope every time PPS changes
  scheduleGate(pps);
}

function scheduleGate(pps) {
  if (gateTimer) { clearInterval(gateTimer); gateTimer = null; }
  if (pps <= 0.01) {
    // Solid tone (ONSPEED band) — gate stays on.
    gateGain.gain.setValueAtTime(1.0, audioCtx.currentTime);
    return;
  }
  const period = 1.0 / pps;
  const halfPeriod = period * 0.5;
  // Use AudioParam ramps rather than setInterval for jitter-free
  // gating. Schedule ~2 s of pulses ahead and re-schedule when PPS
  // changes.
  let t = audioCtx.currentTime;
  const horizon = t + 2.0;
  gateGain.gain.cancelScheduledValues(t);
  while (t < horizon) {
    gateGain.gain.setValueAtTime(1.0, t);
    gateGain.gain.setValueAtTime(0.0, t + halfPeriod);
    t += period;
  }
}

function stopTone() {
  if (carrier) { carrier.stop(); carrier.disconnect(); carrier = null; }
  if (gateGain) { gateGain.disconnect(); gateGain = null; }
  if (gateTimer) { clearInterval(gateTimer); gateTimer = null; }
  currentTone = 0;
}
```

**Why square waves + gate envelope, not pre-recorded WAVs:** the firmware uses pre-rendered PCM for low CPU on ESP32. The browser doesn't have that constraint, and synthesis from `OscillatorNode` lets the audio engine track PPS changes continuously rather than restarting buffer playback. A pilot can sweep the phone through ONSPEED-Slow into Stall-Warn and hear the pulse rate climb smoothly without buffer-edge clicks.

**Why `setValueAtTime` ramps rather than `setInterval`:** Web Audio's audio thread runs independently of the JS main loop. Scheduling future gain ramps via `AudioParam.setValueAtTime` produces sample-accurate gating; `setInterval` jitters by tens of milliseconds when the page is busy.

### 4. Permission UX

Above the embedded sim iframe, render a single button:

```html
<button id="onspeed-enable">Tap to enable tones + tilt</button>
```

Click handler does, in order:
1. `await enableAudio()` — creates `AudioContext`, calls `resume()`. Required by every modern browser.
2. `await enableImu()` — calls `DeviceOrientationEvent.requestPermission()` on iOS, no-op elsewhere.
3. If both succeed, hide the button and start the demo loop.
4. If IMU permission is denied, fall back to the draggable AOA slider; audio still works.

**Crucial:** the gesture must be a single click that fires both unlock paths. iOS specifically rejects permission requests that aren't directly inside a user-gesture handler.

### 5. Render loop integration

Replace the existing dummy-data ramp in `SerialRead.cpp::SerialRead()` (the `DUMMY_SERIAL_DATA` block) with a thin shim that reads `aoaDeg` from JS-exposed memory and runs the rest of the existing pipeline unchanged. The renderer doesn't know or care whether AOA came from a synthetic ramp or a phone tilt.

```c++
// software/OnSpeed-M5-Display/sim/SimMain.cpp
extern "C" {
    EMSCRIPTEN_KEEPALIVE
    void onspeed_set_aoa(float aoaDeg) { g_simAoaDeg = aoaDeg; }
}

// In SerialRead.cpp's DUMMY_SERIAL_DATA block:
AOA = g_simAoaDeg;   // set from JS
// ...everything else (PercentLift formula, threshold writes) unchanged
```

JS calls `Module._onspeed_set_aoa(aoaSmoothed)` on every IMU event, then on the next render tick the M5 sim draws the new orientation.

## Things this design deliberately does not include

- **Slip ball driven by phone roll.** Could be added; phone `gamma` (left/right tilt) is a natural source. Out of scope for the v1; visual indicator only.
- **G-load from phone accel.** Tempting, but the phone's accelerometer is noisy enough that the G-history page would look like static. Skip.
- **Real flight-log replay over Web Audio.** The pieces would all be there once this lands — feed a pre-recorded CSV through the same path and let visitors hear an actual approach. Worth a follow-up issue but distinct from the IMU demo.
- **Multi-flap support.** The dummy frame uses one flap config. Adding a flap selector is trivial UI; ship without it for v1.
- **Mute button.** The page-level "Stop tones" button is a single click; full audio-control surface (volume slider, etc.) is overkill for a demo widget.

## Risks and unknowns

| Risk | Mitigation |
|---|---|
| iOS Safari sandbox quirks | Test on iOS 17+ before rollout; fall back to slider if `requestPermission` is unavailable |
| Audio autoplay restrictions on Chromium | The "Tap to enable" button handles this — no audio plays before user gesture |
| WASM bundle size growth from JS bridge | Negligible; the audio engine is pure JS, the C bridge is two `EMSCRIPTEN_KEEPALIVE` symbols |
| Phone-IMU jitter producing chevron strobing near setpoint boundaries | EMA smoothing on the JS side (alpha = 0.2 over ~60 Hz events) plus the existing renderer's hysteresis is enough |
| Visitor with hearing impairment | Visual indexer continues to work without audio; no degradation |
| Embedded `<iframe>` can't `requestPermission` on some browsers | The enable button can live in the parent docs page rather than the iframe; `postMessage` the granted state in |

## Pages affected

- `docs/site/docs/installation/external-display.md` — replace the existing iframe + intro paragraph with a new "Enable tilt + tones" widget. Keep the synthetic-ramp fallback iframe accessible via a "Use the demo without tilt" link for visitors who decline the permission flow.
- `software/OnSpeed-M5-Display/sim/SimMain.cpp` — add `onspeed_set_aoa()` and `onspeed_tone_for_aoa()` C bridges.
- `software/OnSpeed-M5-Display/src/SerialRead.cpp` — `DUMMY_SERIAL_DATA` path consumes `g_simAoaDeg` instead of running its own ramp (preserve the ramp behind a build flag for the standalone native sim).
- `docs/site/docs/assets/sim/imu_input.js` (new), `docs/site/docs/assets/sim/audio_engine.js` (new), shell HTML updated to wire them in.

## Implementation order

1. **Web Audio engine standalone** (PR 1). Implement `audio_engine.js` driven by hardcoded `ToneCalc` calls in JS that mirror the C++ math. Verify the pulse rates and frequencies sound right against a real OnSpeed box recording.
2. **C bridge for `calculateTone`** (PR 2). Replace the JS-mirrored math with WASM calls. Verify byte-identical tone decisions vs the firmware's `calculateTone()` output for a sweep of test inputs.
3. **AOA from JS** (PR 3). Replace the dummy ramp with `g_simAoaDeg` driven by a draggable slider. Verify the renderer + audio respond correctly to slider drags.
4. **DeviceOrientationEvent** (PR 4). Add the IMU input + permission-flow button. Test on iOS Safari, Chrome on Android, and desktop browsers.
5. **Docs page integration** (PR 5). Wire the widget into `external-display.md` with the fallback flow for visitors without IMU access.

Each PR is independently mergeable and ships a testable improvement.

## Success criteria

- A visitor on an iPhone at the docs page can tap "Enable", tilt their phone, and hear the 400 Hz / 1600 Hz tones at the correct PPS for the AOA region they've put themselves into.
- The audible tone is byte-equivalent to what the firmware produces for the same input AOA — same carrier frequencies, same PPS, same silence/pulse/solid behavior across all 5 tone regions.
- Visitor without phone IMU access (desktop, kiosk browser) still gets a working slider-driven demo.
- No regression in the existing visual-only embed for visitors who don't tap the enable button.

## Cost estimate

Wall-clock: 2–3 dev days plus iOS-device testing time. Five small PRs, each <200 LOC of new code. No firmware changes — all work is in the M5 sim and the docs site.

## Open questions

1. **Sample rate match:** the firmware synthesizes at 16 kHz from precomputed PCM; the browser's `OscillatorNode` outputs at the AudioContext rate (typically 48 kHz). Does the audible difference matter? Probably not (square wave is square wave), but worth a side-by-side listen before locking the design.
2. **Flap selector:** ship without (uses dummy frame's preset flap-0 numbers), or add a tiny dropdown for clean / approach / landing / full? Inclined to ship without; can add later.
3. **Volume control:** none, or single slider? Inclined to ship without — page-level "Stop tones" button covers the safety case (visitor in a quiet office).
4. **Live tone from a flight-log replay:** in scope as a follow-up, or a parallel project that piggybacks on this audio engine? Inclined: separate issue, share the audio engine via a stable JS API.
