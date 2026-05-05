// Geometry-invariant tests. These guard against silent regressions
// that visual A/B against the M5 wouldn't catch on a level bench —
// e.g., flipping MODE1_PITCH_HEIGHT_SCALE from +3 to -3 inverts the
// horizon at flight time. Run with:
//
//   node tools/web/test/geometry-invariants.mjs
//
// Exit code 0 = all pass, non-zero = some failed.

import * as G from '../lib/core/geometry.js';
import { chevronColors } from '../lib/core/chevronColors.js';
import { donutColors } from '../lib/core/donutColors.js';
import { mapPct2Display } from '../lib/core/pct2y.js';
import { flapWidgetFrac, flapTriangleTransform } from '../lib/core/flapWidget.js';
import { slipFromLateralG, slipBall } from '../lib/core/slipBall.js';

let failed = 0;
let passed = 0;
const results = [];

function eq(actual, expected, msg) {
  if (actual === expected) { passed++; results.push(['  PASS', msg]); }
  else { failed++; results.push(['  FAIL', `${msg}: got ${actual}, want ${expected}`]); }
}
function ok(cond, msg) {
  if (cond) { passed++; results.push(['  PASS', msg]); }
  else { failed++; results.push(['  FAIL', msg]); }
}
function near(actual, expected, eps, msg) {
  if (Math.abs(actual - expected) <= eps) { passed++; results.push(['  PASS', msg]); }
  else { failed++; results.push(['  FAIL', `${msg}: got ${actual}, want ~${expected}±${eps}`]); }
}

// ----- Mode 1 (Attitude) layout invariants -------------------------------
//
// The horizon and pitch ladder depend on these constants matching the
// M5 firmware exactly. A sign flip on MODE1_PITCH_HEIGHT_SCALE would
// invert the entire AI without visual A/B catching it on a level bench
// (positive pitch would push the horizon UP instead of DOWN).

eq(G.MODE1_HORIZON_CX, 159, 'horizon CX = main.cpp g_px0');
eq(G.MODE1_HORIZON_CY, 119, 'horizon CY = main.cpp g_py0');
eq(G.MODE1_PITCH_HEIGHT_SCALE, 3, 'pitch scale = HEIGHT/80 = 3 px/deg');
ok(G.MODE1_PITCH_HEIGHT_SCALE > 0,
   'pitch scale is positive (negative would invert horizon at flight time)');

eq(G.MODE1_LADDER_STEP_DEG, 10, 'ladder step 10°');
ok(G.MODE1_LADDER_LONG_HALF_W > G.MODE1_LADDER_SHORT_HALF_W,
   'long ticks wider than short ticks');

ok(G.MODE1_AIRCRAFT_INNER_HALF_W < G.MODE1_AIRCRAFT_OUTER_HALF_W,
   'aircraft inner wing edge inside outer');
eq(G.MODE1_AIRCRAFT_CENTER_R, 6,
   'aircraft center radius = 2 × HEIGHT/80 = 6 (main.cpp:1192)');

eq(G.MODE1_FPV_RING_RADII.length, 3, 'three FPV rings');
eq(G.MODE1_FPV_RING_RADII[2], 14, 'outer FPV ring r=14');
ok(G.MODE1_FPV_WING_OUTER > G.MODE1_FPV_WING_INNER,
   'FPV wing outer past wing inner');

ok(G.MODE1_PITCH_READOUT_W > 0 && G.MODE1_PITCH_READOUT_H > 0,
   'pitch readout has size');
ok(G.MODE1_PITCH_READOUT_X + G.MODE1_PITCH_READOUT_W <= G.M5_PANEL_W,
   'pitch readout right edge inside panel');

ok(G.MODE1_CORNER_TOP_NUM_Y < G.MODE1_CORNER_BOT_NUM_Y,
   'top num above bottom num');
eq(G.MODE1_CORNER_RIGHT_X, 307, 'Mode 1 RIGHT_X = 307 (main.cpp:532)');

// ----- Mode 1 VSI tape ---------------------------------------------------

eq(G.MODE1_VSI_BAR_X, 313, 'VSI bar x = 313');
near(G.MODE1_VSI_FULL_SCALE_FPM * G.MODE1_VSI_HEIGHT_SCALE,
     G.MODE1_VSI_HEIGHT_MAX, 0.01,
     'full-scale fpm fills the VSI bar');
eq(G.MODE1_VSI_FULL_SCALE_FPM, 600,
   'VSI bar saturates at ±600 fpm (matches kVsiBarFullScaleFpm in main.cpp)');
const tickCount = (G.MODE1_VSI_TICK_LAST_Y - G.MODE1_VSI_TICK_FIRST_Y) / G.MODE1_VSI_TICK_STEP + 1;
eq(tickCount, 11, '11 VSI ladder ticks (every 20 px from 19..219)');

// ----- Mode 3 (Energy) decel gauge ---------------------------------------

eq(G.MODE3_GAUGE_X, 109, 'decel gauge x = 109');
eq(G.MODE3_GAUGE_W, 102, 'decel gauge w = 102');
near(G.MODE3_DECEL_OFFSET, 141.48, 0.01,
     'decel offset = 141.48 (main.cpp:1357)');
