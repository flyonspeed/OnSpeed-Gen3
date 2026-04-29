import * as G from '../geometry.js';
import { colors } from '../colors.js';

// Mode 4 — G-load history (case 4 in main.cpp:643).
// Source: main.cpp::displayGloadHistory() at lines 1437-1493.
//
// 60-second strip chart of vertical G. Ring buffer of 300 samples
// updated at 5 Hz (main.cpp:465 — `millis()-gHistoryTime > 200`).
//
// Scroll direction: NEW data enters from the LEFT, OLD data exits on
// the RIGHT. The C++ render loop starts at gHistoryIndex (the next
// WRITE position) and walks the buffer FORWARD while painting columns
// right→left, which means the rightmost column gets the OLDEST sample
// (about to be overwritten) and the leftmost column gets the NEWEST.
// Looks reversed compared to a typical rolling chart, but it's
// faithful to the M5.
//
// Color per dot:
//   g >= 1.0   → green
//   0  ≤ g < 1 → yellow
//   g  < 0     → red

const SVG_NS = 'http://www.w3.org/2000/svg';

function mk(parent, tag, attrs) {
  const e = document.createElementNS(SVG_NS, tag);
  for (const k in attrs) e.setAttribute(k, attrs[k]);
  parent.appendChild(e);
  return e;
}

function gColor(g) {
  if (g >= 1)      return colors.TFT_GREEN;
  else if (g >= 0) return colors.TFT_YELLOW;
  else             return colors.TFT_RED;
}

function gToY(g) {
  let y = G.MODE4_DOT_Y_OFFSET - g * G.MODE4_DOT_Y_SCALE;
  if (y < G.MODE4_DOT_Y_MIN) y = G.MODE4_DOT_Y_MIN;
  if (y > G.MODE4_DOT_Y_MAX) y = G.MODE4_DOT_Y_MAX;
  return y;
}

export function mountGHistory(rootEl) {
  const svg = document.createElementNS(SVG_NS, 'svg');
  svg.setAttribute('viewBox', `0 0 ${G.M5_PANEL_W} ${G.M5_PANEL_H}`);
  svg.setAttribute('xmlns', SVG_NS);
  svg.style.background = colors.TFT_BLACK;
  svg.style.width  = '100%';
  svg.style.height = '100%';

  // ----- Static frame -----
  // Grey gridlines first (under everything else).
  for (const y of G.MODE4_GRIDLINE_YS) {
    mk(svg, 'line', {
      x1: G.MODE4_AXIS_X, y1: y, x2: G.MODE4_TRACE_X_MAX, y2: y,
      stroke: colors.TFT_GREY, 'stroke-width': 1,
    });
  }
  // 1G horizontal + vertical axis (white).
  mk(svg, 'line', {
    x1: G.MODE4_AXIS_X,    y1: G.MODE4_ONE_G_Y,
    x2: G.MODE4_TRACE_X_MAX, y2: G.MODE4_ONE_G_Y,
    stroke: colors.TFT_WHITE, 'stroke-width': 1,
  });
  mk(svg, 'line', {
    x1: G.MODE4_AXIS_X, y1: G.MODE4_AXIS_Y_TOP,
    x2: G.MODE4_AXIS_X, y2: G.MODE4_AXIS_Y_BOT,
    stroke: colors.TFT_WHITE, 'stroke-width': 1,
  });

  // Pip labels.
  for (const pip of G.MODE4_PIP_LABELS) {
    mk(svg, 'text', {
      x: G.MODE4_PIP_LABEL_X, y: pip.y,
      'font-family': 'Helvetica, Arial, sans-serif',
      'font-size': G.MODE4_PIP_FONT_SIZE,
      fill: colors.TFT_WHITE,
      'text-anchor': 'end',
      'dominant-baseline': 'central',
    }).textContent = pip.text;
  }

  // Header text.
  mk(svg, 'text', {
    x: G.MODE4_HEADER_X, y: G.MODE4_HEADER_Y,
    'font-family': 'Helvetica, Arial, sans-serif',
    'font-size': G.MODE4_HEADER_FONT_SIZE,
    fill: colors.TFT_WHITE,
    'text-anchor': 'middle',
    'dominant-baseline': 'hanging',
  }).textContent = G.MODE4_HEADER_TEXT;

  // ----- Strip chart dots -----
  // 300 pre-created circles, one per x-pixel column. Per-frame update
  // sets cy + fill based on the ring buffer, no element creation.
  const traceWidth = G.MODE4_TRACE_X_MAX - G.MODE4_TRACE_X_MIN + 1;  // 300
  const dots = new Array(traceWidth);
  for (let i = 0; i < traceWidth; i++) {
    const cx = G.MODE4_TRACE_X_MAX - i;  // i=0 → newest at x=319
    dots[i] = mk(svg, 'circle', {
      cx, cy: G.MODE4_ONE_G_Y, r: G.MODE4_DOT_R, fill: colors.TFT_GREEN,
    });
  }

  rootEl.appendChild(svg);

  // ----- Ring buffer + 5 Hz sample gate -----
  // Match the M5's 200 ms gate (main.cpp:465). Buffer is initialized
  // to 1.0 G (level cruise) so the chart starts visually flat.
  const buf = new Float32Array(G.MODE4_BUFFER_LEN);
  buf.fill(1.0);
  let writeIdx = 0;
  let lastSampleMs = 0;

  function update(rec) {
    const now = performance.now();
    if (now - lastSampleMs >= G.MODE4_SAMPLE_MS) {
      buf[writeIdx] = rec.verticalG;
      writeIdx = (writeIdx + 1) % G.MODE4_BUFFER_LEN;
      lastSampleMs = now;
    }

    // Render every frame. C++ starts at gDisplayIndex = gHistoryIndex
    // (the NEXT WRITE position = oldest sample about to be overwritten),
    // paints x=319 (rightmost), then increments through the ring while
    // walking x leftward. So:
    //   dots[0] is x=319 (oldest sample, slot writeIdx)
    //   dots[N-1] is x=20 (newest sample, slot writeIdx-1)
    // Net effect: new data appears at the LEFT and scrolls right as
    // it ages, faithful to the M5.
    const N = G.MODE4_BUFFER_LEN;
    let sampleIdx = writeIdx;
    for (let i = 0; i < traceWidth; i++) {
      const g = buf[sampleIdx];
      dots[i].setAttribute('cy', gToY(g));
      dots[i].setAttribute('fill', gColor(g));
      sampleIdx = (sampleIdx + 1) % N;
    }
  }

  return { el: svg, update };
}
