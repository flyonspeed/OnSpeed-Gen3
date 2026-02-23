# Setpoint Tuning UI Design

**Goal:** After calibration, let the pilot tap setpoints up/down in meaningful increments, seeing both the equivalent IAS (×Vs multiplier) and the raw AOA value — instead of editing opaque numbers.

---

## Current State

The settings page shows each setpoint as a raw AOA text field:

```
L/Dmax AOA:        [  4.10  ]  [Use Live AOA]
OnSpeed Fast AOA:  [  4.08  ]  [Use Live AOA]
OnSpeed Slow AOA:  [  4.95  ]  [Use Live AOA]
Stall Warning AOA: [  7.98  ]  [Use Live AOA]
```

Problems:
1. **Numbers are meaningless to pilots.** "4.10" degrees AOA tells you nothing about how fast you'll be flying.
2. **No reference to stall speed.** Is OnSpeedSlow at 1.2×Vs or 1.4×Vs? You can't tell.
3. **No incremental adjustment.** To make it "a little faster," you have to guess which direction to change the number and by how much.
4. **No guard rails.** Easy to accidentally put LDMAX above OnSpeedFast.

## What We Want

For each setpoint, show:
- The current **×Vs multiplier** (e.g., "1.30×Vs") — this is the primary thing pilots understand
- The equivalent **IAS in knots** at calibration weight (e.g., "~81 kt")
- **Tap up/down** buttons that move the setpoint by a fixed NAOA step (maps to ~0.01× change in Vs multiplier)
- The raw AOA value still visible but secondary

---

## Physics: How It All Connects

The calibration wizard fits the lift equation: `DerivedAOA = K / IAS² + alpha_0`

This produces three quantities per flap setting:
- **K** — lift sensitivity (deg·kt²). Encodes the wing's lift-curve slope, probe installation geometry, and calibration weight. This is the primary parameter from the fit.
- **alpha_0** — zero-lift fuselage AOA (deg). Typically negative.
- **alpha_stall** — stall AOA (deg). Derived from K at stall speed: `alpha_stall = K / Vs² + alpha_0`

K is per-flap because each flap setting is aerodynamically a different wing — different CL_alpha, different zero-lift angle, different stall AOA.

### K is the primary stored quantity

**K encodes the full IAS↔AOA relationship.** Given K, alpha_0, and alpha_stall (all already per-flap), everything else is derived:

```
Vs  = sqrt(K / (alpha_stall - alpha_0))        # stall speed at cal weight
IAS = sqrt(K / (AOA - alpha_0))                 # IAS at any AOA
AOA = K / IAS² + alpha_0                        # AOA at any IAS
```

Storing Vs instead of K would only give you a single speed (at stall). K gives you the speed at *every* setpoint — which is exactly what the tuning UI needs. You can't go from Vs back to K without also knowing alpha_stall and alpha_0, which makes Vs the derived quantity:

```
K = (alpha_stall - alpha_0) × Vs²              # if you had Vs
Vs = sqrt(K / (alpha_stall - alpha_0))          # derived from K
```

