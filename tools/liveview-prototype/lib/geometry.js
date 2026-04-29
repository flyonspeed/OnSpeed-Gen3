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
// Label x offset past the long tick end (xRotate*0.75 in C++ at :1340).
export const MODE1_LADDER_LABEL_OFFSET = 0.75 * 0.20 * 115;  // ~17.25
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
// Text drawn middle_right at (100, 138) — :570.
export const MODE1_PITCH_READOUT_TEXT_X = 100;
export const MODE1_PITCH_READOUT_TEXT_Y = 138;
// Degree symbol — drawCircle(106, 132, 2.5) at :573.
export const MODE1_PITCH_READOUT_DEG_CX = 106;
export const MODE1_PITCH_READOUT_DEG_CY = 132;
export const MODE1_PITCH_READOUT_DEG_R  = 2.5;
// Pitch number font. C++ uses FSSB18 (:576). LESSONS rule: ~1.25× scale,
// but the 21-px-tall readout box constrains us — this number must fit
// inside. CSS font-size 16 lands ~12 px cap height which fits the 21
// px box with comfortable margin.
export const MODE1_PITCH_READOUT_FONT_SIZE = 16;

// Mode 1 corner readouts. main.cpp:527-595.
export const MODE1_CORNER_LEFT_X       = 5;
export const MODE1_CORNER_RIGHT_X      = 307;   // :532
export const MODE1_CORNER_TOP_LABEL_Y  = 62;    // :540
export const MODE1_CORNER_BOT_LABEL_Y  = 230;   // :541
// C++ baselines: top numbers at y=30 (:579, :584). User-measured the
// SVG render against M5: our text sits ~5 px lower because browser
// Helvetica's baseline metrics differ from FSSB18 at the same nominal
// font size. Raise the top-row baseline to 25 so cap-height lands at
// the same row as M5. Bottom row stays at 198 (no observed offset).
export const MODE1_CORNER_TOP_NUM_Y    = 25;
export const MODE1_CORNER_BOT_NUM_Y    = 198;   // :588, :593
// Mode 1 uses FSS12 for labels (:542) and FSSB18 for numbers (:576).
// FSS12 → 18 px CSS. FSSB18 nominally maps to 33 in browser Helvetica,
// but Mode 1's numbers measured 1.2 grid-units against the M5 wasm-
// live's 1.6-1.7. CSS font-size 37 brings them up to ~1.6 units —
// Mode 0's corners use the same range (CORNER_NUM_FONT_SIZE=33) and
// have headroom, so a bump there is safe too.
export const MODE1_CORNER_LABEL_FONT_SIZE = 18;
export const MODE1_CORNER_NUM_FONT_SIZE   = 37;

// Mode 1 slip ball. main.cpp:598 — drawSlip(80, 204, 160, 20).
// SHORTER than Mode 0's 34-px height. Reuse mountSlipBall with these.
export const MODE1_SLIP_X = 80;
export const MODE1_SLIP_Y = 204;
export const MODE1_SLIP_W = 160;
export const MODE1_SLIP_H = 20;

// Mode 1 VSI tape. main.cpp:600-621.
// Same edgeTape widget as Mode 0's gOnset, but with VSI scaling and
// orange bar color. height = |iVSI × 120 / 600|, clamped 0..120.
export const MODE1_VSI_BAR_X        = 313;       // :609
export const MODE1_VSI_BAR_W        = 7;
export const MODE1_VSI_HEIGHT_SCALE = 120 / 600; // 0.2 px/fpm
export const MODE1_VSI_HEIGHT_MAX   = 120;
export const MODE1_VSI_ZERO_Y       = 119;       // :607-608
// Tick ladder: every 20 px from y=19 to y=219 (:613).
export const MODE1_VSI_TICK_X1      = 313;
export const MODE1_VSI_TICK_X2      = 319;
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
