// All layout constants extracted from software/OnSpeed-M5-Display/src/main.cpp
// (and helpers from drawAOA/drawSlip/displayAOA). Coordinate system is the
// M5's native 320×240 panel; SVG viewBox matches.
//
// Naming convention: M5_<COMPONENT>_<DIMENSION>.

// ----------------------------------------------------------------------------
// Panel + indexer widget
// ----------------------------------------------------------------------------

export const M5_PANEL_W = 320;
export const M5_PANEL_H = 240;

// displayAOA() at main.cpp:717: wgtX0/Y0/Width/Height set in case 0
// (Primary): wgtWidth=102, wgtHeight=192, wgtX0=(WIDTH-102)/2=109, wgtY0=0.
// drawAOA() at :889 immediately re-centers: X0 = wgtX0 + W/2; Y0 = wgtY0 + H/2.
export const INDEXER_WIDTH  = 102;
export const INDEXER_HEIGHT = 192;
export const INDEXER_X      = (M5_PANEL_W - INDEXER_WIDTH) / 2;  // 109
export const INDEXER_Y      = 0;
export const INDEXER_CX     = INDEXER_X + INDEXER_WIDTH / 2;     // 160
export const INDEXER_CY     = INDEXER_Y + INDEXER_HEIGHT / 2;    // 96

// drawAOA() bounding box: drawRoundRect at :899 with corner radius 5.
export const INDEXER_BOX_RADIUS = 5;

// ----------------------------------------------------------------------------
// Chevron geometry — top + bottom, two halves each (left and right of center).
// drawAOA() :902-:1003. The pre-rotation rect is Px0..Px1 = ±W/12 by Py0..Py1
// = ±H/4. Rotation ±π/8 about each half's center.
// ----------------------------------------------------------------------------

export const CHEVRON_HALF_W = INDEXER_WIDTH / 12;   // 8.5
export const CHEVRON_HALF_H = INDEXER_HEIGHT / 4;   // 48
export const CHEVRON_ROTATION_RAD = Math.PI / 8;    // ±π/8 (±22.5°)

// Half-centers (the X0±W/4, Y0±H/4 anchor points in drawAOA):
export const CHEVRON_TOP_LEFT_CX     = INDEXER_CX - INDEXER_WIDTH / 4;  // 134.5
export const CHEVRON_TOP_LEFT_CY     = INDEXER_CY - INDEXER_HEIGHT / 4; // 48
export const CHEVRON_TOP_RIGHT_CX    = INDEXER_CX + INDEXER_WIDTH / 4;  // 185.5
export const CHEVRON_TOP_RIGHT_CY    = INDEXER_CY - INDEXER_HEIGHT / 4; // 48
export const CHEVRON_BOTTOM_LEFT_CX  = INDEXER_CX - INDEXER_WIDTH / 4;  // 134.5
export const CHEVRON_BOTTOM_LEFT_CY  = INDEXER_CY + INDEXER_HEIGHT / 4; // 144
export const CHEVRON_BOTTOM_RIGHT_CX = INDEXER_CX + INDEXER_WIDTH / 4;  // 185.5
export const CHEVRON_BOTTOM_RIGHT_CY = INDEXER_CY + INDEXER_HEIGHT / 4; // 144

// ----------------------------------------------------------------------------
// Donut — three independently-lit segments.
// drawAOA() :1005-:1031.
// ----------------------------------------------------------------------------

