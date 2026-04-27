# Spin Detection & Recovery Cue: Implementation Plan

**Status:** Draft, not committed. Background research compiled 2026-04-26.
**Tracking issue:** to be filed; companion to #324 (gOnsetRate gap).
**Scope:** Implement the `spinRecoveryCue` wire field that's been reserved
since Gen2 but never computed. Add display rendering on the M5 to make it
visible. Out of scope: any kind of automated recovery action.

---

## Why this plan exists

The OnSpeed `#1` display-serial protocol has a `spinRecoveryCue` field
(offset 86, width 2, encoded as `%+02d`, semantic range −1 / 0 / +1)
since the original Gen2 protocol design.  Both Gen2 and Gen3 firmware
declare it as `int spinRecoveryCue = 0;` in the function-local scope of
the display-serial builder, never write to it elsewhere, and ferry the
literal zero into the sprintf.  Neither generation's M5 firmware
contains render code for it — declared as `extern int SpinRecoveryCue;`
in `SerialRead.h`, written from the wire in `SerialRead.cpp::Inject`,
read by no consumer.

So the field is reserved on the wire, plumbed through both producer
and consumer globals, and silent at both ends.  Pure aspirational
infrastructure inherited from a long-ago design intent.

This plan describes how to wake it up.

---

## Prior art: the F/A-18 Hornet Spin Recovery Mode (SRM)

OnSpeed's aural-tone DNA descends from the F-4 Phantom AOA system that
Vac Vaccaro flew on his way to NTPS, and the human-factors thinking on
spin cues in fast jets crystallised around the F/A-18 SRM.  The
F/A-18's algorithm is the cleanest published prior art for what
OnSpeed's `spinRecoveryCue` was trying to be.

### F/A-18 SRM at a glance (OFP v10.7, current logic)

Detection gate:

```
arrow displayed when:
    lagged(yaw_rate)         > 17°/s     // direction-determining filter
  AND instantaneous(yaw_rate) > 17°/s     // confirms it's still happening
  AND airspeed                < 120 ± 15 KCAS
```

Direction logic:

```
upright spin   (Nz > 0):  arrow points OPPOSITE the yaw direction
inverted spin  (Nz < 0):  arrow points WITH the yaw direction
```

What the arrow tells the pilot:

```
"deflect the lateral stick in this direction"
NOT "this is the direction you are spinning"
```

That last point is load-bearing.  The arrow encodes the upright/inverted
discrimination *for the pilot*, so a disoriented pilot in a developed
spin doesn't have to mentally invert the meaning under stress.

### The 7.2-second filter and "chasing arrows"

Earlier OFP versions used a 15°/s threshold with light filtering, which
caused the arrow to flicker around the threshold.  Pilots followed the
flickering arrow with reciprocal lateral-stick inputs, *delaying*
recovery.  v10.7 added a 7.2 s lag filter on yaw rate plus the
combined "filtered AND instantaneous" gate so the arrow doesn't
appear on momentary excursions and can't disappear mid-spin.

### The falling-leaf gotcha

The F/A-18 community learned by hard experience that the falling-leaf
out-of-control mode produces yaw rates above the SRM threshold but is
not a spin and SRM is the wrong recovery for it.  Pilots
mis-engaged the auto-spin-recovery on a falling leaf, prolonging the
event.  The lesson for any GA implementation:

> A yaw-rate-only detector will misclassify some non-spin
> out-of-control states.  Add an AOA gate to fix this — a wing that
> isn't stalled cannot be in a spin, even if it's yawing fast.

### Garmin ESP and Cirrus Vision Jet — what *not* to copy

Both modern GA envelope-protection systems explicitly do not attempt
automatic spin recovery:

- Garmin ESP: a documented failure mode is "ESP-on-during-stall →
  autopilot lifts the low wing → perfect spin entry."  CFIs are
  trained to disable ESP before maneuvering.
- Cirrus Safe Return / Vision Jet: uses CAPS (the airframe parachute)
  as the escape route, not algorithmic recovery.

The takeaway: **OnSpeed's job is the *cue*, not the *recovery*.**  The
spin field always was a display element — point the pilot at the
correct lateral-stick deflection and stop there.  Don't build
automated recovery; the GA airframe envelope is too narrow and the
control authority too variable for it to be safe.

---

## Proposed OnSpeed algorithm (GA-tuned)

Three deltas from the F/A-18 baseline:

1. **Add an AOA gate.**  OnSpeed already knows whether the wing is
   stalled (the active flap snapshot has `fSTALLAOA` and
   `fSTALLWARNAOA`).  Gate the cue on `AOA > fSTALLWARNAOA`.  This
   eliminates falling-leaf-style misclassification for free, since
   OnSpeed has the AOA primary signal that the F-18 SRM doesn't.

