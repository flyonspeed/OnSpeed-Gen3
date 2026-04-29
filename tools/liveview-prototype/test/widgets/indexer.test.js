import { mountIndexer } from '../../lib/widgets/indexer.js';
import { colors } from '../../lib/colors.js';

const SVG_NS = 'http://www.w3.org/2000/svg';

export function run(assert) {
  const svg = document.createElementNS(SVG_NS, 'svg');
  const w = mountIndexer(svg);

  // Returns { el, update }.
  assert.truthy(w.el && w.update, 'returns el+update contract');
  assert.equal(w.el.tagName, 'g', 'el is an SVG <g>');

  // Structure: 1 bounding rect + 4 chevron rects + 1 donut surround +
  // 2 arcs + 1 gap rect + 1 dot + 1 index bar + 4 pip circles = 15 children.
  // Plus the wrapping <g> = 16 total descendants of the SVG.
  const all = svg.querySelectorAll('*');
  assert.equal(all.length, 16, 'mounted 15 elements inside the group');

  // After update with low AOA, chevrons are dark grey.
  const anchors = [0, 0, 33, 51, 64, 0, 33, 80];
  w.update({ percentLift: 20, anchors, flashFlag: false });
  const chevrons = Array.from(svg.querySelectorAll('rect'))
    .filter(r => r.getAttribute('transform'));
  assert.equal(chevrons.length, 4, 'four chevron halves');
  assert.equal(chevrons[0].getAttribute('fill'), colors.TFT_DARKGREY, 'low AOA: top chev gray');

  // High AOA, not flashing -> top chevron red.
  w.update({ percentLift: 90, anchors, flashFlag: false });
  assert.equal(chevrons[0].getAttribute('fill'), colors.TFT_RED, 'stall AOA: top chev red');
}
