// Shared Preact components for OnSpeed LiveView.
//
// Each component is a pure function of props. The five mode files
// (lib/modes.js) compose these components against a single record `r`
// drawn from the WebSocket. Layout constants come from the framework-
// free helpers in ../../core/.

import { html } from '../../vendor/preact-standalone.js';
import * as G from '../../core/geometry.js';
import { colors } from '../../core/colors.js';
import { mapPct2Display } from '../../core/pct2y.js';
import { chevronColors } from '../../core/chevronColors.js';
import { donutColors } from '../../core/donutColors.js';
import { slipFromLateralG } from '../../core/slipBall.js';
import { flapWidgetFrac, flapTriangleTransform } from '../../core/flapWidget.js';
import { fmt } from '../../core/format.js';

// SVG arc path helper — circle arc from start to end angle (radians).
const arcPath = (cx, cy, r, startRad, endRad) => {
  const x1 = cx + r * Math.cos(startRad), y1 = cy + r * Math.sin(startRad);
  const x2 = cx + r * Math.cos(endRad),   y2 = cy + r * Math.sin(endRad);
  const large = (endRad - startRad) > Math.PI ? 1 : 0;
  return `M ${x1} ${y1} A ${r} ${r} 0 ${large} 1 ${x2} ${y2}`;
};

// Chevron half — rotated rect.
const Chevron = ({ cx, cy, signRad, fill }) => html`
  <rect x=${cx - G.CHEVRON_HALF_W} y=${cy - G.CHEVRON_HALF_H}
        width=${G.CHEVRON_HALF_W * 2} height=${G.CHEVRON_HALF_H * 2}
        transform="rotate(${signRad * 180 / Math.PI} ${cx} ${cy})"
        fill=${fill} shape-rendering="crispEdges" />`;

// AOA indexer: bounding rect + 4 chevrons + donut + index bar + L/Dmax pips.
// Index bar hidden when aoaIsValid is false (matches legacy /live's gate).
export const Indexer = ({ percentLift, anchors, flashFlag, aoaIsValid }) => {
  const c = chevronColors({ percentLift, anchors, flashFlag });
  const d = donutColors({ percentLift, anchors });
  const indexY = mapPct2Display(percentLift, anchors);
  const ldmaxY = mapPct2Display(anchors[6], anchors);
  return html`
    <g data-widget="indexer">
      <rect x=${G.INDEXER_X} y=${G.INDEXER_Y}
            width=${G.INDEXER_WIDTH} height=${G.INDEXER_HEIGHT}
            rx=${G.INDEXER_BOX_RADIUS}
            fill="none" stroke=${colors.TFT_DARKGREY} stroke-width="2" />
      <${Chevron} cx=${G.CHEVRON_TOP_LEFT_CX}     cy=${G.CHEVRON_TOP_LEFT_CY}    signRad=${-G.CHEVRON_ROTATION_RAD} fill=${c.top} />
      <${Chevron} cx=${G.CHEVRON_TOP_RIGHT_CX}    cy=${G.CHEVRON_TOP_RIGHT_CY}   signRad=${G.CHEVRON_ROTATION_RAD}  fill=${c.top} />
      <${Chevron} cx=${G.CHEVRON_BOTTOM_LEFT_CX}  cy=${G.CHEVRON_BOTTOM_LEFT_CY} signRad=${G.CHEVRON_ROTATION_RAD}  fill=${c.bottom} />
      <${Chevron} cx=${G.CHEVRON_BOTTOM_RIGHT_CX} cy=${G.CHEVRON_BOTTOM_RIGHT_CY} signRad=${-G.CHEVRON_ROTATION_RAD} fill=${c.bottom} />
      <circle cx=${G.INDEXER_CX} cy=${G.INDEXER_CY} r=${G.DONUT_BLACK_R} fill=${colors.TFT_BLACK} />
      <path d=${arcPath(G.INDEXER_CX, G.INDEXER_CY, G.DONUT_ARC_R, 0, Math.PI)}
            fill="none" stroke=${d.bottomArc} stroke-width=${G.DONUT_ARC_LINEWIDTH} />
      <path d=${arcPath(G.INDEXER_CX, G.INDEXER_CY, G.DONUT_ARC_R, Math.PI, 2 * Math.PI)}
            fill="none" stroke=${d.topArc} stroke-width=${G.DONUT_ARC_LINEWIDTH} />
      <rect x=${G.DONUT_GAP_X} y=${G.DONUT_GAP_Y}
            width=${G.DONUT_GAP_W} height=${G.DONUT_GAP_H} fill=${colors.TFT_BLACK} />
      <circle cx=${G.INDEXER_CX} cy=${G.INDEXER_CY} r=${G.DONUT_DOT_R} fill=${d.dot} />
      ${aoaIsValid && html`
        <rect x=${G.INDEX_BAR_X} y=${indexY}
              width=${G.INDEX_BAR_W} height=${G.INDEX_BAR_H}
              fill=${colors.TFT_WHITE} stroke=${colors.TFT_BLACK}
              stroke-width="1" shape-rendering="crispEdges" />`}
      <circle cx=${G.PIP_LEFT_CX}  cy=${ldmaxY} r=${G.PIP_HALO_R}  fill=${colors.TFT_BLACK} />
      <circle cx=${G.PIP_LEFT_CX}  cy=${ldmaxY} r=${G.PIP_INNER_R} fill=${colors.TFT_WHITE} />
      <circle cx=${G.PIP_RIGHT_CX} cy=${ldmaxY} r=${G.PIP_HALO_R}  fill=${colors.TFT_BLACK} />
      <circle cx=${G.PIP_RIGHT_CX} cy=${ldmaxY} r=${G.PIP_INNER_R} fill=${colors.TFT_WHITE} />
    </g>`;
};

