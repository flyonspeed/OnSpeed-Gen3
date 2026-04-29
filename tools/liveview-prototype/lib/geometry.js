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
