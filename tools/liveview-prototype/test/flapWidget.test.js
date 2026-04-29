import { flapWidgetFrac, flapTriangleTransform } from '../lib/flapWidget.js';

// Mirrors test/test_flap_widget_math/test_flap_widget_math.cpp.
export function run(assert) {
  // Endpoints.
  assert.equal(flapWidgetFrac( 0,  0, 33), 0,    'at min returns 0');
  assert.equal(flapWidgetFrac(33,  0, 33), 1,    'at max returns 1');
  assert.near (flapWidgetFrac(16,  0, 33), 16/33, 0.01, 'midpoint returns ~0.5');

  // Linearity through the range.
  for (let i = 0; i <= 10; ++i) {
    const pos = i * 4;
    const expected = pos / 40;
    assert.near(flapWidgetFrac(pos, 0, 40), expected, 0.01,
                `linearity at pos ${pos}`);
  }

  // Out-of-range clamping.
  assert.equal(flapWidgetFrac(-5,  0, 33), 0, 'below min clamps to 0');
  assert.equal(flapWidgetFrac(-99, 0, 33), 0, 'far below min clamps to 0');
  assert.equal(flapWidgetFrac(40,  0, 33), 1, 'above max clamps to 1');
  assert.equal(flapWidgetFrac(99,  0, 33), 1, 'far above max clamps to 1');

  // Reflex flaps (negative flapsMin).
  assert.equal(flapWidgetFrac(-5, -5, 30), 0, 'reflex at min returns 0');
  assert.equal(flapWidgetFrac(30, -5, 30), 1, 'reflex at max returns 1');
  assert.near (flapWidgetFrac( 0, -5, 30), 5/35, 0.001, 'reflex neutral = 5/35');
  assert.equal(flapWidgetFrac(-10, -5, 30), 0, 'reflex below min clamps to 0');

  // Degenerate: single-position aircraft (min == max) -> 0.5.
  assert.equal(flapWidgetFrac( 0,  0,  0), 0.5, 'min==max parks mid-arc');
  assert.equal(flapWidgetFrac(15, 15, 15), 0.5, 'min==max parks mid-arc (any value)');
  assert.equal(flapWidgetFrac(99, 15, 15), 0.5, 'min==max parks mid-arc (any value)');
  // Misconfigured: max < min -> same fallback as single-position.
  assert.equal(flapWidgetFrac(15, 30,  5), 0.5, 'max<min parks mid-arc');

  // Triangle transform.
  assert.equal(flapTriangleTransform(0),    0,  'frac 0 -> 0°');
  assert.equal(flapTriangleTransform(0.5), 20,  'frac 0.5 -> 20°');
  assert.equal(flapTriangleTransform(1),   40,  'frac 1 -> 40°');
}
