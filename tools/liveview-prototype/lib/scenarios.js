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
    ...ANCHORS_FULL_FLAPS,
    ...overrides,
  };
}

export const scenarios = {
  idle: (_t) => record({ iasKt: 0, percentLift: 0 }),
  cruise: (_t) => record({ iasKt: 130, paltFt: 4500, percentLift: 30, pitchDeg: 1.5 }),
  approach: (t) => {
    // 30 s sweep: percent-lift goes from 30 to 90 over 15 s, back to 30 over 15 s.
    const phase = (t / 15000) % 2;  // 0..1..2..0
    const frac = phase <= 1 ? phase : (2 - phase);
    return record({
      iasKt: 75 - 15 * frac,            // 75 → 60
      paltFt: 800,
      percentLift: Math.round(30 + 60 * frac),
      pitchDeg: 4 + 4 * frac,
      flightPathDeg: -3,
      vsiFpm: -500,
    });
  },
  stall: (t) => {
    // 10 s climb past stallWarn into flashing red, then recovery.
    const phase = (t / 10000) % 1;  // 0..1
    const pct = phase < 0.7 ? Math.round(50 + 50 * (phase / 0.7))   // climb 50→100
                            : Math.round(100 - 80 * ((phase - 0.7) / 0.3));  // recover 100→20
    return record({
      iasKt: 65,
      paltFt: 3000,
      percentLift: Math.min(99, pct),
      pitchDeg: 8,
      verticalG: 1.0,
    });
  },
};