export const PercentLiftNumber = ({ percent, aoaIsValid }) => {
  if (!aoaIsValid) return null;
  // Round + clamp so the 2-digit field never reads "100" — same
  // saturation convention the M5 firmware uses (see main.cpp's
  // displayPercentLift snapshot).
  const rounded = Math.min(99, Math.max(0, Math.round(percent)));
  const s = String(rounded).padStart(2, '0');
  return html`
    <g data-widget="percent-lift-number">
      <text x=${G.PCT_LIFT_X} y=${G.PCT_LIFT_Y}
            font-family="Helvetica, Arial, sans-serif" font-weight="bold"
            font-size=${G.PCT_LIFT_FONT_SIZE} fill="none"
            stroke=${colors.TFT_BLACK} stroke-width=${G.PCT_LIFT_OUTLINE_PX * 2}
            stroke-linejoin="round" text-anchor="middle">${s}</text>
      <text x=${G.PCT_LIFT_X} y=${G.PCT_LIFT_Y}
            font-family="Helvetica, Arial, sans-serif" font-weight="bold"
            font-size=${G.PCT_LIFT_FONT_SIZE} fill=${colors.TFT_WHITE}
            text-anchor="middle">${s}</text>
    </g>`;
};

export const CornerReadout = ({ label, value,
                                 labelX, labelY, numX, numY,
                                 anchor = 'start',
                                 labelColor = colors.TFT_GREEN,
                                 numColor = colors.TFT_WHITE,
                                 labelFontSize = G.CORNER_LABEL_FONT_SIZE,
                                 numFontSize = G.CORNER_NUM_FONT_SIZE,
                                 numBaseline = 'alphabetic' }) => html`
  <g data-widget="corner">
    <text x=${labelX} y=${labelY}
          font-family="Helvetica, Arial, sans-serif"
          font-size=${labelFontSize} fill=${labelColor}
          text-anchor=${anchor}>${label}</text>
    <text x=${numX} y=${numY}
          font-family="Helvetica, Arial, sans-serif" font-weight="bold"
          font-size=${numFontSize} fill=${numColor}
          text-anchor=${anchor} dominant-baseline=${numBaseline}>${value}</text>
  </g>`;

export const FlapCircle = ({ flapPos, flapsMin, flapsMax }) => {
  const frac = flapWidgetFrac(flapPos, flapsMin, flapsMax);
  const a = flapTriangleTransform(frac) * Math.PI / 180;
  const apexX = G.FLAP_CX + Math.cos(a) * G.FLAP_TRIANGLE_TIP_R;
  const apexY = G.FLAP_CY + Math.sin(a) * G.FLAP_TRIANGLE_TIP_R;
  const topX  = G.FLAP_CX + Math.sin(a) * G.FLAP_R;
  const topY  = G.FLAP_CY - Math.cos(a) * G.FLAP_R;
  const botX  = G.FLAP_CX - Math.sin(a) * G.FLAP_R;
  const botY  = G.FLAP_CY + Math.cos(a) * G.FLAP_R;
  return html`
    <g data-widget="flap-circle">
      <circle cx=${G.FLAP_CX} cy=${G.FLAP_CY} r=${G.FLAP_R} fill=${colors.TFT_DARKGREY} />
      <path fill=${colors.TFT_DARKGREY} shape-rendering="crispEdges"
            d="M ${topX} ${topY} L ${apexX} ${apexY} L ${botX} ${botY} Z" />
      <circle cx=${G.FLAP_CX + G.FLAP_STOP_R} cy=${G.FLAP_CY}
              r="1" fill=${colors.TFT_WHITE} />
      <circle cx=${G.FLAP_CX + Math.cos(G.FLAP_ARC_RAD) * G.FLAP_STOP_R}
              cy=${G.FLAP_CY + Math.sin(G.FLAP_ARC_RAD) * G.FLAP_STOP_R}
              r="1" fill=${colors.TFT_WHITE} />
      <text x=${G.FLAP_CX} y=${G.FLAP_CY}
            font-family="Helvetica, Arial, sans-serif"
            font-size=${G.FLAP_LABEL_FONT_SIZE} fill=${colors.TFT_WHITE}
            text-anchor="middle" dominant-baseline="central">${flapPos ?? '—'}</text>
    </g>`;
};