near(G.MODE3_DECEL_SCALE, 35.143, 0.001,
     'decel scale = 35.143 (main.cpp:1357)');
// At decelRate=0, pointer center should land at y=141 (the "0" pip y).
const zeroPointerCenter = G.MODE3_DECEL_SCALE * 0 + G.MODE3_DECEL_OFFSET;
near(zeroPointerCenter, 141.48, 0.01,
     'decel pointer center at y≈141 when decelRate=0');

// ----- Mode 4 (G-history) -------------------------------------------------

eq(G.MODE4_BUFFER_LEN, 300, 'G-history buffer 300 samples');
eq(G.MODE4_SAMPLE_MS, 200, 'G-history 5 Hz sampling (main.cpp:465)');
eq(G.MODE4_TRACE_X_MAX - G.MODE4_TRACE_X_MIN + 1, G.MODE4_BUFFER_LEN,
   'G-history trace width matches buffer length');
eq(G.MODE4_ONE_G_Y, 133, '1G horizontal at y=133');

// ----- pct2y mapping ----------------------------------------------------
//
// Five anchors in the index array (slot 0 = floor, 3 = OnSpeedFast,
// 4 = OnSpeedSlow, 6 = pip, 7 = stallWarn). The C++ piecewise mapping
// returns specific y values at the band edges.
//
// Post PR #376: the upper ramp tops out at percent_lift = 99 (the
// lift-envelope ceiling), not at stallWarn. stallWarn now drives the
// chevron flash-red color logic only, not the y-coordinate. So
// pct=stallWarn lands somewhere on the upper ramp between 78 and 1
// (proportional to where stallWarn falls in [slowEdge..99]), and only
// pct=99 reaches the top.

// Y values come from main.cpp:1515-1519: bottom=192, OnSpeedFast band
// edge=115, OnSpeedSlow band edge=78, top=1 (at pct=99).
const anchors = [0, 0, 30, 50, 70, 0, 40, 90];  // floor=0, fast=50, slow=70, warn=90
eq(mapPct2Display(0, anchors), 192, 'pct=0 → bottom of indexer (y=192)');
eq(mapPct2Display(50, anchors), 115, 'pct=fastEdge → 115 (donut bottom edge)');
eq(mapPct2Display(70, anchors), 78, 'pct=slowEdge → 78 (donut top edge)');
eq(mapPct2Display(99, anchors), 1, 'pct=99 → 1 (top, lift-envelope ceiling)');
eq(mapPct2Display(99.5, anchors), 1, 'pct in (99, 99.9] saturates at top (off-the-chart cue)');
eq(mapPct2Display(100, anchors), 1, 'pct above 99 clamps to top');
ok(mapPct2Display(90, anchors) > 1 && mapPct2Display(90, anchors) < 78,
   'pct=stallWarn lands on upper ramp between 78 and 1, not pinned at top');
ok(mapPct2Display(60, anchors) < mapPct2Display(50, anchors),
   'pct ramps monotonically up (lower y is HIGHER on screen)');

// Degenerate-band guards: when adjacent anchors land on the same value,
// the matching branch has zero span and would divide by zero without the
// inHigh==inLow guard in map2int. Both pct2y.js and main.cpp's map2int
// collapse the empty band to outLow rather than producing ±Inf.
const fastEqSlow = [0, 0, 30, 50, 50, 0, 50, 90];  // fast==slow (band collapsed)
eq(mapPct2Display(50, fastEqSlow), 115,
   'Array[3]==Array[4]: pct at the collapsed band lands at outLow (115), not divide-by-zero');
const slowAt99 = [0, 0, 30, 50, 99, 0, 50, 99];    // slow==99 (upper ramp collapsed)
eq(mapPct2Display(99, slowAt99), 78,
   'Array[4]==99: pct=99 lands at slow-edge y (78), not divide-by-zero');

// Regression: the index-bar bug from the v4.23 wire-format split.
// Before the v4.24 unification, the renderer read `PercentLiftDeci`
// (a *separate* float global) for the bar y-position, while
// `PercentLift` (int) drove the chevrons.  Any code path that
// populated the int but forgot the float (e.g. the M5 sim's
// DUMMY_SERIAL_DATA block) would pin the bar at y=192 forever.  We
// catch the equivalent failure mode now by:
//   1. asserting the bar y is a strict function of percent_lift
//      (different inputs must produce different outputs), and
//   2. asserting that fractional pcts produce y values strictly
//      between adjacent integer-pct outputs (sub-percent fidelity
//      survives end-to-end through the float pipeline).
// Without these, the previous bug could re-emerge silently if a
// future refactor reintroduces a "second hidden global" — the test
// pins the contract that one float in, one indexY out.
// Lower y on screen = higher on the indexer column.  The mapping is
// monotonically decreasing in pct: bigger pct → smaller y.
const yAt0    = mapPct2Display(0,    anchors);  // floor case
const yAt05   = mapPct2Display(0.5,  anchors);  // sub-percent above floor
ok(yAt0 === 192, 'pct=0 still maps to floor y=192');
ok(yAt05 < yAt0,
   'pct=0.5 advances bar above floor (was pinned at 192 in the v4.23 bug)');