2. **Lower the yaw-rate threshold and shorten the filter.**  GA
   incipient spins develop and recover on a much faster timescale
   than fighter departures.  Steady-state GA spin yaw rates run
   60–180°/s, but the *incipient* phase rate at the moment of
   departure is more like 20–40°/s.  Threshold 20°/s with a 1–2 s
   lag filter (vs F-18's 17°/s + 7.2 s) is the right ballpark to
   start from.  Vac would be the right person to nail this number
   from his test-pilot experience.

3. **Drop the airspeed gate.**  The F-18's 120 ± 15 kt gate exists
   because the airframe has legitimate post-stall maneuvering above
   that speed.  GA aircraft spin only when the wing is stalled, by
   definition; the AOA gate above already covers the speed range
   implicitly.

### Sketch in pseudocode

```c
// Constants (initial values, expect to tune per airframe)
const float YAW_THRESHOLD_DPS    = 20.0f;
const float YAW_HYSTERESIS_DPS   =  5.0f;
const float YAW_FILTER_TAU_SEC   =  1.0f;

// State (init to zeros at boot)
float yaw_filtered = 0.0f;
bool  spin_active  = false;

// Single-pole IIR on yaw rate.  dt is the AHRS task period, ~ 1/208 s.
const float alpha = dt / (YAW_FILTER_TAU_SEC + dt);
yaw_filtered += alpha * (yaw_now - yaw_filtered);

// Detection — must satisfy both filtered and instantaneous gates AND
// must satisfy the AOA gate (wing actually stalled).
const FOSConfig::SuFlaps& cfg = activeFlapSnapshot;
bool wing_stalled    = (aoa_deg > cfg.fSTALLWARNAOA);
bool yaw_now_fast    = (fabsf(yaw_now)      > YAW_THRESHOLD_DPS);
bool yaw_filt_fast   = (fabsf(yaw_filtered) > YAW_THRESHOLD_DPS);

// Latch-with-hysteresis to prevent arrow flicker.
if (!spin_active && wing_stalled && yaw_now_fast && yaw_filt_fast) {
    spin_active = true;
} else if (spin_active && fabsf(yaw_filtered) <
                          (YAW_THRESHOLD_DPS - YAW_HYSTERESIS_DPS)) {
    spin_active = false;
}

// Direction.  Use the lagged rate — its sign is stable through the
// developed phase even when the instantaneous rate jitters.
int cue = 0;
if (spin_active) {
    // Upright vs inverted via vertical-G sign.
    bool inverted = (vertical_g < 0.0f);
    int  yaw_sign = (yaw_filtered > 0.0f) ? +1 : -1;  // +1 = nose right
    cue = inverted ? yaw_sign : -yaw_sign;            // pilot stick direction
}
spin_recovery_cue = cue;   // wire field semantics: -1/0/+1 = left/none/right
```

### Once latched, do NOT change direction mid-spin

Critical rule from the F-18 "chasing arrows" history: **once the cue
latches a direction, do not flip it until either yaw drops below the
lower hysteresis threshold OR the wing un-stalls.**  Pick a direction
at latch time and stay there.  Better-wrong than flickering.

### Falling-leaf guard

Optional v0.5 enhancement: also require AOA to be high *and not
changing rapidly* — i.e. `|d(AOA)/dt| < some_threshold` — to confirm
"developed stall" rather than "post-stall thrash."  Without this guard,
violent post-stall pitch oscillations could occasionally clip the
threshold.  Defer until after first flight tests show whether it's
needed.

---

## Wire format (no change required)

`spinRecoveryCue` is already on the wire at offset 86, width 2,
`%+02d` formatted, semantic range −1/0/+1.  No protocol bump needed
to ship the producer-side computation.

The M5 already plumbs the value to a global `SpinRecoveryCue` in
`SerialRead.cpp::Inject`.  The only firmware change beyond the
producer is that the M5's display code needs to actually draw
something when `SpinRecoveryCue != 0`.

---

## M5 display design

The cue must be readable in 100 ms by a disoriented pilot.  Subtle is
worse than nothing.  Recommended render:

- A large, bright arrow overlaid in the center of the AOA indexer
  band when `SpinRecoveryCue != 0`.
- Color: red, flashing (existing stall-warn flash cadence is fine).
- Direction: `+1` → arrow points right (push lateral stick right);
  `-1` → arrow points left.
- Audio: existing stall-warn audio is already at maximum gain when
  AOA is past stall warn; reuse it.  No need for a separate spin
  tone.

For the LiveView page: the same convention — large overlay arrow.
WebSocket already has the option to add a JSON field if useful, but
the cue can also be derived browser-side from the AOA + yaw-rate fields
already in the JSON.

---

## What goes in the calibration config?

Open question: should `YAW_THRESHOLD_DPS` and `YAW_FILTER_TAU_SEC` be
per-airframe config values?

Arguments for: an RV-4 spin develops faster than an RV-10 spin.  The
threshold may need tuning.

Arguments against: the failure mode of an over-tuned threshold is
"cue doesn't fire when it should," which is recoverable by the pilot;
the failure mode of a per-airframe knob is "user mis-configures the
knob and gets a worse result than the default."

**Recommendation:** ship with a single fixed default for v0; revisit
per-airframe config only if flight test reveals a real need.

---

## Test plan

1. **Unit tests** (native, in `test/test_spin_detect/`):
   - Arrow off when not stalled, regardless of yaw rate.
   - Arrow off when stalled but yaw rate below threshold.
   - Arrow on, correct direction, when stalled and yaw rate above
     threshold (test both signs and both upright + inverted).
   - Hysteresis: arrow doesn't flicker as yaw rate oscillates around
     the threshold.
   - Latch direction: arrow direction does not change once set, even
     if yaw rate flips sign briefly.
   - Filter behavior: instantaneous yaw rate spike doesn't trigger
     until filtered rate also passes threshold.

2. **Bench replay**: synthesize a few spin profiles (CSV logs from
   real spin flight tests if available; synthetic otherwise) and feed
   through the m5-replay pipeline, eyeball the M5 display.

3. **Flight test**: Vac signs off.  Don't ship without this.  The
   threshold values above are reasonable engineering guesses, not
   flight-tested numbers.

---

## Implementation steps

1. Create issue: "Firmware: implement spin detection and populate
   spinRecoveryCue" (companion to #324).  Link this plan.
2. Add `SpinDetector` to `software/Libraries/onspeed_core/src/sensors/`
   — pure-core class with `Update(dt, yaw_dps, aoa_deg, vert_g,
   stall_warn_aoa)` returning `int cue` in {-1, 0, +1}.  Hold the
   filtered yaw rate, hysteresis state, latched direction.
3. Add `test/test_spin_detect/` covering the cases above.
4. Wire it into the firmware in `Housekeeping.cpp` (or a new
   `SpinTask`) running at 50 Hz off the AHRS output, write to a
   global the same way `g_iDataMark` is written.  Read in
   `DisplaySerial::Write` under `xAhrsMutex`.
5. Replace the hardcoded `inputs.spinRecoveryCue = 0;` in
   `DisplaySerial.cpp` with the real value.
6. Add render code to M5 `main.cpp` — a `if (SpinRecoveryCue != 0)`
   block in the display draw path, drawing a flashing arrow overlay.
7. Optional: add to LiveView (WebSocket JSON + `html_liveview.h`
   render path).  This is small.
8. Bench test.  Flight test.  Vac signs off.  Ship.

---

## Non-goals

- **No automatic recovery actions.**  The cue is a display element.
- **No spin-mode-specific audio.**  Existing stall-warn audio is
  already saturating.
- **No self-defending behavior** (e.g. "auto-mute the audio so the
  pilot can hear ATC").  Out of scope.

---

## Open questions to resolve before ship

- Final yaw-rate threshold (target 20°/s; needs Vac's read).
- Final filter time constant (target 1.0 s; needs flight test).
- Whether to add the falling-leaf guard from day 1 or defer.
- Whether the threshold needs to scale with flap setting (probably
  not — flap setting changes stall AOA, not spin yaw rate, and the
  AOA gate already references flap-specific stall AOA).
- M5 render: arrow shape / size / position (Mode-by-Mode question
  — the indexer modes already have different overlay positions).

---

## References

- F/A-18 Controls Released Departure Recovery Flight (DTIC ADA256522)
- Development of F/A-18 Spin Departure Demonstration (UTenn thesis,
  trace.tennessee.edu/cgi/viewcontent.cgi?article=3738&context=utk_gradthes)
- F/A-18A-D FCC OFP Versions 10.6.1+ (UTenn thesis,
  trace.tennessee.edu/cgi/viewcontent.cgi?article=3811&context=utk_gradthes)
- Susceptibility of F/A-18 Flight Controllers to the Falling-Leaf Mode
  (AIAA J. Guidance, doi:10.2514/1.50675)
- USN/USMC team pinpoints F-18 falling leaf rescue maneuver
  (Flight Global)
- NATOPS Flight Manual F/A-18A/B/C/D
- Garmin ESP overview + SAFE blog "Disable Garmin ESP Before
  Maneuvering"
- Cirrus Vision Jet Safe Return
- APS All-Attitude Upset Recovery Strategy (AAURS)
- PARE recovery technique
- Mike Vaccaro NAFI Mentor session on AOA basics