K is also what the calibration wizard actually fits (it's the slope of the DerivedAOA vs 1/IAS² regression). Storing K means the config preserves the primary fit result.

### Multiplier and NAOA

The ×Vs multiplier and normalized AOA (NAOA) are two views of the same thing:

```
NAOA = (AOA - alpha_0) / (alpha_stall - alpha_0)     # normalized 0..1
multiplier = 1 / sqrt(NAOA)                           # ×Vs
IAS = multiplier × Vs = sqrt(K / (AOA - alpha_0))     # knots (at cal weight)
```

And the inverse:
```
multiplier → NAOA = 1 / multiplier²
NAOA → AOA = NAOA × (alpha_stall - alpha_0) + alpha_0
```

### Weight correction (future)

K bakes in the calibration weight: `K = W_cal / (½ρS × CL_alpha)`. At a different weight, the actual IAS shifts:

```
IAS_actual = IAS_cal × sqrt(W_actual / W_cal)
```

This is a simple post-hoc correction. The NAOA fractions and ×Vs multipliers are weight-independent — only the IAS column changes. For V1 we show IAS at calibration weight. A future "current weight" input could adjust the display.

---

## Design: Enhanced Setpoint Row

Each setpoint becomes a richer row. Here's the layout for one (e.g., OnSpeed Fast):

```
┌─────────────────────────────────────────────────────────┐
│  OnSpeed Fast                                           │
│                                                         │
│   [−]  1.35 ×Vs  (~84 kt)  [+]     AOA: 11.25°        │
│                                                         │
└─────────────────────────────────────────────────────────┘
```

- **[−] / [+]** buttons adjust the ×Vs multiplier by ±0.01 (about 0.5 kt per tap at typical Vs)
- **1.35 ×Vs** is the primary display — big, readable
- **(~84 kt)** is computed from `sqrt(K / (AOA - alpha_0))`, shown as secondary info
- **AOA: 11.25°** is the raw value, shown smaller for reference
- The hidden form field still submits the raw AOA value

### When K/alpha_0/alpha_stall are missing (legacy configs)

If `fAlpha0 == 0.0 && fAlphaStall == 0.0` (un-calibrated or legacy config), the physics model can't compute multipliers. In this case:

```
┌─────────────────────────────────────────────────────────┐
│  OnSpeed Fast                                           │
│                                                         │
│   [−]  AOA: 4.08°  [+]     (calibrate for ×Vs display) │
│                                                         │
└─────────────────────────────────────────────────────────┘
```

- **[−] / [+]** still work, but adjust the raw AOA by ±0.10° per tap
- A note says "calibrate for ×Vs display" to encourage running the wizard
- No multiplier or IAS shown — we don't have the physics model to compute them
- Still much better than a raw text field for incremental adjustment

### Detection of legacy vs calibrated config

```javascript
function hasPhysicsModel(flapIdx) {
    // A valid calibration has alpha_stall > alpha_0 (typically alpha_stall
    // is 8-25° and alpha_0 is -10 to 0°) and K > 0.
    var a0     = parseFloat(document.getElementById('id_flapAlpha0' + flapIdx).value);
    var aStall = parseFloat(document.getElementById('id_flapAlphaStall' + flapIdx).value);
    var k      = parseFloat(document.getElementById('id_flapKFit' + flapIdx).value);
    return (aStall > a0 + 1.0) && (k > 0);
}
```

---

## Data Requirements

### What we need stored in config (SuFlaps)

Already present:
- `fAlpha0` — zero-lift fuselage AOA
- `fAlphaStall` — stall AOA from fit

**New field:**
- `fKFit` — lift sensitivity (deg·kt²) from IAS-to-AOA fit

Currently `K_fit` is computed during calibration (it's the slope of the `DerivedAOA` vs `1/IAS²` regression) but not saved to the config. We store it because:

1. It's the primary fit result — alpha_stall is *derived* from K at stall speed
2. It gives IAS at any AOA: `IAS = sqrt(K / (AOA - alpha_0))` — needed for the UI
3. Vs is trivially derived: `Vs = sqrt(K / (alpha_stall - alpha_0))`
4. It enables future weight correction without re-calibrating

### What we need in JavaScript (client-side computation)

All the math happens in the browser. No new firmware endpoints needed.

```javascript
// Derived stall speed from K
function kToVs(k, alpha0, alphaStall) {
    var alphaRange = alphaStall - alpha0;
    if (alphaRange <= 0 || k <= 0) return null;
    return Math.sqrt(k / alphaRange);
}

// IAS at any AOA (from the lift equation directly)
function aoaToIAS(aoa, alpha0, k) {
    var dAlpha = aoa - alpha0;
    if (dAlpha <= 0 || k <= 0) return null;
    return Math.sqrt(k / dAlpha);
}

// AOA at any IAS
function iasToAoa(ias, alpha0, k) {
    if (ias <= 0 || k <= 0) return null;
    return k / (ias * ias) + alpha0;
}

// Convert raw AOA to ×Vs multiplier (doesn't need K — only alpha_0/alpha_stall)
function aoaToMultiplier(aoa, alpha0, alphaStall) {
    var naoa = (aoa - alpha0) / (alphaStall - alpha0);
    if (naoa <= 0.01) return Infinity;
    return 1.0 / Math.sqrt(naoa);
}

// Convert ×Vs multiplier to raw AOA (doesn't need K either)
function multiplierToAoa(mult, alpha0, alphaStall) {
    var naoa = 1.0 / (mult * mult);
    return naoa * (alphaStall - alpha0) + alpha0;
}

// Tap handler: adjust multiplier by step, update AOA field and display
function tapSetpoint(flapIdx, setpointName, direction) {
    var alpha0     = getAlpha0(flapIdx);
    var alphaStall = getAlphaStall(flapIdx);
    var k          = getKFit(flapIdx);
    var aoaField   = document.getElementById('id_flap' + setpointName + flapIdx);
    var aoa        = parseFloat(aoaField.value);
    var step       = 0.01;  // multiplier step

    if (hasPhysicsModel(flapIdx)) {
        var mult = aoaToMultiplier(aoa, alpha0, alphaStall);
        mult += direction * step;  // +1 = faster (higher mult), -1 = slower
        if (mult < 1.0) mult = 1.0;  // can't go below stall
        aoa = multiplierToAoa(mult, alpha0, alphaStall);
    } else {
        aoa -= direction * 0.10;  // lower AOA = faster
    }

    // Clamp to maintain ordering
    var bounds = getAoaBounds(flapIdx, setpointName);
    aoa = Math.max(bounds.min, Math.min(bounds.max, aoa));

    aoaField.value = aoa.toFixed(2);
    updateSetpointDisplay(flapIdx, setpointName);
}

// Update the multiplier and IAS labels for one setpoint
function updateSetpointDisplay(flapIdx, setpointName) {
    var alpha0     = getAlpha0(flapIdx);
    var alphaStall = getAlphaStall(flapIdx);
    var k          = getKFit(flapIdx);
    var aoa        = parseFloat(document.getElementById('id_flap' + setpointName + flapIdx).value);

    var multEl = document.getElementById('id_mult_' + setpointName + flapIdx);
    var iasEl  = document.getElementById('id_ias_' + setpointName + flapIdx);

    if (hasPhysicsModel(flapIdx)) {
        var mult = aoaToMultiplier(aoa, alpha0, alphaStall);
        var ias  = aoaToIAS(aoa, alpha0, k);  // uses K directly
        multEl.textContent = mult.toFixed(2) + ' ×Vs';
        iasEl.textContent  = ias ? '(~' + Math.round(ias) + ' kt)' : '';
    } else {
        multEl.textContent = '';
        iasEl.textContent  = '(calibrate for ×Vs display)';
    }
}
```

Note that the multiplier math only needs alpha_0 and alpha_stall (dimensionless NAOA fractions). K is needed specifically for the IAS display — it's what converts the dimensionless multiplier into knots.

---

## Setpoint Ordering Enforcement

The [+] and [-] buttons **clamp** to maintain ordering. OnSpeedFast's [−] (slower direction / higher AOA) stops before it reaches OnSpeedSlow. OnSpeedSlow's [+] (faster direction / lower AOA) stops before it reaches OnSpeedFast.

```javascript
function getAoaBounds(flapIdx, setpointName) {
    var flap = getFlap(flapIdx);
    switch (setpointName) {
        case 'LDMAXAOA':
            return { min: -10, max: flap.ONSPEEDFASTAOA - 0.1 };
        case 'ONSPEEDFASTAOA':
            return { min: flap.LDMAXAOA + 0.1, max: flap.ONSPEEDSLOWAOA - 0.1 };
        case 'ONSPEEDSLOWAOA':
            return { min: flap.ONSPEEDFASTAOA + 0.1, max: flap.STALLWARNAOA - 0.1 };
        case 'STALLWARNAOA':
            return { min: flap.ONSPEEDSLOWAOA + 0.1, max: flap.STALLAOA - 0.1 };
    }
}
```

The clamping ensures `AreSetpointsOrdered()` can never be violated by the +/- buttons. The raw text field remains editable for power users, with the existing server-side warning on save.

---

## Stall and Maneuvering (Non-Adjustable)

**Stall AOA** and **Maneuvering AOA** are not pilot-adjustable in this UI:
- **Stall** = `alpha_stall` from the physics fit. This is a property of the airplane, not a preference.
- **Maneuvering** = AOA at Va = Vs × sqrt(G-limit). Also derived from physics.

These are shown as read-only info, with IAS computed from K:

```
┌─────────────────────────────────────────────────────────┐
│  Stall         AOA: 18.50°    (1.00 ×Vs = ~62 kt)     │
│  Maneuvering   AOA: 5.82°     (Va = ~98 kt)            │
└─────────────────────────────────────────────────────────┘
```

---

## Implementation Plan

### Step 1: Store K in config (firmware)

Add `fKFit` to `SuFlaps`:

```cpp
struct SuFlaps {
    // ... existing fields ...
    float fKFit;       // Lift sensitivity (deg·kt²) from IAS-to-AOA fit
};
```

- Default to `0.0` for backward compatibility
- Add to XML serialization (`<KFIT>` element in `<FLAP_POSITION>`)
- Save from calibration wizard (`K_fit` is already computed as `resultIAStoAOA.equation[0]`)

### Step 2: Add JavaScript helper functions

Add to the `<script>` block in `HandleConfig()`:

- `kToVs(k, alpha0, alphaStall)` → float (derived stall speed)
- `aoaToIAS(aoa, alpha0, k)` → float (IAS from K directly)
- `aoaToMultiplier(aoa, alpha0, alphaStall)` → float
- `multiplierToAoa(mult, alpha0, alphaStall)` → float
- `hasPhysicsModel(flapIdx)` → bool
- `updateSetpointDisplay(flapIdx, setpointName)` — refreshes the multiplier/IAS labels
- `tapSetpoint(flapIdx, setpointName, direction)` — handles +/- click with clamping

### Step 3: Modify the setpoint HTML rows

Replace each setpoint's simple `<input>` + `[Use Live AOA]` with the enhanced row:

```html
<div class="setpoint-row">
    <label>OnSpeed Fast</label>
    <div class="setpoint-controls">
        <button class="tap-btn" onclick="tapSetpoint(0,'ONSPEEDFASTAOA',-1)">−</button>
        <span class="multiplier-display" id="id_mult_ONSPEEDFASTAOA0">1.35 ×Vs</span>
        <span class="ias-display" id="id_ias_ONSPEEDFASTAOA0">(~84 kt)</span>
        <button class="tap-btn" onclick="tapSetpoint(0,'ONSPEEDFASTAOA',+1)">+</button>
    </div>
    <div class="aoa-raw">
        AOA: <input id="id_flapONSPEEDFASTAOA0" name="flapONSPEEDFASTAOA0"
                     type="text" value="11.25"
                     onchange="updateSetpointDisplay(0,'ONSPEEDFASTAOA')" />°
    </div>
    <button class="greybutton" onclick="FillInValue(...)">Use Live AOA</button>
</div>
```

### Step 4: Add K field to settings page

Show K as a read-only field alongside alpha_0 and alpha_stall in each flap section:

```html
<div class="form-divs flex-col-4">
    <label>K (lift sensitivity)</label>
    <input id="id_flapKFit0" name="flapKFit0" type="text" value="..." readonly />
</div>
<div class="form-divs flex-col-4">
    <label>Alpha-0 (zero-lift)</label>
    <input id="id_flapAlpha00" name="flapAlpha00" type="text" value="..." />
</div>
<div class="form-divs flex-col-4">
    <label>Alpha-Stall (from fit)</label>
    <input id="id_flapAlphaStall0" name="flapAlphaStall0" type="text" value="..." />
</div>
```

Below those, show derived Vs as info text: `Vs = √(K / (α_stall − α_0)) = 62.3 kt`

### Step 5: Initialize displays on page load

After the page renders, call `updateSetpointDisplay()` for each setpoint of each flap position to populate the multiplier/IAS labels. If the physics model isn't available, show the fallback.

### Step 6: CSS styling

Add minimal CSS for the new layout:

```css
.setpoint-row { display: flex; align-items: center; flex-wrap: wrap; gap: 8px; }
.tap-btn { width: 36px; height: 36px; font-size: 20px; border-radius: 4px; }
.multiplier-display { font-size: 18px; font-weight: bold; min-width: 80px; }
.ias-display { color: #666; font-size: 14px; }
.aoa-raw { font-size: 12px; color: #888; }
.aoa-raw input { width: 60px; font-size: 12px; }
```

The page already uses a flex grid system (`flex-col-4`, `flex-col-8`, etc.) so we can work within that.

---

## Summary of Changes

| Component | Change | Size |
|---|---|---|
| `Config.h` | Add `fKFit` to `SuFlaps`, default 0.0 | 3 lines |
| `Config.cpp` | Serialize/deserialize `<KFIT>` in XML | ~6 lines |
| `ConfigWebServer.cpp` | Save `K_fit` from wizard to `fKFit` | 1 line |
| `ConfigWebServer.cpp` | Enhanced setpoint rows with +/- and labels | ~80 lines HTML |
| `ConfigWebServer.cpp` | JavaScript helpers (K-based math, tap handler) | ~80 lines JS |
| `ConfigWebServer.cpp` | K field display + derived Vs readout | ~15 lines HTML |
| `ConfigWebServer.cpp` | CSS for setpoint row layout | ~15 lines |

No changes to: Audio, ToneCalc, AHRS, SensorIO, or any real-time firmware path. This is purely a config storage + web UI enhancement.

---

## Open Questions

### 1. Step size for +/- buttons

Proposed: ±0.01 in multiplier space (~0.5 kt per tap at Vs≈62). Should it be configurable? Should there be a "fine" and "coarse" mode?

### 2. The weight problem: K and the IAS display

This needs group discussion. The IAS shown in the UI comes from K, and K bakes in the calibration weight.

**The physics:** `K = W_cal / (½ρS × CL_alpha)`. When you calibrate at 2200 lbs, K encodes 2200 lbs. The Vs derived from K (`Vs = sqrt(K / (alpha_stall - alpha_0))`) is the stall speed *at that weight*. At 1800 lbs, the actual stall speed is lower: `Vs_actual = Vs_cal × sqrt(W_actual / W_cal)`.

**What's weight-independent:** The ×Vs multipliers and NAOA fractions don't care about weight. "1.30×Vs" means 1.30×Vs regardless of what Vs is. The AOA setpoints are also weight-independent — the wing stalls at the same angle regardless of weight (you just reach that angle at a lower speed when light).

**What changes with weight:** Only the IAS column. At 2200 lbs, OnSpeedFast at 1.35×Vs = ~84 kt. At 1800 lbs, it's ~76 kt. The AOA and the multiplier are the same in both cases.

**Options for the IAS display:**

**(a) Show IAS at calibration weight, label it clearly.** Something like "~84 kt @ 2200 lbs". This requires storing the calibration weight, which the wizard currently has as `acCurrentWeight` but doesn't save to the per-flap config. We'd add `fCalWeight` to SuFlaps.

Pros: Honest, gives a useful reference point, pilots can mentally adjust.
Cons: Adds a field that could get stale. Need to decide if each flap setting has its own cal weight (probably, if calibrated on different flights).

**(b) Show IAS at a user-entered "current weight."** Add a weight input to the settings page. Compute IAS on the fly: `IAS = sqrt(K × W_current / W_cal / (AOA - alpha_0))`.

Pros: Shows speeds relevant to right now.
Cons: The weight field is ephemeral — it's not something you'd persist in config (the whole point of AOA is that you *don't* need to know your weight). Feels brittle and misleading — pilots might set it once and forget to update it, then trust stale numbers.

**(c) Show only the ×Vs multiplier, no IAS.** The multiplier is always correct regardless of weight.

Pros: Simple, always accurate, no weight dependency.
Cons: Loses the "~84 kt" reference that makes it concrete. Pilots think in knots, not multipliers. The multiplier is especially useful when you're first setting up and want to sanity-check that your OnSpeed band corresponds to a reasonable approach speed range.

**(d) Show IAS from K without a weight label.** Just show "~84 kt" and accept that it's approximate — it's the speed at whatever weight you calibrated at.

Pros: Simple, useful reference. The approximation is honest enough — most GA pilots fly at roughly the same weight range.
Cons: Could mislead if someone calibrates at gross weight and then always flies light.

**Current leaning:** Probably (a) or (d). The ×Vs multiplier is the primary display either way. The IAS is secondary context — it's useful for sanity-checking but pilots shouldn't be *targeting* a specific IAS from this display (that defeats the purpose of AOA). We should note in the UI that the IAS is at calibration weight.

The calibration wizard already has `acCurrentWeight` — we should record it at calibration time regardless, so at minimum we have the provenance. Whether to show a weight-correction input in the settings page is a separate UI decision.

**Needs group input:** How do other OnSpeed users think about this? Is "~84 kt @ 2200 lbs" confusing or helpful? Would a weight input field be used or ignored?

### 3. What about LDmax?

LDmax is set from `acVldmax` (a fixed airspeed for the aircraft type), not from a multiplier. Should its display show the IAS directly (from K) rather than a ×Vs multiplier? LDmax in multiplier space is typically ~1.35×Vs but varies with aircraft.
