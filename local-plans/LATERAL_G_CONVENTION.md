# Lateral-G sign convention across OnSpeed surfaces

**Status:** Active reference document. Update with every change to any of the surfaces below.

This document is the single source of truth for what positive vs. negative lateral G means in OnSpeed code. Reference-frame confusion has bitten us at least twice (the synth-record `from_log` bug in commit `a81ca41`, and a misdiagnosed scenario in `spin_recovery.py`). When you're working on anything that touches a `lateralG` / `LateralG` / `lateral_g` field, **read this first.**

---

## TL;DR — current state (PR #386 / v4.23, May 2026)

**Body-frame everywhere. Slip-skid ball renderers negate locally at the rendering site.**

PR #386 merged the wire-format change that makes every transport agree on body-frame.  The pre-v4.23 split (wire = ball-frame, everything else = body-frame) is gone.  Issue #374 is closed.

| Surface | Field | Frame | Positive means |
|---|---|---|---|
| IMU raw | `g_pIMU->Ay` | body | airframe accel **rightward** |
| AHRS smoothed | `g_AHRS.AccelLatFilter` | body | airframe accel **rightward** |
| SD log column | `LateralG` | body | airframe accel **rightward** |
| WebSocket JSON | `lateralGLoad` | body | airframe accel **rightward** |
| `#1` wire frame field | `lateralG` (offset 26) | body | airframe accel **rightward** |
| `LiveSnapshot.lateral_g` | `lateral_g` | body | airframe accel **rightward** |
| M5 `Slip` (after `SerialRead::SerialProcess` negates) | `Slip = -LateralG × 850` | screen | ball drawn **right** of center |
| LiveView `slipBall.js` | `slip = -lateralGLoad × 850` | screen | ball drawn **right** of center |

**Rule for scenario authors:** set `lateral_g` in body-frame (positive = airframe accelerating rightward).
- **Right spin / right yaw** → airframe pulled rightward → `lateral_g = +0.40` → wire +ve → M5 negates → `Slip < 0` → ball drawn **left**.
- **Left spin / left yaw** → airframe pulled leftward → `lateral_g = -0.40` → wire -ve → M5 negates → `Slip > 0` → ball drawn **right**.

The "TL;DR — pre-v4.23" section below is preserved for historical context only.  Do not consult it for current behavior.

---

## TL;DR — pre-v4.23 (HISTORICAL)

**Body-frame everywhere except the `#1` wire (M5 serial protocol).**

| Surface | Field | Frame | Positive means |
|---|---|---|---|
| IMU raw | `g_pIMU->Ay` | body | airframe accel **rightward** |
| AHRS smoothed | `g_AHRS.AccelLatCorr` | body | airframe accel **rightward** |
| SD log column | `LateralG` | body | airframe accel **rightward** |
| WebSocket JSON | `lateralGLoad` | body | airframe accel **rightward** |
| `#1` wire frame field | `lateralG` (offset 26) | **ball** ← **odd one out** | airframe accel **leftward** |
| M5 internal `LateralG` | (consumed from wire) | ball | airframe accel **leftward** |
| M5 `Slip` (pixel offset) | `Slip = LateralG × 850` | screen | **right** of center |
| LiveView `slipBall.js` | `slip = -lateralG × 850` | screen (after re-negation) | **right** of center |

The `#1` wire diverges because the firmware producer (`DisplaySerial.cpp::Write`) negates `AccelLatCorr` before encoding. That's a historical convention. Issue [#374](https://github.com/flyonspeed/OnSpeed-Gen3/issues/374) tracks the desired consolidation.

---

## How the ball physics works (the only physics worth memorizing)

Imagine a free weight in a curved tube mounted laterally in the cockpit. The tube is fixed to the airframe. The weight is free.

**In a coordinated turn:** centripetal force on the airframe is balanced by gravity (into the seat); the ball sits centered.

**In an uncoordinated turn or spin:** centripetal force pulls the airframe one way; the ball, by inertia, lags the *other* way relative to the airframe. The pilot reads this as "ball goes outside the turn."

**Pilot mnemonic — "step on the ball":** press the rudder pedal *on the side the ball moved*. That's anti-yaw rudder; it arrests whatever yaw is happening.

So:

- **Right yaw / right spin** → airframe centripetal force is **rightward** → ball lags **left** → press **left** rudder (cue = -1).
- **Left yaw / left spin** → airframe centripetal force is **leftward** → ball lags **right** → press **right** rudder (cue = +1).

This is the only chain that has to be true. Every code-level convention exists to make this chain produce the right rendered ball position and right cue arrow.

---

## How to flow data correctly through each surface

### Reading from a log column (today)

```python
log_LateralG = float(row["LateralG"])   # body-frame, +rightward
# To put on the wire, negate:
wire_lateralG = -log_LateralG           # ball-frame, +leftward
# To draw the ball:
slip_pixel_x = wire_lateralG * 850      # +pixel = right of center
```

