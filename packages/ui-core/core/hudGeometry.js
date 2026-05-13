// hudGeometry.js — layout constants for the full-frame HUD overlay.
//
// FlySto-style visual language: yellow pitch ladder + FPM, white bank
// arc + tapes, monochrome tape ticks (no colored speed bands — those
// require Vne/Vno/Vfe wiring we don't have yet). Reference frame is
// 1920x1080 (16:9); SVG scales via preserveAspectRatio="xMidYMid meet"
// so the elements stay anchored to the cockpit view across aspect
// ratios.
//
// Scope intentionally narrower than a full EFIS:
//   - Pitch ladder + bank arc (artificial horizon)
//   - Airspeed tape (left)
//   - Altimeter tape with numeric VSI (right)
//   - Slip ball + G readout (bottom)
// No heading tape, no TAS/GS/wind block, no OAT/ISA/AGL stack.

// ---------------------------------------------------------------------
// Reference frame
// ---------------------------------------------------------------------

export const HUD_W = 1920;
export const HUD_H = 1080;
export const HUD_CX = HUD_W / 2;  // 960
export const HUD_CY = HUD_H / 2;  // 540

// ---------------------------------------------------------------------
// Pitch ladder (FlySto yellow)
// ---------------------------------------------------------------------
// Short yellow tick bars at ±10°, ±20°, ±30° straddling the horizon
// center, plus the horizon line itself. No continuous extension to
// frame edges, no sky/ground fill, no dashed marks. The whole ladder
// rotates with -roll and translates with pitch — classic ADI.

export const HUD_PITCH_PX_PER_DEG = 18;
export const HUD_HORIZON_HALF_W   = 540;  // shorter than full frame
export const HUD_PITCH_TICK_HALF_W = 110;
export const HUD_HORIZON_STROKE   = 4;
export const HUD_PITCH_TICK_STROKE = 4;
export const HUD_PITCH_LABEL_OFFSET = 28;
export const HUD_PITCH_LABEL_FONT_SIZE = 28;

// ---------------------------------------------------------------------
// Bank indicator arc (FlySto white)
// ---------------------------------------------------------------------
// White arc with sparse minor ticks every 10°, more pronounced ticks
// at ±30° and ±45°. Stationary yellow triangle pointer at top; the
// arc itself rotates by -roll so the ticks slide under the pointer.
// No 60° tick (FlySto stops at 45°).

export const HUD_BANK_CX        = HUD_CX;
export const HUD_BANK_CY        = HUD_CY;
export const HUD_BANK_R         = 460;
export const HUD_BANK_TICK_LONG  = 28;   // ±30, ±45
export const HUD_BANK_TICK_SHORT = 14;   // ±10, ±20
export const HUD_BANK_STROKE    = 4;
export const HUD_BANK_POINTER_H = 28;
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
// IAS tape (left edge) — FlySto inline labels, monochrome
// ---------------------------------------------------------------------
// Translucent strip with ticks + inline numeric labels every 20 kt.
// The current value sits in a center-line readout box. No boxed digit
// over the tape ticks (FlySto integrates the label into the tape
// itself). No colored Vne/Vno/Vfe bands — we don't have those wired.

export const HUD_IAS_X            = 60;
export const HUD_IAS_W            = 160;
export const HUD_IAS_TAPE_H       = 720;
export const HUD_IAS_CY           = HUD_CY;
export const HUD_IAS_PX_PER_UNIT  = 8;
export const HUD_IAS_TICK_EVERY   = 10;
export const HUD_IAS_LABEL_EVERY  = 20;
export const HUD_IAS_TICK_LEN_MAJOR = 22;
export const HUD_IAS_TICK_LEN_MINOR = 12;
export const HUD_IAS_LABEL_FONT_SIZE = 30;
export const HUD_IAS_BOX_H        = 60;
export const HUD_IAS_BOX_FONT_SIZE = 44;
export const HUD_IAS_BOX_PAD_X    = 12;

// ---------------------------------------------------------------------
// ALT tape + numeric VSI (right edge)
// ---------------------------------------------------------------------
// Mirror of IAS tape. VSI is a numeric readout floating next to the
// altimeter centerline (FlySto style: "-600"), not a chevron.