// drawAOA() :1008: bullsEye = H × (65 - 55 - 2) / 200 = 192 × 8 / 200 = 7.68
//   (the C++ uses integer math so it rounds to 7; we use the float here to
//   match the visual size precisely. Either is fine within 1 px.)
export const DONUT_BULLSEYE_R = INDEXER_HEIGHT * 8 / 200; // 7.68
// :1009 black surround radius = bullsEye + H/12 = 7.68 + 16 = 23.68
export const DONUT_BLACK_R = DONUT_BULLSEYE_R + INDEXER_HEIGHT / 12;
// :1012 arc radius = bullsEye + H/16 = 7.68 + 12 = 19.68
export const DONUT_ARC_R = DONUT_BULLSEYE_R + INDEXER_HEIGHT / 16;
// :1013 line width
export const DONUT_ARC_LINEWIDTH = 8;
// :1026 black gap rect between top and bottom arcs
export const DONUT_GAP_X = INDEXER_CX - INDEXER_WIDTH / 3;     // 126
export const DONUT_GAP_Y = INDEXER_CY - INDEXER_HEIGHT / 48;   // 92
export const DONUT_GAP_W = 2 * INDEXER_WIDTH / 3;              // 68
export const DONUT_GAP_H = INDEXER_HEIGHT / 24;                // 8
// :1031 center dot radius = bullsEye + 2
export const DONUT_DOT_R = DONUT_BULLSEYE_R + 2;

// ----------------------------------------------------------------------------
// Index bar (the moving white horizontal that shows current AOA).
// drawAOA() :1037: rect (X0 - W/2, indexY, W, H/24).
// ----------------------------------------------------------------------------

export const INDEX_BAR_X      = INDEXER_X;                     // 109
export const INDEX_BAR_W      = INDEXER_WIDTH;                 // 102
export const INDEX_BAR_H      = INDEXER_HEIGHT / 24;           // 8

// ----------------------------------------------------------------------------
// L/Dmax pip dots — black halo + white inner.
// drawAOA() :1049-:1052.
// ----------------------------------------------------------------------------

export const PIP_LEFT_CX  = INDEXER_X;                          // 109
export const PIP_RIGHT_CX = INDEXER_X + INDEXER_WIDTH - 1;      // 210
export const PIP_HALO_R   = INDEXER_HEIGHT / 24;                // 8
export const PIP_INNER_R  = INDEXER_HEIGHT / 32;                // 6

// ----------------------------------------------------------------------------
// Percent-lift number above the indexer.
// displayAOA() :739-:760.
// ----------------------------------------------------------------------------

// Centered above the indexer (matches INDEXER_CX). The C++ uses a hardcoded
// PERCENT_X_POS=140 with the FSSB18 bitmap font's intrinsic kerning to land
// over the chevron. For SVG with a different font we use INDEXER_CX (160)
// with text-anchor: middle as the right approximation.
export const PCT_LIFT_X = 160;
export const PCT_LIFT_Y = 27;   // PERCENT_Y_POS at :740
// Font: FSSB18 (FreeSans Bold 18pt). User-measured at the prototype's
// 1:1 render scale: M5 FSSB18 digits are ~28 px tall; CSS font-size 36
// matches the visual height at the same baseline-y.
export const PCT_LIFT_FONT_SIZE = 36;
// Outline: C++ stamps 9 black copies at ±3 offset (a 6 px-wide black
// halo around the white digits).  In SVG this is a single text element
// rendered with stroke-width = OUTLINE_PX * 2; the doubling makes the
// halo visibly wider than the 8 px-tall index bar so the white digits
// don't blend in.  3 reads as a tight black outline (~6 px stroke); 5
// reads heavy.
export const PCT_LIFT_OUTLINE_PX = 3;

// ----------------------------------------------------------------------------
// Corner readouts — IAS top-left, G top-right.
// displayAOA() :764-:786.
// ----------------------------------------------------------------------------

export const CORNER_RIGHT_X  = 303;  // RIGHT_X
export const CORNER_LABEL_Y  = 90;
export const CORNER_NUM_Y    = 130;
export const CORNER_LEFT_X   = 5;
// Label font: FSS18 (FreeSans 18pt). Number font: FSSB18 (FreeSans Bold 18pt).
// User-measured at 1:1 prototype render scale: CSS font-size 33 with
// browser Helvetica matches the M5 wasm-live FSS18/FSSB18 visual size
// closely. Label baseline y=90, number baseline y=130 — gap is from
// C++, do not adjust.
export const CORNER_LABEL_FONT_SIZE = 33;
export const CORNER_NUM_FONT_SIZE   = 33;

// ----------------------------------------------------------------------------
// Flap circle widget.
// displayAOA() :790-:835.
// ----------------------------------------------------------------------------

