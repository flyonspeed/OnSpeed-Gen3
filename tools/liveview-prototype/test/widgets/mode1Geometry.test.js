import * as G from '../../lib/geometry.js';

export function run(assert) {
  // Horizon center is the same anchor pitchGraph and AiGraph use.
  // main.cpp g_px0=159, g_py0=119.
  assert.equal(G.MODE1_HORIZON_CX, 159, 'horizon CX = g_px0');
  assert.equal(G.MODE1_HORIZON_CY, 119, 'horizon CY = g_py0');
  assert.truthy(G.MODE1_HORIZON_CX > 0 && G.MODE1_HORIZON_CX < G.M5_PANEL_W,
                'horizon CX inside panel');

  // Pitch scale: HEIGHT/80 = 240/80 = 3 (main.cpp:1096).
  assert.equal(G.MODE1_PITCH_HEIGHT_SCALE, 3, 'pitch scale = 3 px/deg');

  // Pitch ladder: 10° step matches the C++ for-loop step (main.cpp:1310).
  assert.equal(G.MODE1_LADDER_STEP_DEG, 10, 'ladder step 10°');
  assert.truthy(G.MODE1_LADDER_LONG_HALF_W > G.MODE1_LADDER_SHORT_HALF_W,
                'long ticks wider than short ticks');

  // Aircraft symbol: outer wings extend symmetrically; inner edge < outer.
  assert.truthy(G.MODE1_AIRCRAFT_INNER_HALF_W < G.MODE1_AIRCRAFT_OUTER_HALF_W,
                'inner wing edge inside outer');
  // Center circle radius 6 = 2 × HEIGHT/80 (main.cpp:1192).
  assert.equal(G.MODE1_AIRCRAFT_CENTER_R, 6, 'aircraft center r = 6');

  // Flight path marker: three rings, 12/13/14.
  assert.equal(G.MODE1_FPV_RING_RADII.length, 3, 'three FPV rings');
  assert.equal(G.MODE1_FPV_RING_RADII[2], 14, 'outer ring r=14');
  assert.truthy(G.MODE1_FPV_WING_OUTER > G.MODE1_FPV_WING_INNER,
                'wing outer past wing inner');

  // Pitch readout box fits inside top half of panel and is non-empty.
  assert.truthy(G.MODE1_PITCH_READOUT_W > 0 && G.MODE1_PITCH_READOUT_H > 0,
                'pitch readout has size');
  assert.truthy(G.MODE1_PITCH_READOUT_X + G.MODE1_PITCH_READOUT_W <= G.M5_PANEL_W,
                'pitch readout right edge inside panel');

  // Corner readouts: top numbers above bottom numbers, labels close to numbers.
  assert.truthy(G.MODE1_CORNER_TOP_NUM_Y < G.MODE1_CORNER_BOT_NUM_Y,
                'top num above bottom num');
  assert.truthy(G.MODE1_CORNER_TOP_LABEL_Y > G.MODE1_CORNER_TOP_NUM_Y,
                'top label sits below top number');
  assert.truthy(G.MODE1_CORNER_BOT_LABEL_Y > G.MODE1_CORNER_BOT_NUM_Y,
                'bot label sits below bot number');
  assert.equal(G.MODE1_CORNER_RIGHT_X, 307, 'Mode 1 RIGHT_X = 307 (main.cpp:532)');

  // Slip ball is shorter (20) than Mode 0's (34).
  assert.equal(G.MODE1_SLIP_H, 20, 'Mode 1 slip H = 20');
  assert.truthy(G.MODE1_SLIP_H < G.SLIP_H, 'Mode 1 slip shorter than Mode 0');

  // VSI tape lives at the same x as Mode 0's gOnset; different y/scale.
  assert.equal(G.MODE1_VSI_BAR_X, 313, 'VSI bar x = 313');
  // 600 fpm at scale 0.2 = 120 px (matches heightMax).
  assert.near(600 * G.MODE1_VSI_HEIGHT_SCALE, G.MODE1_VSI_HEIGHT_MAX, 0.01,
              '600 fpm fills the bar');
  // First tick at y=19, last tick at y=219, step 20 → exactly 11 ticks.
  const tickCount = (G.MODE1_VSI_TICK_LAST_Y - G.MODE1_VSI_TICK_FIRST_Y) / G.MODE1_VSI_TICK_STEP + 1;
  assert.equal(tickCount, 11, '11 VSI ladder ticks (every 20 px from 19..219)');
}
