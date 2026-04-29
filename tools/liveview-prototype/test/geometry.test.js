import * as G from '../lib/geometry.js';

export function run(assert) {
  // Sanity: indexer is centered horizontally on the panel.
  assert.equal(G.INDEXER_CX, G.M5_PANEL_W / 2, 'indexer center horizontally on panel');
  // Indexer extents fit inside the panel.
  assert.truthy(G.INDEXER_X >= 0, 'indexer left edge in panel');
  assert.truthy(G.INDEXER_X + G.INDEXER_WIDTH <= G.M5_PANEL_W, 'indexer right edge in panel');
  // Donut center coincides with indexer widget center.
  assert.equal(G.DONUT_GAP_X + G.DONUT_GAP_W / 2, G.INDEXER_CX, 'donut gap centered on indexer');
  // Slip ball center matches displayAOA() drawSlip(80, 204, 160, 34) call.
  assert.equal(G.SLIP_CENTER_X, 160, 'slip center x = 160');
  assert.equal(G.SLIP_CENTER_Y, 221, 'slip center y = 221');
  // G-onset zero pip is 3-px tall centered on y=119.
  assert.equal(G.GONSET_PIP_Y_MIDDLE, 119, 'G-onset zero pip middle y = 119');
  // After PR #351 fix, the first ladder tick is at 14, step 15, so tick at y=119 exists.
  const ticksOnPipMiddle = (G.GONSET_PIP_Y_MIDDLE - G.GONSET_TICK_FIRST_Y) % G.GONSET_TICK_STEP;
  assert.equal(ticksOnPipMiddle, 0, 'a ladder tick lands on the zero pip center');
}