export const FLAP_CX = 23;
export const FLAP_CY = 204;
export const FLAP_R  = 16;
// Triangle apex sits at radius FLAP_R + 33.
export const FLAP_TRIANGLE_TIP_R = FLAP_R + 33;
// Stop-mark dots at the arc endpoints sit at the same radius as the apex
// (main.cpp:809 stopRadius = Radius + 33).
export const FLAP_STOP_R = FLAP_R + 33;
// Numeric flap angle inside the circle. C++ uses FSS12 + middle_center
// at (cx, cy). User-measured: at font-size 18 the digits read 1.2 units
// tall; bumped to 23 to land at the target 1.5 units (matching the
// M5 wasm-live visual size).
export const FLAP_LABEL_FONT_SIZE = 23;
// Visual arc the triangle sweeps (kFlapArcDeg).
export const FLAP_ARC_DEG = 40;
export const FLAP_ARC_RAD = FLAP_ARC_DEG * Math.PI / 180;

// ----------------------------------------------------------------------------
// Slip ball.
// drawSlip() at :1060, called from displayAOA() :840 with (80, 204, 160, 34).
// ----------------------------------------------------------------------------

export const SLIP_X = 80;
export const SLIP_Y = 204;
export const SLIP_W = 160;
export const SLIP_H = 34;
export const SLIP_CENTER_X = SLIP_X + SLIP_W / 2;  // 160
export const SLIP_CENTER_Y = SLIP_Y + SLIP_H / 2;  // 221
export const SLIP_BALL_R   = SLIP_H / 2 - 1;       // 16
// Ball x offset from center: slipValue × (W - H - 1) / 99 / 2.
// The displayed slip value range is ±99 (clamped).
export const SLIP_BALL_X_RANGE = (SLIP_W - SLIP_H - 1) / 2;  // 62.5

// ----------------------------------------------------------------------------
// Mode 1: Attitude / Backup AI
// ----------------------------------------------------------------------------
// Source: software/OnSpeed-M5-Display/src/main.cpp
//   - case 1 block at lines 521-624 (corner readouts, slip ball, pitch
//     readout, VSI tape).
//   - AiGraph() at lines 1075-1279 (horizon polygon, aircraft symbol,
//     flight path marker).
//   - pitchGraph() at lines 1284-1345 (pitch ladder ticks + labels).
//   - Globals at lines 134-135, 208-210 (WIDTH=320, HEIGHT=240,
//     g_px0=159, g_py0=119, g_arcSize=115).
// ----------------------------------------------------------------------------

// Horizon / aircraft symbol center. main.cpp:208-209 (g_px0, g_py0).
export const MODE1_HORIZON_CX = 159;
export const MODE1_HORIZON_CY = 119;

// Pixels per degree of pitch. main.cpp:1096 uses `HEIGHT/80` (240/80 = 3),
// and the FPV at :1255 uses `120/40 = 3`. Both produce the same scale.
export const MODE1_PITCH_HEIGHT_SCALE = 3;

// Pitch ladder. main.cpp:1310 (short ticks every 10° from -85..+85),
// :1328 (long ticks + numeric labels every 10° from -90..+90). The
// short half-width is 10% of g_arcSize (:1301-1302); the long
// half-width is 20%. g_arcSize=115 (:210), so short=11.5, long=23.
export const MODE1_LADDER_STEP_DEG     = 10;
export const MODE1_LADDER_SHORT_HALF_W = 0.10 * 115;  // ~11.5
export const MODE1_LADDER_LONG_HALF_W  = 0.20 * 115;  // ~23
export const MODE1_LADDER_SHORT_RANGE  = 85;          // -85..+85
export const MODE1_LADDER_LONG_RANGE   = 90;          // -90..+90
// Label x offset past the long tick end. C++ at :1340 uses
// `xRotate*0.75` where xRotate = 0.10 × g_arcSize. So the offset is
// 0.075 × g_arcSize ≈ 8.625 px along the rolled axis — NOT
// 0.75 × longHalfW (which would be ~17 px and place the digits much
// further from the tick).
export const MODE1_LADDER_LABEL_OFFSET = 0.075 * 115;        // ~8.625
// Label font size. C++ uses FSS12 (~14 px tall). LESSONS rule: ~1.25× scale.
export const MODE1_LADDER_FONT_SIZE = 16;