// Slip ball — left/right frame brackets + ball.
// drawSlip() at main.cpp:1048-1077 draws each bracket as a 10-px-wide
// black bar + a 6-px-wide white inner bar, both spanning the full
// slip-ball height. The brackets sit ±h/2 from the slip-ball
// centerline, with the inner WHITE bar offset 2 px inward of the
// outer BLACK bar. Mode 0 calls drawSlip(80, 204, 160, 34) — wide
// frame; Mode 1/3 call drawSlip(80, 204|215, 160, 20) — same widths,
// shorter height.
//
// Frame geometry (from main.cpp:1048-1057):
//   black left bracket : x = cx - h/2 - 9, width 10
//   white left bracket : x = cx - h/2 - 7, width 6
//   black right bracket: x = cx + h/2,     width 10
//   white right bracket: x = cx + h/2 + 2, width 6
const SLIP_FRAME_OUTER_W = 10;
const SLIP_FRAME_INNER_W = 6;
const SLIP_FRAME_OUTER_OFFSET = 9;  // outer-edge offset from h/2
const SLIP_FRAME_INNER_OFFSET = 7;  // inner-edge offset from h/2
const SLIP_FRAME_RIGHT_GAP = 2;     // right-side white-bar gap from h/2

export const SlipBall = ({ lateralG, percentLift, stallWarn, flashFlag,
                            x = G.SLIP_X, y = G.SLIP_Y,
                            width = G.SLIP_W, height = G.SLIP_H }) => {
  const slip = slipFromLateralG(lateralG);
  // Per drawSlip() main.cpp:1064-1071: ball.cx = centerX +
  // slip * (W - H - 1) / 99 / 2. Range gates and color follow the
  // chevron's flashFlag for high-AOA + high-slip stall warning.
  const cx = x + width / 2 + slip * (width - height - 1) / 99 / 2;
  const cy = y + height / 2;
  const r = height / 2 - 1;
  let fill = colors.TFT_GREEN;
  if (Math.abs(slip) >= 30 && percentLift >= stallWarn) {
    fill = flashFlag ? colors.TFT_BLACK : colors.TFT_RED;
  }
  const cxMid = x + width / 2;
  return html`
    <g data-widget="slip">
      <rect x=${cxMid - height/2 - SLIP_FRAME_OUTER_OFFSET} y=${y}
            width=${SLIP_FRAME_OUTER_W} height=${height} fill=${colors.TFT_BLACK} />
      <rect x=${cxMid - height/2 - SLIP_FRAME_INNER_OFFSET} y=${y}
            width=${SLIP_FRAME_INNER_W} height=${height} fill=${colors.TFT_WHITE} />
      <rect x=${cxMid + height/2} y=${y}
            width=${SLIP_FRAME_OUTER_W} height=${height} fill=${colors.TFT_BLACK} />
      <rect x=${cxMid + height/2 + SLIP_FRAME_RIGHT_GAP} y=${y}
            width=${SLIP_FRAME_INNER_W} height=${height} fill=${colors.TFT_WHITE} />
      <circle cx=${cx} cy=${cy} r=${r} fill=${fill} />
    </g>`;
};

// Right-edge tape — bar grows up/down from zeroY by `value * heightScale`.
// Used by Mode 0 (gOnset, yellow) and Modes 1/3 (VSI, white).
export const EdgeTape = ({
  value,
  barX = G.GONSET_BAR_X, barW = G.GONSET_BAR_W,
  barColor = colors.TFT_YELLOW,
  zeroY = G.GONSET_ZERO_Y,
  heightScale = G.GONSET_HEIGHT_SCALE,
  heightMax = G.GONSET_HEIGHT_MAX,
  tickX1 = G.GONSET_TICK_X1, tickX2 = G.GONSET_TICK_X2,
  tickFirstY = G.GONSET_TICK_FIRST_Y,
  tickLastY = G.GONSET_TICK_LAST_Y,
  tickStep = G.GONSET_TICK_STEP,
  pipX1 = G.GONSET_PIP_X1, pipX2 = G.GONSET_PIP_X2,
  pipYs = [G.GONSET_PIP_Y_TOP, G.GONSET_PIP_Y_MIDDLE, G.GONSET_PIP_Y_BOT],
  tickColor = colors.TFT_GREY, pipColor = colors.TFT_GREY,
}) => {
  const h = Math.min(heightMax, Math.abs(value * heightScale));
  const top = value > 0 ? zeroY - h : zeroY;
  const ticks = [];
  for (let y = tickFirstY; y <= tickLastY; y += tickStep) {
    ticks.push(html`<line x1=${tickX1} y1=${y} x2=${tickX2} y2=${y}
                          stroke=${tickColor} stroke-width="1" />`);
  }
  return html`
    <g data-widget="edge-tape">
      <rect x=${barX} y=${top} width=${barW} height=${h} fill=${barColor} />
      ${ticks}
      ${pipYs.map(y => html`
        <line x1=${pipX1} y1=${y} x2=${pipX2} y2=${y}
              stroke=${pipColor} stroke-width="1" />`)}
    </g>`;
};