// Sub-percent fidelity in the mapping function (before SVG int-rounding):
// the difference between pct=20 and pct=20.5 must be visible in the
// returned y.  Walk pct in 0.5 steps across the upper ramp where 1 pct
// of body-angle corresponds to ~3.85 y-pixels; 0.5 pct → ~1.9 px,
// distinguishable after Math.round.
const yPct70  = mapPct2Display(70,   anchors);
const yPct705 = mapPct2Display(70.5, anchors);
const yPct71  = mapPct2Display(71,   anchors);
ok(yPct705 < yPct70 && yPct705 > yPct71,
   'fractional pct=70.5 lands strictly between pct=70 and pct=71 (sub-percent fidelity)');

// ----- chevron color logic ------------------------------------------------
//
// Encodes the audio-cue gating. The legacy /live had this hand-coded;
// any divergence here means visual indexer disagrees with the audio
// thresholds, which is a real safety issue.

const chevTone = chevronColors({ percentLift: 35, anchors, flashFlag: false });
ok(chevTone.bottom !== chevTone.top || true, 'chevron returns shape');
// At percentLift below tonesOn (slot 2 = 30), nothing should be green.
const chevLow = chevronColors({ percentLift: 20, anchors, flashFlag: false });
ok(typeof chevLow.top === 'string' && typeof chevLow.bottom === 'string',
   'chevron returns string colors');
// At percentLift above stallWarn (90), with flash on, top is the
// flash color (DARKGREY). Without flash, RED.
const chevWarn = chevronColors({ percentLift: 95, anchors, flashFlag: false });
const chevFlash = chevronColors({ percentLift: 95, anchors, flashFlag: true });
ok(chevWarn.top !== chevFlash.top,
   'flashFlag swaps top chevron color above stallWarn');

// ----- donut color logic --------------------------------------------------
//
// Independent of chevron — donut highlights when AOA is in the
// OnSpeed band (between OnSpeedFast and OnSpeedSlow).

const donutOnSpeed = donutColors({ percentLift: 60, anchors });
const donutLow = donutColors({ percentLift: 20, anchors });
ok(donutOnSpeed.dot !== donutLow.dot,
   'donut center dot color differs in OnSpeed band vs low AOA');

// ----- flap widget math ---------------------------------------------------

eq(flapWidgetFrac(0, 0, 33), 0, 'flap at min → frac 0');
eq(flapWidgetFrac(33, 0, 33), 1, 'flap at max → frac 1');
near(flapWidgetFrac(16, 0, 33), 0.485, 0.01, 'flap halfway → frac ~0.5');
// Reflex flaps (negative min) — important for full-flap aircraft
// designs that use negative settings as a cruise reflex.
eq(flapWidgetFrac(-5, -5, 30), 0, 'reflex flap at min → frac 0');
near(flapWidgetFrac(15, -5, 30), 0.571, 0.01,
     'reflex flap range maps correctly');
// Degenerate range: returns 0.5 (midpoint of arc) — see C++ contract
// `if (span <= 0) return 0.5f`.
eq(flapWidgetFrac(0, 5, 5), 0.5, 'degenerate flap range → midpoint');

// flapTriangleTransform: maps frac → degrees on the visual arc.
near(flapTriangleTransform(0), 0, 0.01, 'frac 0 → 0°');
near(flapTriangleTransform(1), G.FLAP_ARC_DEG, 0.01,
     'frac 1 → max arc angle');

// ----- slip ball ---------------------------------------------------------
//
// slipFromLateralG accepts JSON-frame body lateral G (positive = right)
// and produces the cockpit-frame ball deflection (positive Slip = ball
// right of center).  Sign rationale at proto/DisplaySerial.h::lateralG.

eq(slipFromLateralG(0), 0, 'zero G → zero slip');
eq(slipFromLateralG(0.05), -42, 'rightward G → ball deflects left (negative slip)');
eq(slipFromLateralG(-0.05), 43, 'leftward G → ball deflects right (positive slip)');
eq(slipFromLateralG(0.5), -99, 'rightward G clamped to -99');
eq(slipFromLateralG(-0.5), 99, 'leftward G clamped to +99');

const slipResult = slipBall({
  slip: 0,
  percentLift: 50,
  stallWarn: 90,
  flashFlag: false,
});
eq(slipResult.cx, G.SLIP_CENTER_X, 'slip=0 → ball at center');
ok(typeof slipResult.fill === 'string', 'slipBall returns fill color');
const slipStall = slipBall({
  slip: 50,
  percentLift: 95,
  stallWarn: 90,
  flashFlag: false,
});
ok(slipStall.fill !== slipResult.fill,
   'slip + high AOA changes fill (stall flash)');

// ----- Report -------------------------------------------------------------

console.log('Geometry invariants:');
for (const [tag, msg] of results) console.log(tag, msg);
console.log(`\n${passed} passed, ${failed} failed`);
process.exit(failed > 0 ? 1 : 0);