// Aircraft reference symbol. main.cpp:1178-1231.
// arcSize=100 inside AiGraph (:1178); the symbol uses arcSize, not
// g_arcSize. Wing inner edge = arcSize/4 = 25; outer = arcSize = 100.
export const MODE1_AIRCRAFT_ARC_SIZE       = 100;
export const MODE1_AIRCRAFT_INNER_HALF_W   = 25;   // arcSize/4 (:1183-1186)
export const MODE1_AIRCRAFT_OUTER_HALF_W   = 100;  // arcSize    (:1181, :1187)
export const MODE1_AIRCRAFT_WING_HALF_LEN  = 75;   // 3*arcSize/4 (:1195)
export const MODE1_AIRCRAFT_DROOP_DY       = 25;   // arcSize/4 (:1190)
// Center circle: 2 degree radius in pitch units = 2 × HEIGHT/80 = 6 px (:1192).
export const MODE1_AIRCRAFT_CENTER_R       = 6;
// Wing/droop bar thickness — C++ stamps 7 parallel 1-px lines (-3..+3),
// SVG renders that as a stroked path. 7 px tall total.
export const MODE1_AIRCRAFT_BAR_THICKNESS  = 7;

// Flight path marker. main.cpp:1255-1277.
// Concentric magenta rings at radii 12, 13, 14. Wing bars are 3 px tall
// (drawLine y-1, y, y+1) extending from x±14 to x±33. Top tick from
// y-14 to y-33, 3 px wide.
export const MODE1_FPV_CX            = 159;        // :1257
export const MODE1_FPV_RING_RADII    = [12, 13, 14];
export const MODE1_FPV_WING_INNER    = 14;
export const MODE1_FPV_WING_OUTER    = 33;
export const MODE1_FPV_BAR_THICKNESS = 3;

// Pitch readout — small dark rounded rectangle over horizon line.
// main.cpp:561-573.
export const MODE1_PITCH_READOUT_X      = 55;
export const MODE1_PITCH_READOUT_Y      = 129;
export const MODE1_PITCH_READOUT_W      = 56;
export const MODE1_PITCH_READOUT_H      = 21;
export const MODE1_PITCH_READOUT_RADIUS = 3;
// Text horizontally centered inside the box (x = 55 + 56/2 = 83) and
// vertically centered (y = 129 + 21/2 ~= 139). C++ uses middle_right
// at (100, 138) but the user wants the digit centered within the dark
// rect rather than right-aligned at the degree symbol — the box
// itself is the visual contrast region, and "0.0" off-center reads as
// asymmetric.
export const MODE1_PITCH_READOUT_TEXT_X = 83;
// y=140: the dark rect spans y=129..150 (true vertical center 139.5).
// User polish-pass-5: y=141 read too low next to the degree symbol
// (whose center is at y=132 — higher than the M5's FSS12 baseline)
// because the digit baseline is what the eye reads as "the row" and
// at y=141 the digit cap-tops sit ~5 px below the degree symbol top.
// y=140 reduces that to ~4 px and looks more like the M5's bitmap
// rendering where the digit and degree symbol read as the same row.
export const MODE1_PITCH_READOUT_TEXT_Y = 140;
// Degree symbol — drawCircle(106, 132, 2.5) at :573.
export const MODE1_PITCH_READOUT_DEG_CX = 106;
export const MODE1_PITCH_READOUT_DEG_CY = 132;
export const MODE1_PITCH_READOUT_DEG_R  = 2.5;
// Pitch number font. User target: 1.5 grid units (~15 px) cap height.
// Browser Helvetica at font-size 20 lands ~14.5 px cap height — close
// enough to the target. Font-size 16 read as too short (~12 px cap).
export const MODE1_PITCH_READOUT_FONT_SIZE = 20;

