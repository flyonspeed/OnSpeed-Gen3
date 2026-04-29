import { mountSlipBall } from '../../lib/widgets/slipBall.js';
import { colors } from '../../lib/colors.js';

const SVG_NS = 'http://www.w3.org/2000/svg';

export function run(assert) {
  const svg = document.createElementNS(SVG_NS, 'svg');
  const w = mountSlipBall(svg, { x: 80, y: 204, width: 160, height: 34 });

  // 4 frame rects + 1 ball circle = 5 children.
  assert.equal(w.el.children.length, 5, 'group has 4 frame rects + 1 ball');
  const ball = w.el.querySelector('circle');
  assert.truthy(ball, 'ball circle exists');
  assert.equal(w.centerX, 160, 'centerX exposed');
  assert.equal(w.centerY, 221, 'centerY exposed');

  // Centered slip → cx at center.
  w.update({ slip: 0, percentLift: 50, stallWarn: 80, flashFlag: false });
  assert.equal(ball.getAttribute('cx'), '160', 'zero slip: cx = centerX');
  assert.equal(ball.getAttribute('fill'), colors.TFT_GREEN, 'normal: green');

  // Stalling + slipping flashes red/black.
  w.update({ slip: 50, percentLift: 90, stallWarn: 80, flashFlag: false });
  assert.equal(ball.getAttribute('fill'), colors.TFT_RED, 'stall+slip flash on: red');
  w.update({ slip: 50, percentLift: 90, stallWarn: 80, flashFlag: true });
  assert.equal(ball.getAttribute('fill'), colors.TFT_BLACK, 'stall+slip flash off: black');
}
