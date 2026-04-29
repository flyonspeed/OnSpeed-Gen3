import { colors } from '../lib/colors.js';

export function run(assert) {
  assert.equal(colors.TFT_BLACK, 'var(--panel-bg)', 'TFT_BLACK maps to panel-bg var');
  assert.equal(colors.TFT_GREEN, 'var(--green)',    'TFT_GREEN maps to green var');
  // Mode 1 (Attitude) tokens.
  assert.equal(colors.TFT_CYAN,    'var(--sky)',     'TFT_CYAN maps to sky var');
  assert.equal(colors.TFT_BROWN,   'var(--ground)',  'TFT_BROWN maps to ground var');
  assert.equal(colors.TFT_MAGENTA, 'var(--magenta)', 'TFT_MAGENTA maps to magenta var');
  assert.equal(colors.TFT_ORANGE,  'var(--orange)',  'TFT_ORANGE maps to orange var');
  assert.equal(colors.TFT_BLUE,    'var(--blue)',    'TFT_BLUE maps to blue var');
  assert.equal(Object.isFrozen(colors), true,       'colors is frozen');
}