// Mode 1 corner readouts. main.cpp:527-595.
// Left-side label baseline x. Per polish-pass-5: digits should
// left-align flush with the label's left edge — the +2 offset from
// polish-pass-3 read as drifted right relative to "IAS" / "G".
export const MODE1_CORNER_LEFT_X       = 5;
export const MODE1_CORNER_LEFT_NUM_X   = 5;
export const MODE1_CORNER_RIGHT_X      = 307;   // :532
// Right-side numbers should align flush with the label's right edge.
// Polish-pass-5: both PALT (top) and AOA (bottom) numbers go to x=307
// to match the label's anchor x exactly — earlier offsets to 301/306
// over-corrected for digit metrics and read as drifted left.
export const MODE1_CORNER_TOP_RIGHT_NUM_X = 307;
export const MODE1_CORNER_BOT_RIGHT_NUM_X = 307;
export const MODE1_CORNER_TOP_LABEL_Y  = 62;    // :540
export const MODE1_CORNER_BOT_LABEL_Y  = 230;   // :541
// Top numbers (IAS, PALT) center vertically on the topmost VSI tick
// at y=19 (MODE1_VSI_TICK_FIRST_Y). User polish-pass-4: the digits
// were sitting ~10 px low (centered at y=29 instead of 19) — the tick
// is a stronger visual anchor than the pitch ladder's i=-30 line, and
// the user reads "centered on the first horizontal tick" as the
// correct alignment. With dominant-baseline "central" the digit center
// lands exactly on the tick line.
export const MODE1_CORNER_TOP_NUM_Y    = 19;
export const MODE1_CORNER_BOT_NUM_Y    = 198;   // :588, :593
// Mode 1 uses FSS12 for labels (:542) and FSSB18 for numbers (:576).
// User target after polish-pass-3 review: corner numbers at 2.5 grid
// units (~25 px cap height). At browser Helvetica's ~73% cap-to-em
// ratio, font-size 35 lands ~25.5 px cap. The size-45 from polish-2
// over-shot at 3.3 units.
export const MODE1_CORNER_LABEL_FONT_SIZE = 22;
export const MODE1_CORNER_NUM_FONT_SIZE   = 35;

// Mode 1 slip ball. main.cpp:598 — drawSlip(80, 204, 160, 20).
// SHORTER than Mode 0's 34-px height. Reuse mountSlipBall with these.
export const MODE1_SLIP_X = 80;
export const MODE1_SLIP_Y = 204;
export const MODE1_SLIP_W = 160;
export const MODE1_SLIP_H = 20;

// Mode 1 VSI tape. main.cpp:600-621.
// Same edgeTape widget as Mode 0's gOnset, but with VSI scaling and
// white bar color (orange was unreadable against the attitude-page
// background). height = |iVSI × 120 / 600|, clamped 0..120.
// Mirror constants kVsiBarHeightPx + kVsiBarFullScaleFpm in
// software/OnSpeed-M5-Display/src/main.cpp.
export const MODE1_VSI_BAR_X        = 313;       // :609
export const MODE1_VSI_BAR_W        = 7;
export const MODE1_VSI_HEIGHT_MAX   = 120;
export const MODE1_VSI_FULL_SCALE_FPM = 600;
export const MODE1_VSI_HEIGHT_SCALE =
    MODE1_VSI_HEIGHT_MAX / MODE1_VSI_FULL_SCALE_FPM; // 0.2 px/fpm
export const MODE1_VSI_ZERO_Y       = 119;       // :607-608
// Tick ladder: every 20 px from y=19 to y=219 (:613).
// Tick endpoints. Per user polish-pass-3: ticks must touch the right
// edge of the panel (x=320), not stop one pixel short.
export const MODE1_VSI_TICK_X1      = 313;
export const MODE1_VSI_TICK_X2      = 320;
export const MODE1_VSI_TICK_FIRST_Y = 19;
export const MODE1_VSI_TICK_LAST_Y  = 219;
export const MODE1_VSI_TICK_STEP    = 20;
// Zero pip — 3 horizontal lines at y=118,119,120 (:619-621).
export const MODE1_VSI_PIP_X1       = 306;
export const MODE1_VSI_PIP_X2       = 312;
export const MODE1_VSI_PIP_Y_TOP    = 118;
export const MODE1_VSI_PIP_Y_MIDDLE = 119;
export const MODE1_VSI_PIP_Y_BOT    = 120;