// Mode 1 horizon — sky/ground polygon driven by pitch+roll. Mirrors
// AiGraph() at main.cpp:1085-1122. Sky is a full-panel cyan rect,
// ground is a wide rotated polygon whose top edge is the horizon line.
const HALF_PI = Math.PI / 2;
export const Horizon = ({ pitchDeg, rollDeg }) => {
  const cx = G.MODE1_HORIZON_CX, cy = G.MODE1_HORIZON_CY;
  const panelW = G.M5_PANEL_W, panelH = G.M5_PANEL_H;
  const r = rollDeg * Math.PI / 180;
  const sinR = Math.sin(r), cosR = Math.cos(r);
  const pxc = cx + pitchDeg * G.MODE1_PITCH_HEIGHT_SCALE * sinR;
  const pyc = cy + pitchDeg * G.MODE1_PITCH_HEIGHT_SCALE * cosR;
  const xRot = 2 * panelW * cosR, yRot = 2 * panelW * sinR;
  const px1 = pxc - xRot, py1 = pyc + yRot;
  const px2 = pxc + xRot, py2 = pyc - yRot;
  const px3 = px1 + 3 * panelH * Math.cos( r - HALF_PI);
  const py3 = py1 - 3 * panelH * Math.sin(-r - HALF_PI);
  const px4 = px2 - 3 * panelH * Math.cos( r + HALF_PI);
  const py4 = py2 + 3 * panelH * Math.sin( r + HALF_PI);
  return html`
    <g data-widget="horizon">
      <rect x="0" y="0" width=${panelW} height=${panelH} fill=${colors.TFT_CYAN} />
      <polygon points="${px1},${py1} ${px2},${py2} ${px4},${py4} ${px3},${py3}"
               fill=${colors.TFT_BROWN} />
      <line x1=${px1} y1=${py1} x2=${px2} y2=${py2}
            stroke=${colors.TFT_BLACK} stroke-width="1" />
      <line x1=${px3} y1=${py3} x2=${px4} y2=${py4}
            stroke=${colors.TFT_BLUE} stroke-width="1" />
      <circle cx=${pxc} cy=${pyc} r="3" fill=${colors.TFT_BLACK} />
    </g>`;
};

// Mode 1 pitch ladder — tick marks every 10° from -90..+90 plus numeric
// labels on the long ticks. Mirrors pitchGraph() at main.cpp:1284-1345.
// Two passes: short ticks at half-width 0.10×g_arcSize, long ticks at
// half-width 0.20×g_arcSize plus numeric labels offset along the
// rolled axis.
export const PitchLadder = ({ pitchDeg, rollDeg }) => {
  const cx = G.MODE1_HORIZON_CX, cy = G.MODE1_HORIZON_CY;
  const pitchScale = G.MODE1_PITCH_HEIGHT_SCALE;
  const r = rollDeg * Math.PI / 180;
  const sinR = Math.sin(r), cosR = Math.cos(r);
  const pxc = cx + pitchDeg * pitchScale * sinR;
  const pyc = cy + pitchDeg * pitchScale * cosR;

  const tick = (i, halfW) => {
    const ax = pxc - i * pitchScale * sinR;
    const ay = pyc - i * pitchScale * cosR;
    const dx = halfW * cosR, dy = halfW * sinR;
    return html`<line x1=${ax - dx} y1=${ay + dy} x2=${ax + dx} y2=${ay - dy}
                       stroke=${colors.TFT_BLACK} stroke-width="1" />`;
  };
  const label = (i) => {
    const ax = pxc - i * pitchScale * sinR;
    const ay = pyc - i * pitchScale * cosR;
    const lx = ax + (G.MODE1_LADDER_LONG_HALF_W + G.MODE1_LADDER_LABEL_OFFSET) * cosR;
    const ly = ay - (G.MODE1_LADDER_LONG_HALF_W + G.MODE1_LADDER_LABEL_OFFSET) * sinR;
    return html`<text x=${lx} y=${ly}
                       font-family="'B612', 'Helvetica Neue', Arial, sans-serif"
                       font-size=${G.MODE1_LADDER_FONT_SIZE} fill=${colors.TFT_BLACK}
                       text-anchor="start" dominant-baseline="central">${i}°</text>`;
  };

  const shorts = [];
  for (let i = -G.MODE1_LADDER_SHORT_RANGE; i <= G.MODE1_LADDER_SHORT_RANGE; i += G.MODE1_LADDER_STEP_DEG) {
    shorts.push(tick(i, G.MODE1_LADDER_SHORT_HALF_W));
  }
  const longs = [];
  for (let i = -G.MODE1_LADDER_LONG_RANGE; i <= G.MODE1_LADDER_LONG_RANGE; i += G.MODE1_LADDER_STEP_DEG) {
    longs.push(tick(i, G.MODE1_LADDER_LONG_HALF_W));
    longs.push(label(i));
  }
  return html`<g data-widget="pitch-ladder">${shorts}${longs}</g>`;
};

