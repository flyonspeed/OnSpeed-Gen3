// hudGeometry.js — layout constants for the full-frame HUD overlay.
//
// The HUD reuses the M5 attitude indicator components (HorizonLine,
// PitchLadder, BankArc, AircraftSymbol, FlightPathMarker, SlipBall)
// scaled up to a 1920x1080 reference frame. The center of the inset's
// horizon (MODE1_HORIZON_CX, MODE1_HORIZON_CY in core/geometry.js) is
// mapped to the HUD center (960, 540) at 3x scale via a single
// `<g transform>` so the geometry is one source of truth across the
// inset and HUD.
//
// SVG scales via preserveAspectRatio="xMidYMid meet" so the elements
// stay anchored to the cockpit view across aspect ratios.

// ---------------------------------------------------------------------
// Reference frame
// ---------------------------------------------------------------------

export const HUD_W = 1920;
export const HUD_H = 1080;
export const HUD_CX = HUD_W / 2;  // 960
export const HUD_CY = HUD_H / 2;  // 540

// ---------------------------------------------------------------------
// Top readouts (IAS / MH / PALT)
// ---------------------------------------------------------------------
// Three boxed text boxes along the top. Black-fill rounded rect with
// white border; label small + value large. MH is hidden when the log
// doesn't carry efisMagHeading.

export const HUD_TOP_Y               = 40;
export const HUD_TOP_BOX_W           = 260;
export const HUD_TOP_BOX_H           = 80;
export const HUD_TOP_LABEL_FONT_SIZE = 24;
export const HUD_TOP_VALUE_FONT_SIZE = 44;
export const HUD_TOP_LABEL_PAD_X     = 18;
export const HUD_TOP_VALUE_PAD_X     = 18;
export const HUD_TOP_LEFT_X          = 60;                          // IAS
export const HUD_TOP_CENTER_X        = HUD_CX - HUD_TOP_BOX_W / 2;  // MH
export const HUD_TOP_RIGHT_X         = HUD_W - 60 - HUD_TOP_BOX_W;  // PALT

// ---------------------------------------------------------------------
// VVI trend bar (right side)
// ---------------------------------------------------------------------
// Centerline at HUD vertical center; bar extends up for climb and down
// for descent. Ticks at +/-1000 and +/-2000 fpm. Numeric label when
// |VVI| exceeds HUD_VVI_THRESHOLD fpm; bar hidden below
// HUD_VVI_BAR_THRESHOLD fpm so the gauge sits still at idle.

export const HUD_VVI_X               = HUD_W - 200;
export const HUD_VVI_CY              = HUD_CY;
export const HUD_VVI_HALF_H          = 380;
export const HUD_VVI_FULL_SCALE_FPM  = 2000;
export const HUD_VVI_BAR_THRESHOLD   = 50;
export const HUD_VVI_THRESHOLD       = 100;
export const HUD_VVI_TICK_FONT_SIZE  = 20;
export const HUD_VVI_VALUE_FONT_SIZE = 36;

// ---------------------------------------------------------------------
// Slip ball (bottom center)
// ---------------------------------------------------------------------
// Reuses the existing SlipBall component. The HUD just supplies a
// frame-scaled position + size.

export const HUD_SLIP_W = 480;
export const HUD_SLIP_H = 60;
export const HUD_SLIP_X = HUD_CX - HUD_SLIP_W / 2;
export const HUD_SLIP_Y = HUD_H - 120;

// ---------------------------------------------------------------------
// Top-readout box fill
// ---------------------------------------------------------------------
// Black box fill and white outline for the top readouts.

export const HUD_BOX_FILL = 'rgba(0, 0, 0, 0.72)';