// ----------------------------------------------------------------------------
// Mode 3: Energy / decel gauge
// ----------------------------------------------------------------------------
// Source: software/OnSpeed-M5-Display/src/main.cpp::displayDecelGauge()
// at lines 1350-1432.
// ----------------------------------------------------------------------------

// Vertical band gauge: 102×210 rounded rect spanning the central column.
// main.cpp:1353-1355.
export const MODE3_GAUGE_X      = 109;
export const MODE3_GAUGE_Y      = 1;
export const MODE3_GAUGE_W      = 102;
export const MODE3_GAUGE_H      = 210;
export const MODE3_GAUGE_RADIUS = 5;
// Green band inside the red gauge (the "OnSpeed" target zone).
// main.cpp:1354.
export const MODE3_GAUGE_GREEN_X = 109;
export const MODE3_GAUGE_GREEN_Y = 87;
export const MODE3_GAUGE_GREEN_W = 102;
export const MODE3_GAUGE_GREEN_H = 36;

// Pointer is 7 px tall, full gauge width.
// main.cpp:1357-1362. Position formula:
//   decelIndex = 35.143 * SmoothedDecelRate + 141.48 - 3.5
//   clamped to [2, 205] so the pointer never escapes the gauge.
// 3.5 is half the pointer height (7/2). At decelRate=0 the pointer
// CENTER lands at y=141 (decelIndex=138 + 3.5).
export const MODE3_POINTER_W           = 102;
export const MODE3_POINTER_H           = 7;
export const MODE3_POINTER_X           = 109;
export const MODE3_DECEL_SCALE         = 35.143;     // px per kt/s
export const MODE3_DECEL_OFFSET        = 141.48;     // y at decelRate=0 (center)
export const MODE3_POINTER_HALF_H      = 3.5;
export const MODE3_POINTER_Y_MIN       = 2;
export const MODE3_POINTER_Y_MAX       = 205;

// Gauge labels at left of gauge — drawn middle_right at (95, *).
// main.cpp:1368-1372.
export const MODE3_GAUGE_LABEL_X = 95;
export const MODE3_GAUGE_LABELS  = [
  { text: '-3', y:  36 },
  { text: '-2', y:  72 },
  { text: '-1', y: 106 },
  { text:  '0', y: 141 },
  { text:  '1', y: 177 },
];
// Pip ticks aligned with each label. main.cpp:1376-1380.
export const MODE3_PIP_X1 = 99;
export const MODE3_PIP_X2 = 107;
// Gauge label font: FSS9 in C++ (small). User polish: target ~1.1
// grid units cap height (~11 px). Browser Helvetica at font-size 15
// lands ~11 px cap, matching the M5 wasm-live FSS9 visual size.
export const MODE3_GAUGE_LABEL_FONT_SIZE = 15;

// VSI tape — same geometry as Mode 1's VSI tape, but tick color is
// TFT_LIGHT_GREY (main.cpp:1397) instead of TFT_BLACK.
// All other constants (bar x/w, height scale, tick step, zero pip)
// match MODE1_VSI_*.

// Slip ball — y=215 (vs y=204 for Mode 0/1). main.cpp:1406.
export const MODE3_SLIP_X = 80;
export const MODE3_SLIP_Y = 215;
export const MODE3_SLIP_W = 160;
export const MODE3_SLIP_H = 20;

// Corner readouts — IAS top-left, Kt/s top-right. Same y values as
// Mode 0's IAS/G corners. main.cpp:1409-1431.
export const MODE3_CORNER_LEFT_X    = 5;
export const MODE3_CORNER_LEFT_NUM_X = 7;       // matches C++ :1425
export const MODE3_CORNER_RIGHT_X   = 303;
export const MODE3_CORNER_LABEL_Y   = 90;
export const MODE3_CORNER_NUM_Y     = 130;