// Mode 1 bank arc — long bars at every 30°, short bars at ±10°/±20°,
// dots at ±45°, yellow ARROW_OUT pointer at top (rotates with -roll).
export const BankArc = ({ rollDeg }) => {
  const cx = G.MODE1_HORIZON_CX, cy = G.MODE1_HORIZON_CY;
  const ARC_R = 115, ARC_W = 15;
  const LONG_INNER = ARC_R - 1.25 * ARC_W, LONG_OUTER = ARC_R + 0.25 * ARC_W, LONG_THICK = 0.25 * ARC_W;
  const SHORT_INNER = ARC_R - 0.875 * ARC_W, SHORT_OUTER = ARC_R - 0.125 * ARC_W, SHORT_THICK = 0.24 * ARC_W;
  const DEG = Math.PI / 180;
  const BAR_EDGE = '#9f9f9f';

  const radialBar = (deg, innerR, outerR, thickness, edge = BAR_EDGE) => {
    const a = deg * DEG;
    const cosA = Math.cos(a), sinA = Math.sin(a);
    const half = (thickness - 1.75) / 2;
    const ix = cx + innerR * cosA, iy = cy + innerR * sinA;
    const ox = cx + outerR * cosA, oy = cy + outerR * sinA;
    const ilx = ix + half *  sinA, ily = iy + half * -cosA;
    const olx = ox + half *  sinA, oly = oy + half * -cosA;
    const orx = ox + half * -sinA, ory = oy + half *  cosA;
    const irx = ix + half * -sinA, iry = iy + half *  cosA;
    return html`<polygon points="${ilx},${ily} ${olx},${oly} ${orx},${ory} ${irx},${iry}"
                          fill=${colors.TFT_WHITE} stroke=${edge} stroke-width="1"
                          stroke-linejoin="miter" shape-rendering="crispEdges" />`;
  };
  const dot = (deg) => {
    const a = deg * DEG;
    const r = ARC_R - 0.5 * ARC_W;
    return html`<circle cx=${cx + r * Math.cos(a)} cy=${cy + r * Math.sin(a)}
                         r=${0.25 * ARC_W} fill=${colors.TFT_WHITE}
                         stroke=${colors.TFT_BLACK} stroke-width="1" />`;
  };
  const arrow = (deg, color) => {
    const a = deg * DEG;
    const tipR = ARC_R - 0.5 * ARC_W;
    const baseR = ARC_R + 0.5 * ARC_W;
    const half = 0.33 * ARC_W;
    const cosA = Math.cos(a), sinA = Math.sin(a);
    const p4x = cx + tipR  * cosA, p4y = cy + tipR  * sinA;
    const p1x = cx + baseR * cosA, p1y = cy + baseR * sinA;
    const p2x = p1x + half *  sinA, p2y = p1y + half * -cosA;
    const p3x = p1x + half * -sinA, p3y = p1y + half *  cosA;
    return html`<polygon points="${p4x},${p4y} ${p3x},${p3y} ${p2x},${p2y}"
                          fill=${color} stroke=${colors.TFT_BLACK} stroke-width="1"
                          stroke-linejoin="miter" />`;
  };

  return html`
    <g data-widget="bank-arc" transform="rotate(${-rollDeg} ${cx} ${cy})">
      ${[0, 180, 210, 240, 300, 330].map(d => radialBar(d, LONG_INNER, LONG_OUTER, LONG_THICK))}
      ${arrow(270, colors.TFT_YELLOW)}
      ${[250, 260, 280, 290].map(d => radialBar(d, SHORT_INNER, SHORT_OUTER, SHORT_THICK))}
      ${[225, 315].map(d => dot(d))}
    </g>`;
};

