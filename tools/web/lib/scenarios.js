// Synthetic scenarios — each one is a function that takes elapsed milliseconds
// since the scenario started and returns a complete record matching the
// frameBuilder schema.

const RV10_FULL_FLAPS = {
  // From SerialRead.cpp:198-203: alpha_0 = -9.21, alpha_stall = 11.57.
  // L/Dmax = -2.24°, OnSpeedFast = 2.19°, OnSpeedSlow = 4.09°, StallWarn = 7.94°.
  // Convert to percent-lift: pct = (aoaDeg - alpha_0) / (alpha_stall - alpha_0) * 100.
  alpha0: -9.21,
  alphaStall: 11.57,
  aoaToPct(aoaDeg) {
    return Math.max(0, Math.min(99, Math.round((aoaDeg - this.alpha0) / (this.alphaStall - this.alpha0) * 100)));
  },
};

const ANCHORS_FULL_FLAPS = {
  tonesOnPctLift:     RV10_FULL_FLAPS.aoaToPct(-2.24),  // ~33
  onSpeedFastPctLift: RV10_FULL_FLAPS.aoaToPct( 2.19),  // ~55
  onSpeedSlowPctLift: RV10_FULL_FLAPS.aoaToPct( 4.09),  // ~64
  stallWarnPctLift:   RV10_FULL_FLAPS.aoaToPct( 7.94),  // ~83
  pipPctLift:         RV10_FULL_FLAPS.aoaToPct(-2.24),  // L/Dmax
};

function record(overrides) {
  return {
    pitchDeg: 0, rollDeg: 0, iasKt: 0, paltFt: 0,
    turnRateDps: 0, lateralG: 0, verticalG: 1.0, percentLift: 0,
    vsiFpm: 0, oatC: 70, flightPathDeg: 0, flapsDeg: 33,
    tonesOnPctLift: 0, onSpeedFastPctLift: 0, onSpeedSlowPctLift: 0,
    stallWarnPctLift: 0, flapsMinDeg: 0, flapsMaxDeg: 33,
    gOnsetRate: 0, spinCue: 0, dataMark: 0, pipPctLift: 0,
    // decelRate (kt/s) is computed locally on the M5 from the IAS time
    // series via Savitzky-Golay derivative; for the prototype we drive
    // it directly from scenarios. Wasm-live computes its own from our
    // IAS values, so the side-by-side will agree by construction.
    decelRate: 0,
    ...ANCHORS_FULL_FLAPS,
    ...overrides,
  };
}

// gOnsetRate (g/s) is the M5's right-edge orange tape input. Values
// produce visible bar height per main.cpp:840 (height = |gOnsetRate * 60|
// clamped 0..120). To exercise the bar, scenarios drive it with a
// natural-looking shape: a sub-Hertz sine for cruise (gentle), a
// pull-up + recovery shape for approach (mid amplitude), and a sharp
// pull for stall (peaks near the limits).
function gOnsetSine(tMs, periodMs, amplitude) {
  return amplitude * Math.sin(2 * Math.PI * tMs / periodMs);
}

export const scenarios = {
  idle: (_t) => record({ iasKt: 0, percentLift: 0, gOnsetRate: 0 }),
  cruise: (t) => record({
    iasKt: 130, paltFt: 4500, percentLift: 30, pitchDeg: 1.5,
    // Cruise wings-level with a slow roll oscillation and gentle VSI.
    rollDeg: 1.5 * Math.sin(2 * Math.PI * t / 8000),
    flightPathDeg: 1.5,
    vsiFpm: 100 * Math.sin(2 * Math.PI * t / 12000),
    // Light turbulence: ±0.3 g/s, 6-second period.
    gOnsetRate: gOnsetSine(t, 6000, 0.3),
    // Cruise: small +/- knot/sec wobble around 0.
    decelRate: 0.2 * Math.sin(2 * Math.PI * t / 8000),
    // Cruise G: gentle ±0.15 G turbulence around 1 G so the g-history
    // strip chart shows a live trace instead of a flat line.
    verticalG: 1.0 + 0.15 * Math.sin(2 * Math.PI * t / 6000),
    // Tick the data-mark counter once per simulated second so the
    // top-left readout exercises the full 0..99 mod range during a
    // dev-server demo. Real hardware advances on each 1 s long-press
    // of the OnSpeed switch, not on a timer — this is dev-only.
    dataMark: Math.floor(t / 1000) % 100,
  }),
  approach: (t) => {
    // 30 s sweep: percent-lift goes from 30 to 90 over 15 s, back to 30 over 15 s.
    const phase = (t / 15000) % 2;  // 0..1..2..0
    const frac = phase <= 1 ? phase : (2 - phase);
    return record({
      iasKt: 75 - 15 * frac,            // 75 → 60
      paltFt: 800,
      // Float so dev-server demos exhibit sub-percent index-bar smoothness
      // (the JSON wire carries `%.1f` since v4.24).
      percentLift: 30 + 60 * frac,
      pitchDeg: 4 + 4 * frac,
      // Roll ±5° over the cycle to exercise the horizon + ladder.
      rollDeg: 5 * Math.sin(2 * Math.PI * t / 10000),
      flightPathDeg: -3,
      // Sink rate during approach.
      vsiFpm: -500,
      // Pull-up cycle as the AOA climbs: positive gOnset on climb,
      // negative on recovery. Up to ±0.8 g/s, 4-second period.
      gOnsetRate: gOnsetSine(t, 4000, 0.8),
      // Approach: bleeding airspeed → mostly negative decel rate,
      // sweeping ±1 kt/s as the aircraft works the descent profile.
      decelRate: -0.5 + 1.0 * Math.sin(2 * Math.PI * t / 6000),
    });
  },
  stall: (t) => {
    // 10 s climb past stallWarn into flashing red, then recovery.
    const phase = (t / 10000) % 1;  // 0..1
    const pct = phase < 0.7 ? (50 + 50 * (phase / 0.7))   // climb 50→100
                            : (100 - 80 * ((phase - 0.7) / 0.3));  // recover 100→20
    // Sharp pull-up then recovery: ~+1.5 g/s during climb, ~-1.5 g/s on
    // recovery. Bar will saturate (height clamped at 120) for part of
    // the climb, exercising the full visual range.
    let g;
    if (phase < 0.55) g =  2.0 * (phase / 0.55);          // ramp up to +2 g/s
    else if (phase < 0.7) g = 2.0 - 4.0 * ((phase - 0.55) / 0.15);  // through zero to -2 g/s
    else g = -2.0 + 2.0 * ((phase - 0.7) / 0.3);          // recovery back to 0
    return record({
      iasKt: 65,
      paltFt: 3000,
      percentLift: Math.min(99, pct),
      // High pitch hold at the top of the climb; FP marker drops
      // below the aircraft as airspeed bleeds.
      pitchDeg: 8 + 2 * Math.sin(2 * Math.PI * t / 5000),
      flightPathDeg: 2,
      vsiFpm: -300,
      // Vertical G follows the same shape as gOnsetRate: pull-up
      // peaks at ~3.5 G during the climb, recovery dips to ~-0.5 G
      // briefly, then returns to 1 G. Exercises all three g-history
      // colors (green ≥1, yellow 0..1, red <0).
      verticalG: 1.0 + g,
      gOnsetRate: g,
      // Stall sequence: heavy decel during the climb, recovery shows
      // strong positive accel. Sweeps the gauge across both halves.
      decelRate: -2.0 * (phase < 0.7 ? phase / 0.7 : 1 - 2 * (phase - 0.7) / 0.3),
    });
  },
};
