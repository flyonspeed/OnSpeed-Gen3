import { mapPct2Display } from '../lib/pct2y.js';

// Representative anchor set (RV-10 full flaps, from SerialRead.cpp:198-203).
// Slot 0 is the alpha_0 floor (always 0); slot 2 is L/Dmax (pctOf(-2.24)),
// slot 3 is OnSpeedFast, slot 4 is OnSpeedSlow, slot 7 is StallWarn.
// Numbers below approximate what the firmware would emit for that flap.
const anchors = [0, 0, 33, 51, 64, 0, 33, 80];
// anchors[0]=0 (floor), anchors[3]=51 (OnSpeedFast), anchors[4]=64 (OnSpeedSlow),
// anchors[7]=80 (StallWarn). Y range: 192 (bottom) → 115 → 78 → 1 (top).

export function run(assert) {
  // Endpoints.
  assert.equal(mapPct2Display(0,   anchors), 192, 'percent 0 -> y 192 (bottom)');
  assert.equal(mapPct2Display(51,  anchors), 115, 'OnSpeedFast edge -> y 115');
  assert.equal(mapPct2Display(64,  anchors),  78, 'OnSpeedSlow edge -> y 78');
  assert.equal(mapPct2Display(80,  anchors),   1, 'StallWarn -> y 1 (top)');
  assert.equal(mapPct2Display(99,  anchors),   1, 'above StallWarn -> y 1');
  // Mid-band linear in [floor, OnSpeedFast]: percent 33 -> map2int(33, 0, 51, 192, 115).
  assert.equal(mapPct2Display(33,  anchors),
               Math.round(192 + (115 - 192) * (33 - 0) / (51 - 0)),
               'mid floor->fast linear at percent 33');
  // Halfway between fast and slow: percent 57.5 — but inputs are ints; use 58.
  assert.equal(mapPct2Display(58,  anchors),
               Math.round(115 + (78 - 115) * (58 - 51) / (64 - 51)),
               'mid-donut linear at percent 58');
}
