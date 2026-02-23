# Flight Envelope (V-n Diagram)

The V-n diagram maps every combination of airspeed and load factor (G) your aircraft can experience. It shows where the aerodynamic limits are, where structural limits are, and — with OnSpeed tone regions overlaid — where each tone lives in the flight envelope.

<div id="vn-diagram" markdown>

<div class="vn-controls" markdown>
<div class="vn-control">
<label for="vn-weight">Weight:</label>
<input type="range" id="vn-weight" min="900" max="1375" step="5" value="1375">
<span class="vn-val" id="vn-weight-val">1375 lb</span>
</div>
<div class="vn-control">
<label for="vn-bank">Bank angle:</label>
<input type="range" id="vn-bank" min="0" max="75" step="1" value="0">
<span class="vn-val" id="vn-bank-val">0°  (1.00 G)</span>
</div>
<div class="vn-control">
<input type="checkbox" id="vn-show-tones" checked>
<label for="vn-show-tones">Tone regions</label>
</div>
<div class="vn-control">
<input type="checkbox" id="vn-show-tp" checked>
<label for="vn-show-tp">Traffic pattern</label>
</div>
<div class="vn-control">
<input type="checkbox" id="vn-show-point">
<label for="vn-show-point">"You are here" point</label>
</div>
</div>

</div>

## Reading the Diagram

### The Aerodynamic Limit (Stall Boundary)

The red parabola on the left is the **stall boundary** — the lowest speed at which the wing can sustain a given load factor. It follows $V_s = V_{s_{1G}} \times \sqrt{n}$, which means stall speed increases with the square root of load factor.

Key points:

- At **1G** (straight and level), the stall boundary crosses at the aircraft's clean stall speed
- At **2G** (a 60° bank turn), stall speed is 41% higher
- At **4G**, stall speed doubles
- Below and left of this curve, the wing **cannot produce enough lift** — you are stalled

### Tone Regions

The colored bands between the AOA curves show where each OnSpeed tone lives at every G-loading:

| Region | Color | NAOA Range | Tone |
|--------|-------|-----------|------|
| **Stall Warning** | Red | 90–100% | Rapid 20 Hz buzz |
| **Slow Tone** | Yellow | 64–90% | High-pitched pulsing, speeds up as you slow |
| **ONSPEED** | Green | 59–64% | Solid low tone — hold this |
| **Fast Tone** | Blue | 55–59% | Low-pitched pulsing, speeds up as you slow |
| **Silent** | (none) | Below 55% | No tone |

Notice that these bands follow the same parabolic shape as the stall boundary. At higher G (in a turn, pull-up, or turbulence), the tone region boundaries shift to higher airspeeds — **but your OnSpeed tones automatically track this shift**. The tone you hear always reflects your actual proximity to stall, regardless of G-loading.

### Structural Limits

The hatched areas represent conditions beyond the aircraft's structural certification:

- **Top**: Above the positive G limit (+6.0G for the RV-4, aerobatic category)
- **Bottom**: Below the negative G limit (-3.0G)
- **Right**: Beyond V~NE~ (never exceed speed)

The yellow-shaded band between V~NO~ and V~NE~ is the **caution range** — speeds above the maximum structural cruising speed where flight should be limited to smooth air. Beyond V~NE~, the red hatching indicates structural limits reserved for the designer, not the pilot.

### Traffic Pattern Box

The red dashed box highlights the traffic pattern — the small region of the overall envelope where takeoff, landing, and pattern operations occur. This area lies uncomfortably close to the aerodynamic limit, which is why accurate AOA feedback is critical during low-altitude maneuvering.

## Key Insights

!!! tip "Stall speed is not a fixed number"
    Move the **bank angle** slider to 60°. Watch the stall boundary shift right — stall speed increases from 62 to 88 knots (at max gross). In a steep turn close to the ground, the margin between flying and stalling shrinks dramatically. OnSpeed tones account for this automatically because they're based on AOA, not airspeed.

!!! warning "AOA changes faster than you think"
    Look at how close together the tone region bands are at higher G-loadings. At 3G, the entire range from "Fast" silence through the ONSPEED band to stall warning spans only about 30 knots. A bump of turbulence or a slight pull on the stick can jump you through multiple tone regions.

!!! tip "Weight shifts everything"
    Move the **weight** slider from 1375 lb down to 1000 lb. All the parabolas shift left — stall speed drops, and so do all the tone boundaries. Lighter aircraft have more margin at any given airspeed. This is why AOA-based tones are more useful than a fixed V-speed bug: the tones automatically adapt.

!!! info "Maneuvering speed and the G-limit intersection"
    Follow the stall boundary parabola upward. Where it crosses the +G limit line is the **maneuvering speed (V~A~)**. Below this speed, the wing stalls before the structure breaks — full control deflection is safe. Above it, you can overstress the aircraft before the wing stalls.

!!! note "The 'You are here' point"
    Enable the **"You are here" point** checkbox and drag the red dot around the diagram. The info panel shows your computed NAOA, active tone region, and stall margin at that airspeed and G-loading. Try dragging it along the 1G line from fast to slow and watch the tone region progress match what you hear in the cockpit.

## The Physics

Each constant-AOA curve on the diagram follows the relationship:

$$V = V_{s_{1G}} \times \sqrt{\frac{n}{\text{NAOA}}}$$

where NAOA is the normalized angle of attack (fractional lift). The stall boundary is simply the curve where NAOA = 1.0 (100% of maximum lift).

Weight affects all curves through $V_{s_{1G}}$:

$$V_{s_{1G}}(W) = V_{s_{ref}} \times \sqrt{\frac{W}{W_{ref}}}$$

And bank angle determines the required load factor to maintain level flight:

$$n = \frac{1}{\cos(\phi)}$$

For the full derivation of how OnSpeed extracts these parameters from flight data, see [How OnSpeed Measures AOA](../calibration/how-aoa-works.md).

## Reference Values (RV-4)

| Parameter | Value |
|-----------|-------|
| V~S~ (clean, max gross) | 62 KIAS |
| Max gross weight | 1375 lb |
| V~NO~ | 165 KIAS |
| V~NE~ | 200 KIAS |
| V~D~ (design dive) | 230 KIAS |
| +G limit | +6.0 |
| -G limit | -3.0 |