// Mode 1 static aircraft reference symbol — yellow chevron with droops.
// Mirrors AiGraph() at main.cpp:1192-1231. The C++ paints 7 parallel
// stamps (y-offsets -3..+3) of a 4-segment polyline (left wing →
// inner-left → apex → inner-right → right wing). Yellow rows at
// dy=-2..+2, black at dy=±3. Wing-tip end-caps are 6-px-tall black
// vertical lines. Center is a 6-px yellow circle with 1-px black ring.
export const AircraftSymbol = () => {
  const cx = G.MODE1_HORIZON_CX, cy = G.MODE1_HORIZON_CY;
  const inner = G.MODE1_AIRCRAFT_INNER_HALF_W;
  const outer = G.MODE1_AIRCRAFT_OUTER_HALF_W;
  const droop = G.MODE1_AIRCRAFT_DROOP_DY;
  const r = G.MODE1_AIRCRAFT_CENTER_R;
  const px1 = cx - outer, px2 = cx - inner, px3 = cx + inner, px4 = cx + outer;
  const px5 = cx, py5 = cy + droop;
  const rows = [
    { dy:  0, color: colors.TFT_YELLOW },
    { dy: -1, color: colors.TFT_YELLOW },
    { dy: -2, color: colors.TFT_YELLOW },
    { dy: -3, color: colors.TFT_BLACK  },
    { dy: +1, color: colors.TFT_YELLOW },
    { dy: +2, color: colors.TFT_YELLOW },
    { dy: +3, color: colors.TFT_BLACK  },
  ];
  return html`
    <g data-widget="aircraft-symbol">
      ${rows.map(({ dy, color }) => html`
        <polyline points="${px1},${cy + dy} ${px2},${cy + dy} ${px5},${py5 + dy} ${px3},${cy + dy} ${px4},${cy + dy}"
                  fill="none" stroke=${color} stroke-width="1"
                  shape-rendering="crispEdges" />`)}
      <line x1=${px1} y1=${cy - 3} x2=${px1} y2=${cy + 2}
            stroke=${colors.TFT_BLACK} stroke-width="1" shape-rendering="crispEdges" />
      <line x1=${px4} y1=${cy - 3} x2=${px4} y2=${cy + 2}
            stroke=${colors.TFT_BLACK} stroke-width="1" shape-rendering="crispEdges" />
      <circle cx=${cx} cy=${cy} r=${r} fill=${colors.TFT_YELLOW} />
      <circle cx=${cx} cy=${cy} r=${r}
              fill="none" stroke=${colors.TFT_BLACK} stroke-width="1" />
    </g>`;
};

// Mode 1 static "this is up" yellow triangle.
export const TopPointer = () => {
  const cx = G.MODE1_HORIZON_CX, cy = G.MODE1_HORIZON_CY;
  const ARC_SIZE = 100, ARC_WIDTH = 15;
  const tipX = cx, tipY = cy - ARC_SIZE + ARC_WIDTH / 2;
  const baseLX = cx - ARC_WIDTH / 2, baseRX = cx + ARC_WIDTH / 2;
  const baseY = cy - ARC_SIZE + 2 * ARC_WIDTH;
  return html`
    <polygon points="${tipX},${tipY} ${baseLX},${baseY} ${baseRX},${baseY}"
             fill=${colors.TFT_YELLOW} stroke=${colors.TFT_BLACK} stroke-width="1"
             stroke-linejoin="miter" />`;
};

// Mode 1 flight-path marker — magenta concentric rings + perpendicular wing
// bars + top tick. Vertical-only translation by (flightPath - pitch) × scale.
export const FlightPathMarker = ({ pitchDeg, flightPathDeg }) => {
  const cx = G.MODE1_FPV_CX;
  const cy = G.MODE1_HORIZON_CY - (flightPathDeg - pitchDeg) * G.MODE1_PITCH_HEIGHT_SCALE;
  const inner = G.MODE1_FPV_WING_INNER, outer = G.MODE1_FPV_WING_OUTER;
  const bar = G.MODE1_FPV_BAR_THICKNESS;
  return html`
    <g data-widget="fpv">
      ${G.MODE1_FPV_RING_RADII.map(r => html`
        <circle cx=${cx} cy=${cy} r=${r} fill="none"
                stroke=${colors.TFT_MAGENTA} stroke-width="1" />`)}
      <line x1=${cx + inner} y1=${cy} x2=${cx + outer} y2=${cy}
            stroke=${colors.TFT_MAGENTA} stroke-width=${bar} />
      <line x1=${cx - inner} y1=${cy} x2=${cx - outer} y2=${cy}
            stroke=${colors.TFT_MAGENTA} stroke-width=${bar} />
      <line x1=${cx} y1=${cy - inner} x2=${cx} y2=${cy - outer}
            stroke=${colors.TFT_MAGENTA} stroke-width=${bar} />
    </g>`;
};