export const HUD_ALT_X            = HUD_W - 60 - 160;
export const HUD_ALT_W            = 160;
export const HUD_ALT_TAPE_H       = 720;
export const HUD_ALT_CY           = HUD_CY;
export const HUD_ALT_PX_PER_UNIT  = 0.16;
export const HUD_ALT_TICK_EVERY   = 100;
export const HUD_ALT_LABEL_EVERY  = 200;
export const HUD_ALT_TICK_LEN_MAJOR = 22;
export const HUD_ALT_TICK_LEN_MINOR = 12;
export const HUD_ALT_LABEL_FONT_SIZE = 30;
export const HUD_ALT_BOX_H        = 60;
export const HUD_ALT_BOX_FONT_SIZE = 40;
export const HUD_ALT_BOX_PAD_X    = 12;

// VSI numeric readout — sits just outside the ALT tape, vertically
// centered. Color shifts subtly (gray-white for zero, brighter when
// |VSI| is large enough to matter). We render only when |VSI| crosses
// a small threshold so the box doesn't churn at idle.
export const HUD_VSI_X            = HUD_W - 30;      // anchored at right margin
export const HUD_VSI_Y            = HUD_CY;          // tape centerline
export const HUD_VSI_FONT_SIZE    = 36;
export const HUD_VSI_THRESHOLD    = 100;             // fpm — hide below this

// ---------------------------------------------------------------------
// Flight-path marker (FlySto yellow, vertical-only)
// ---------------------------------------------------------------------
// Yellow circle + horizontal "wings" + top fin. Vertical position
// tracks (FlightPath - Pitch) × pixels-per-degree, MATCHING the
// existing AI inset's FlightPathMarker math exactly. Horizontal
// position is FIXED at HUD center.
//
// LATERAL FPM MOTION — DEFERRED. A "real" HUD FPM slides horizontally
// with the yaw-rate / ground-track delta from heading. OnSpeed does
// not currently expose yaw rate, and a previous LateralG-based
// approximation produced jumpy motion that didn't match the AI
// inset's behavior. Tracked in #542; revisit after a wire-format
// bump adds yaw rate or ground track.

export const HUD_FPM_CX           = HUD_CX;
export const HUD_FPM_CY           = HUD_CY;
export const HUD_FPM_R            = 22;
export const HUD_FPM_WING_INNER   = 22;
export const HUD_FPM_WING_OUTER   = 56;
export const HUD_FPM_TOP_TICK     = 22;
export const HUD_FPM_STROKE       = 4;

// ---------------------------------------------------------------------
// Slip ball (bottom center)
// ---------------------------------------------------------------------
// Reuses the existing SlipBall component. The HUD just supplies a
// frame-scaled position + size.

export const HUD_SLIP_W           = 480;
export const HUD_SLIP_H           = 60;
export const HUD_SLIP_X           = HUD_CX - HUD_SLIP_W / 2;
export const HUD_SLIP_Y           = HUD_H - 120;

// ---------------------------------------------------------------------
// G readout (bottom-right of slip ball)
// ---------------------------------------------------------------------
// Single numeric value "X.X G" rendered next to the slip ball.
// Reads state.VerticalG (the load-factor channel). One-decimal
// precision matches what you'd glance at on a panel G-meter.

export const HUD_G_X              = HUD_SLIP_X + HUD_SLIP_W + 36;
export const HUD_G_Y              = HUD_SLIP_Y + HUD_SLIP_H / 2;
export const HUD_G_FONT_SIZE      = 40;

// ---------------------------------------------------------------------
// Stroke / color defaults
// ---------------------------------------------------------------------
// FlySto palette: yellow for the airplane-fixed elements (FPM, pitch
// ladder, bank pointer), white for the world-fixed scales (bank arc,
// tapes). A black drop-shadow halo is applied via CSS in replay.css
// so white-on-bright-sky stays legible.

export const HUD_LINE_COLOR        = 'var(--white)';
export const HUD_HORIZON_COLOR     = 'var(--white)';
export const HUD_PITCH_COLOR       = 'var(--yellow)';
export const HUD_FPM_COLOR         = 'var(--yellow)';
export const HUD_BANK_ARC_COLOR    = 'var(--white)';
export const HUD_BANK_POINTER_COLOR = 'var(--yellow)';
export const HUD_VSI_COLOR         = 'var(--white)';
export const HUD_BOX_FILL          = 'rgba(0, 0, 0, 0.72)';