### Reading from the WebSocket (today)

```js
const lateralGLoad = msg.lateralGLoad;  // body-frame, +rightward
// To draw the ball:
const slipPixelX = -lateralGLoad * 850; // +pixel = right of center
                                        // (negate because consumer wants ball-frame)
```

### Authoring a synthetic scenario (today, going onto the `#1` wire)

```python
# We're populating LiveSnapshot.lateral_g, which goes onto the #1 wire's
# `lateralG` field unchanged.  Wire convention is BALL-FRAME, +leftward.

# Right spin (airframe pulled rightward):
state.yaw_rate   = +80.0      # body-frame, +nose-right
state.lateral_g  = -0.40      # ball-frame, -ve = airframe pulled rightward
                              # Result: M5 Slip < 0, ball drawn LEFT

# Left spin (airframe pulled leftward):
state.yaw_rate   = -80.0
state.lateral_g  = +0.40      # ball-frame, +ve = airframe pulled leftward
                              # Result: M5 Slip > 0, ball drawn RIGHT
```

### M5 (today, consumer of the wire)

```cpp
Slip = LateralG * 850;        // wire is ball-frame; multiply directly
                              // +Slip = ball drawn right of center
```

---

## The trip-wire test (run this when you're not sure)

When in doubt, ask **"in a right spin, what should each value be?"** Right spin means:

- **yaw_rate**: positive (+80°/s, nose-right)
- **roll**: positive (right wing down) or zero (depending on phase)
- **wire `lateralG`** (today): **negative** (centripetal accel was rightward in body frame; wire ball-frame negates → negative)
- **M5 `Slip`** (today): negative → ball drawn **left**
- **Cue value**: `-sign(yaw_filtered)` = `-1` → press **LEFT** rudder
- **All four** of {ball position, cue arrow direction, lit pedal, RUDDER text color side} must agree.

If your scenario produces a right spin and the cue says "press right," you have a sign bug.

---

## Future state (after issue #374 lands)

Once the wire convention is consolidated to body-frame:

| Surface | Field | Frame | Positive means |
|---|---|---|---|
| IMU raw | `g_pIMU->Ay` | body | airframe accel rightward |
| AHRS smoothed | `g_AHRS.AccelLatCorr` | body | airframe accel rightward |
| SD log column | `LateralG` | body | airframe accel rightward |
| WebSocket JSON | `lateralGLoad` | body | airframe accel rightward |
| **`#1` wire frame field** | `lateralG` | **body** ← changed | airframe accel rightward |
| M5 internal `LateralG` | (consumed from wire) | body | airframe accel rightward |
| M5 `Slip` (pixel offset) | `Slip = -LateralG × 850` | screen (after negation) | right of center |
| LiveView `slipBall.js` | `slip = -lateralG × 850` | unchanged | right of center |

**One frame to rule them all.** Every transport ships body-frame; every display consumer negates locally. The only knowledge an author needs is "body-frame: positive = airframe accelerating rightward." The display layer handles ball physics in one place per consumer.

After #374 lands, the synth-record scenarios change as follows:

```python
# Right spin (after #374):
state.yaw_rate   = +80.0      # unchanged: body-frame +nose-right
state.lateral_g  = +0.40      # changed sign: now body-frame +rightward
                              # Wire field carries +0.40 directly (no negation)
                              # M5: Slip = -LateralG × 850 = -340 → ball drawn LEFT ✓
```

**The `_log_to_wire_lateral_g` negation in `from_log.py` disappears** — log is body-frame, wire is body-frame, no conversion. **`LiveSnapshot.lateral_g` switches its convention to body-frame.** Synthetic scenarios flip lateral_g signs (all of them, in lockstep) to match.

---

## Checklist for any PR that touches lateral G

Before reviewing or filing any PR that mentions `lateralG`/`LateralG`/`lateral_g`, answer:

- [ ] Which surface is this code on (IMU, AHRS, log, WebSocket, wire, M5, LiveView, synth-record)?
- [ ] Which frame does that surface use (per the table above)?
- [ ] Is this code reading or writing the value? Does it need a negation to cross frames?
- [ ] If writing into the `#1` wire, is the value in ball-frame? (Today: yes; after #374: body-frame.)
- [ ] In the trip-wire test (right spin), do all four of {ball position, cue arrow, lit pedal, rudder text} agree?

If any answer is "I don't know," stop and read this document until they're all answered.

---

## Documents this supersedes / consolidates

- The inline comment block in `tools/synth-record/live_snapshot.py::lateral_g` (was the previous canonical reference; now points here).
- The convention table in issue #374 (kept there for context; this doc has the up-to-date version).
- Scattered comments in `from_log.py::_log_to_wire_lateral_g`, `slipBall.js`, `DisplaySerial.cpp::Write`, `SerialRead.cpp::SerialProcess`. All of those should reference this document.

When this document changes, update the references.