// Mode 1 pitch readout — small dark rounded rect over horizon.
export const PitchReadout = ({ pitchDeg, dataValid = true }) => {
  const txt = dataValid ? fmt(pitchDeg, 1) : '—';
  return html`
    <g data-widget="pitch-readout">
      <rect x=${G.MODE1_PITCH_READOUT_X} y=${G.MODE1_PITCH_READOUT_Y}
            width=${G.MODE1_PITCH_READOUT_W} height=${G.MODE1_PITCH_READOUT_H}
            rx=${G.MODE1_PITCH_READOUT_RADIUS} ry=${G.MODE1_PITCH_READOUT_RADIUS}
            fill=${colors.TFT_DARKGREY} />
      <text x=${G.MODE1_PITCH_READOUT_TEXT_X} y=${G.MODE1_PITCH_READOUT_TEXT_Y}
            font-family="Helvetica, Arial, sans-serif" font-weight="bold"
            font-size=${G.MODE1_PITCH_READOUT_FONT_SIZE} fill=${colors.TFT_WHITE}
            text-anchor="middle" dominant-baseline="central">${txt}</text>
      <circle cx=${G.MODE1_PITCH_READOUT_DEG_CX} cy=${G.MODE1_PITCH_READOUT_DEG_CY}
              r=${G.MODE1_PITCH_READOUT_DEG_R} fill="none"
              stroke=${colors.TFT_WHITE} stroke-width="1" />
    </g>`;
};

// Mode 3 (Energy) decel gauge — vertical band gauge with sliding pointer.
export const DecelGauge = ({ decelRate, dataValid = true }) => {
  const labels = G.MODE3_GAUGE_LABELS;
  let pointerY = G.MODE3_DECEL_SCALE * (decelRate || 0) + G.MODE3_DECEL_OFFSET - G.MODE3_POINTER_HALF_H;
  pointerY = Math.max(G.MODE3_POINTER_Y_MIN, Math.min(G.MODE3_POINTER_Y_MAX, pointerY));
  return html`
    <g data-widget="decel-gauge">
      <rect x=${G.MODE3_GAUGE_X} y=${G.MODE3_GAUGE_Y}
            width=${G.MODE3_GAUGE_W} height=${G.MODE3_GAUGE_H}
            rx=${G.MODE3_GAUGE_RADIUS} ry=${G.MODE3_GAUGE_RADIUS}
            fill=${colors.TFT_RED} />
      <rect x=${G.MODE3_GAUGE_GREEN_X} y=${G.MODE3_GAUGE_GREEN_Y}
            width=${G.MODE3_GAUGE_GREEN_W} height=${G.MODE3_GAUGE_GREEN_H}
            fill=${colors.TFT_GREEN} />
      <rect x=${G.MODE3_GAUGE_X} y=${G.MODE3_GAUGE_Y}
            width=${G.MODE3_GAUGE_W} height=${G.MODE3_GAUGE_H}
            rx=${G.MODE3_GAUGE_RADIUS} ry=${G.MODE3_GAUGE_RADIUS}
            fill="none" stroke=${colors.TFT_LIGHTGREY} stroke-width="1" />
      ${dataValid && html`
        <rect x=${G.MODE3_POINTER_X} y=${pointerY}
              width=${G.MODE3_POINTER_W} height=${G.MODE3_POINTER_H}
              fill=${colors.TFT_WHITE} stroke=${colors.TFT_BLACK} stroke-width="1" />`}
      ${labels.map(l => html`
        <text x=${G.MODE3_GAUGE_LABEL_X} y=${l.y}
              font-family="Helvetica, Arial, sans-serif"
              font-size=${G.MODE3_GAUGE_LABEL_FONT_SIZE} fill=${colors.TFT_WHITE}
              text-anchor="end" dominant-baseline="central">${l.text}</text>
        <line x1=${G.MODE3_PIP_X1} y1=${l.y} x2=${G.MODE3_PIP_X2} y2=${l.y}
              stroke=${colors.TFT_LIGHTGREY} stroke-width="1" />`)}
    </g>`;
};

// Mode 4 G-load history — frame + 300-dot strip chart.
// Color per dot: green ≥ 1G, yellow 0..1, red < 0. Buffer fill is
// 1.0G initially so the chart starts visually flat.
// cx values for the 300 strip-chart dots are static (one column per
// pixel from x=319 down to x=20). Compute once, reuse every render.
const _GHISTORY_CXS = (() => {
  const out = new Array(G.MODE4_BUFFER_LEN);
  for (let i = 0; i < G.MODE4_BUFFER_LEN; i++) {
    out[i] = G.MODE4_TRACE_X_MAX - i;
  }
  return out;
})();

