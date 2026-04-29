import { colors } from '../lib/colors.js';

export function run(assert) {
  assert.equal(colors.TFT_BLACK, 'var(--bg-panel)', 'TFT_BLACK maps to bg var');
  assert.equal(colors.TFT_GREEN, 'var(--green)',     'TFT_GREEN maps to green var');
  assert.equal(Object.isFrozen(colors), true,        'colors is frozen');
}
