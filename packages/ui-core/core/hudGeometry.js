// hudGeometry.js — layout constants for the full-frame HUD overlay.
//
// FlySto-style attitude indicator (pitch ladder + bank arc + FPM)
// rendered directly at 1920x1080, plus three top boxed readouts
// (IAS / MH / PALT), a right-side VVI trend bar, and the slip ball
// at bottom center. No airspeed/altimeter tapes — the top readouts
// carry those values.
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
// Pitch ladder (yellow ticks at +/-10/+/-20/+/-30, white horizon line)
// ---------------------------------------------------------------------
// Short yellow tick bars at +/-10, +/-20, +/-30 degrees straddling the
// horizon center, plus the horizon line itself. No continuous extension
// to frame edges, no sky/ground fill, no dashed marks. The whole ladder
// rotates with -roll and translates with pitch — classic ADI.

export const HUD_PITCH_PX_PER_DEG     = 18;
export const HUD_HORIZON_HALF_W       = 540;  // shorter than full frame
export const HUD_PITCH_TICK_HALF_W    = 110;
export const HUD_HORIZON_STROKE       = 4;
export const HUD_PITCH_TICK_STROKE    = 4;
export const HUD_PITCH_LABEL_OFFSET   = 28;
export const HUD_PITCH_LABEL_FONT_SIZE = 28;

// ---------------------------------------------------------------------
// Bank indicator arc (white, ticks at +/-10/+/-20/+/-30/+/-45)
// ---------------------------------------------------------------------
// White arc with sparse minor ticks every 10 degrees, more pronounced
// ticks at +/-30 and +/-45. Stationary yellow triangle pointer at top;
// the arc itself rotates by -roll so the ticks slide under the pointer.
// No 60-degree tick (FlySto stops at 45).

export const HUD_BANK_CX             = HUD_CX;
export const HUD_BANK_CY             = HUD_CY;
export const HUD_BANK_R              = 460;
export const HUD_BANK_TICK_LONG      = 28;   // +/-30, +/-45
export const HUD_BANK_TICK_SHORT     = 14;   // +/-10, +/-20
export const HUD_BANK_STROKE         = 4;
export const HUD_BANK_POINTER_H      = 28;
export const HUD_BANK_POINTER_HALF_W = 16;
export const HUD_BANK_TICKS = Object.freeze([
  { deg: -45, long: true  },
  { deg: -30, long: true  },
  { deg: -20, long: false },
  { deg: -10, long: false },
  { deg:   0, long: true  },
  { deg:  10, long: false },
  { deg:  20, long: false },
  { deg:  30, long: true  },
  { deg:  45, long: true  },
]);

// ---------------------------------------------------------------------
// Flight-path marker (yellow, vertical-only; lateral motion deferred #542)
// ---------------------------------------------------------------------
// Yellow circle + horizontal wings + top fin. Vertical position tracks
// (FlightPath - Pitch) * pixels-per-degree, matching the AI inset's
// FlightPathMarker math exactly. Horizontal position is FIXED at HUD
// center — lateral FPM motion needs yaw rate / ground track we don't
// have yet (#542).

export const HUD_FPM_CX           = HUD_CX;
export const HUD_FPM_CY           = HUD_CY;
export const HUD_FPM_R            = 22;
export const HUD_FPM_WING_INNER   = 22;
export const HUD_FPM_WING_OUTER   = 56;
export const HUD_FPM_TOP_TICK     = 22;
export const HUD_FPM_STROKE       = 4;

// ---------------------------------------------------------------------
// Pitch / bank / FPM colors
// ---------------------------------------------------------------------
// Yellow for airplane-fixed elements (pitch ticks, FPM, bank pointer),
// white for world-fixed scales (horizon line, bank arc).

export const HUD_HORIZON_COLOR      = 'var(--white)';
export const HUD_PITCH_COLOR        = 'var(--yellow)';
export const HUD_FPM_COLOR          = 'var(--yellow)';
export const HUD_BANK_ARC_COLOR     = 'var(--white)';
export const HUD_BANK_POINTER_COLOR = 'var(--yellow)';

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