export const GHistory = ({ buf, writeIdx }) => {
  const dots = [];
  const N = G.MODE4_BUFFER_LEN;
  let sampleIdx = writeIdx;
  for (let i = 0; i < N; i++) {
    const g = buf[sampleIdx];
    let cy = G.MODE4_DOT_Y_OFFSET - g * G.MODE4_DOT_Y_SCALE;
    if (cy < G.MODE4_DOT_Y_MIN) cy = G.MODE4_DOT_Y_MIN;
    else if (cy > G.MODE4_DOT_Y_MAX) cy = G.MODE4_DOT_Y_MAX;
    const fill = g >= 1 ? colors.TFT_GREEN : g >= 0 ? colors.TFT_YELLOW : colors.TFT_RED;
    dots.push(html`<circle cx=${_GHISTORY_CXS[i]} cy=${cy} r=${G.MODE4_DOT_R} fill=${fill} />`);
    sampleIdx = (sampleIdx + 1) % N;
  }
  return html`
    <g data-widget="g-history">
      ${G.MODE4_GRIDLINE_YS.map(y => html`
        <line x1=${G.MODE4_AXIS_X} y1=${y} x2=${G.MODE4_TRACE_X_MAX} y2=${y}
              stroke=${colors.TFT_GREY} stroke-width="1" />`)}
      <line x1=${G.MODE4_AXIS_X} y1=${G.MODE4_ONE_G_Y}
            x2=${G.MODE4_TRACE_X_MAX} y2=${G.MODE4_ONE_G_Y}
            stroke=${colors.TFT_WHITE} stroke-width="1" />
      <line x1=${G.MODE4_AXIS_X} y1=${G.MODE4_AXIS_Y_TOP}
            x2=${G.MODE4_AXIS_X} y2=${G.MODE4_AXIS_Y_BOT}
            stroke=${colors.TFT_WHITE} stroke-width="1" />
      ${G.MODE4_PIP_LABELS.map(p => html`
        <text x=${G.MODE4_PIP_LABEL_X} y=${p.y}
              font-family="Helvetica, Arial, sans-serif"
              font-size=${G.MODE4_PIP_FONT_SIZE} fill=${colors.TFT_WHITE}
              text-anchor="end" dominant-baseline="central">${p.text}</text>`)}
      <text x=${G.MODE4_HEADER_X} y=${G.MODE4_HEADER_Y}
            font-family="Helvetica, Arial, sans-serif"
            font-size=${G.MODE4_HEADER_FONT_SIZE} fill=${colors.TFT_WHITE}
            text-anchor="middle" dominant-baseline="hanging">
        ${G.MODE4_HEADER_TEXT}</text>
      ${dots}
    </g>`;
};

// M5-style stale-data overlay: red X across panel + black "NO DATA" pill.
// Mounted last on each mode SVG so it paints on top when visible.
//
// Each diagonal is built from a band of 9 parallel 1px lines (offset
// -4..+4) so the X reads as a thick stroke without relying on
// stroke-width which would foreshorten near the corners.  The previous
// implementation moved only one endpoint of the bottom-left → top-right
// diagonal per offset, fanning the band out from the corners and
// rendering visibly thinner than its mirror; both diagonals now offset
// both endpoints symmetrically for a uniform-thickness X.
export const StaleOverlay = ({ stale }) => {
  if (!stale) return null;
  const lines = [];
  for (let off = -4; off <= 4; ++off) {
    // Top-left → bottom-right diagonal.  Positive off shifts the band
    // down-right, negative shifts up-left.
    const tlbr_pos = Math.max(0, off);
    const tlbr_neg = -Math.min(0, off);
    lines.push(html`
      <line x1=${tlbr_pos}       y1=${tlbr_neg}
            x2=${319 - tlbr_neg} y2=${239 - tlbr_pos}
            stroke=${colors.TFT_RED} stroke-width="1" />`);
    // Top-right → bottom-left diagonal.  Mirror of the above so the
    // band has matching thickness.
    lines.push(html`
      <line x1=${319 - tlbr_pos} y1=${tlbr_neg}
            x2=${tlbr_neg}       y2=${239 - tlbr_pos}
            stroke=${colors.TFT_RED} stroke-width="1" />`);
  }
  return html`
    <g data-widget="stale-overlay">
      <rect x="0" y="0" width="320" height="240" fill=${colors.TFT_BLACK} />
      ${lines}
      <rect x="100" y="100" width="120" height="40" fill=${colors.TFT_BLACK} />
      <text x="160" y="120"
            font-family="Helvetica, Arial, sans-serif" font-weight="bold"
            font-size="24" fill=${colors.TFT_WHITE}
            text-anchor="middle" dominant-baseline="central">NO DATA</text>
    </g>`;
};