// ----------------------------------------------------------------------------
// Mode 4: G-load history (60 s strip chart)
// ----------------------------------------------------------------------------
// Source: software/OnSpeed-M5-Display/src/main.cpp::displayGloadHistory()
// at lines 1437-1493. Sample loop at lines 465-471 (5 Hz gate).
// ----------------------------------------------------------------------------

// 300 samples × 5 Hz = 60 s, drawn at x=20..319 (one column per sample).
export const MODE4_BUFFER_LEN  = 300;
export const MODE4_SAMPLE_MS   = 200;     // 5 Hz gate, main.cpp:465
export const MODE4_TRACE_X_MIN = 20;
export const MODE4_TRACE_X_MAX = 319;

// Frame: vertical axis at x=19, 1 G horizontal line at y=133 (white).
// main.cpp:1441-1444.
export const MODE4_AXIS_X       = 19;
export const MODE4_ONE_G_Y      = 133;
export const MODE4_AXIS_Y_TOP   = 0;
export const MODE4_AXIS_Y_BOT   = 239;

// 7 grey gridlines (above + below 1G), main.cpp:1446-1453.
export const MODE4_GRIDLINE_YS = [27, 53, 80, 106, 160, 186, 213];

// Pip labels at left of axis (middle_right at x=18). main.cpp:1461-1468.
// Each entry is { text, y }.
export const MODE4_PIP_LABEL_X = 18;
export const MODE4_PIP_LABELS  = [
  { text:  '5', y:  27 },
  { text:  '4', y:  53 },
  { text:  '3', y:  80 },
  { text:  '2', y: 106 },
  { text:  '1', y: 133 },
  { text:  '0', y: 160 },
  { text: '-1', y: 186 },
  { text: '-2', y: 213 },
];
// FSS12 in C++. User polish: 24 read crowded against the gridlines
// (the gridlines are ~26 px apart so 1.8-unit caps eat the whole
// row), 16 read too small. 20 splits the difference: ~14.5 px cap,
// ~1.45 units, leaves visible breathing room above/below each pip.
export const MODE4_PIP_FONT_SIZE = 20;

// Header "G-LOAD [1 min]" — top_center at (160, 2). main.cpp:1471-1473.
// Header uses the same FSS12 as pips. Matched to pip size for
// visual consistency.
export const MODE4_HEADER_TEXT = 'G-LOAD [1 min]';
export const MODE4_HEADER_X = 160;
export const MODE4_HEADER_Y = 2;
export const MODE4_HEADER_FONT_SIZE = 20;

// G to y conversion: y = 160 - g*26.67, clamped 0..239. main.cpp:1481.
export const MODE4_DOT_Y_OFFSET = 160;
export const MODE4_DOT_Y_SCALE  = 26.67;
export const MODE4_DOT_Y_MIN    = 0;
export const MODE4_DOT_Y_MAX    = 239;
export const MODE4_DOT_R        = 2;

// ----------------------------------------------------------------------------
// G-onset right-edge tape.
// displayAOA() :845-:868.
// ----------------------------------------------------------------------------

export const GONSET_BAR_X      = 313;
export const GONSET_BAR_W      = 7;
// :847: gOnsetHeight = |gOnsetRate × 60|, then constrain(0, 120).
// (60 because main.cpp uses /2 then ×120 — verify in source.)
export const GONSET_HEIGHT_SCALE = 60;
export const GONSET_HEIGHT_MAX   = 120;
export const GONSET_ZERO_Y       = 119;  // :850 if positive: top = 119 - height
// Tick ladder. After PR #351 (pip alignment fix), start=14 step=15.
export const GONSET_TICK_X1   = 313;
export const GONSET_TICK_X2   = 319;
export const GONSET_TICK_FIRST_Y = 14;
export const GONSET_TICK_STEP    = 15;
export const GONSET_TICK_LAST_Y  = 224;
// Zero pip — 3 horizontal 7-px lines.
export const GONSET_PIP_X1 = 306;
export const GONSET_PIP_X2 = 312;
export const GONSET_PIP_Y_TOP    = 118;
export const GONSET_PIP_Y_MIDDLE = 119;
export const GONSET_PIP_Y_BOT    = 120;
