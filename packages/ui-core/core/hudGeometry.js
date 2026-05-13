// hudGeometry.js — layout constants for the full-frame HUD overlay.
//
// The HUD's reference frame is 1920x1080 (16:9). The SVG scales via
// preserveAspectRatio="xMidYMid meet" so it covers any source-video
// aspect; letterboxed footage gets letterboxed HUD too, which keeps
// the elements visually anchored to the cockpit view rather than the
// black bars.
//
// All numeric tunables live here so iteration is one-source-of-truth
// (per PLAN_HUD_OVERLAY.md "Element geometry sketch"). Edit a number
// here, see the change in the live preview.

// ---------------------------------------------------------------------
// Reference frame
// ---------------------------------------------------------------------

export const HUD_W = 1920;
export const HUD_H = 1080;
export const HUD_CX = HUD_W / 2;  // 960
export const HUD_CY = HUD_H / 2;  // 540

// ---------------------------------------------------------------------
// Pitch ladder
// ---------------------------------------------------------------------
// Horizontal pitch lines drawn around HUD center, rotated by roll and
// translated vertically by pitch. The ladder rotates with the airframe
// (per the plan's "Open questions" item — looks like a real ADI).
//
// Pixels-per-degree of pitch was chosen so that ±30 degrees fills
// roughly the center vertical third of the frame; finer than this and
// the ladder looks cluttered, coarser and the rate of motion looks
// sluggish vs. the actual horizon in the footage.

export const HUD_PITCH_PX_PER_DEG = 18;
// Horizon line spans the full HUD width through the center, so when
// rotated/translated it still covers everywhere we'd want to see it.
export const HUD_HORIZON_HALF_W   = HUD_W;
// Pitch ladder half-widths by tick value (degrees).
// ±10° / ±20° are solid short ticks; ±30° is dashed.
export const HUD_PITCH_TICK_HALF_W_SHORT  = 90;
export const HUD_PITCH_TICK_HALF_W_LONG   = 150;
export const HUD_PITCH_TICK_HALF_W_DASHED = 180;
// Pitch ladder line stroke widths.
export const HUD_HORIZON_STROKE   = 4;
export const HUD_PITCH_TICK_STROKE = 3;
// Pitch label offset past the right end of the tick.
export const HUD_PITCH_LABEL_OFFSET = 28;
export const HUD_PITCH_LABEL_FONT_SIZE = 28;

// ---------------------------------------------------------------------
// Bank indicator arc
// ---------------------------------------------------------------------
// Top of frame. A static reference arc with tick marks at fixed bank
// angles, and a stationary triangular pointer at the 12 o'clock
// position. The arc rotates with -roll (so the ticks slide under the
// pointer as the aircraft banks).

export const HUD_BANK_CX        = HUD_CX;
export const HUD_BANK_CY        = HUD_CY;     // arc is part of the same hub as the pitch ladder
export const HUD_BANK_R         = 460;        // radius from center
export const HUD_BANK_TICK_LONG  = 28;        // tick mark inward length (±30/±60)
export const HUD_BANK_TICK_SHORT = 18;        // tick mark inward length (±10/±20/±45)
export const HUD_BANK_STROKE    = 4;
// Pointer triangle (stationary at top).
export const HUD_BANK_POINTER_H = 28;
export const HUD_BANK_POINTER_HALF_W = 16;
// Tick angles in degrees, measured from "up" (negative bank = left wing
// low in body frame). The arc itself rotates by -roll.
export const HUD_BANK_TICKS = Object.freeze([
  { deg: -60, long: true  },
  { deg: -45, long: false },
  { deg: -30, long: true  },
  { deg: -20, long: false },
  { deg: -10, long: false },
  { deg:   0, long: true  },
  { deg:  10, long: false },
  { deg:  20, long: false },
  { deg:  30, long: true  },
  { deg:  45, long: false },
  { deg:  60, long: true  },
]);

// ---------------------------------------------------------------------
// IAS tape (left edge)
// ---------------------------------------------------------------------
// Scrolling numeric strip. The tape's vertical center is fixed at the
// HUD center; the strip of numbers scrolls behind it so the current
// IAS value lines up with the highlighted center box.

export const HUD_IAS_X            = 60;    // left edge of tape
export const HUD_IAS_W            = 160;
export const HUD_IAS_TAPE_H       = 720;   // total visible height of the strip
export const HUD_IAS_CY           = HUD_CY;
export const HUD_IAS_PX_PER_UNIT  = 8;     // px per knot
export const HUD_IAS_TICK_EVERY   = 10;    // major tick every 10 kt
export const HUD_IAS_LABEL_EVERY  = 20;    // label every 20 kt
export const HUD_IAS_TICK_LEN_MAJOR = 22;
export const HUD_IAS_TICK_LEN_MINOR = 12;
export const HUD_IAS_LABEL_FONT_SIZE = 30;
// Highlight box dimensions (covers the centerline tick row).
export const HUD_IAS_BOX_H        = 60;
export const HUD_IAS_BOX_FONT_SIZE = 44;
export const HUD_IAS_BOX_PAD_X    = 12;

// ---------------------------------------------------------------------
// ALT tape + VSI chevron (right edge)
// ---------------------------------------------------------------------
// Same shape as IAS tape but mirrored. The VSI chevron sits beside the
// tape and grows up/down with vertical speed.

export const HUD_ALT_X            = HUD_W - 60 - 160;  // right edge minus margin + width
export const HUD_ALT_W            = 160;
export const HUD_ALT_TAPE_H       = 720;
export const HUD_ALT_CY           = HUD_CY;
export const HUD_ALT_PX_PER_UNIT  = 0.16;     // 100 ft = 16 px (denser than IAS)
export const HUD_ALT_TICK_EVERY   = 100;      // major tick every 100 ft
export const HUD_ALT_LABEL_EVERY  = 200;      // label every 200 ft
export const HUD_ALT_TICK_LEN_MAJOR = 22;
export const HUD_ALT_TICK_LEN_MINOR = 12;
export const HUD_ALT_LABEL_FONT_SIZE = 30;
// Highlight box covers the centerline.
export const HUD_ALT_BOX_H        = 60;
export const HUD_ALT_BOX_FONT_SIZE = 40;
export const HUD_ALT_BOX_PAD_X    = 12;
// VSI chevron — to the right of the ALT tape (outside the frame edge
// would be ideal but we keep it inside so it can't be clipped).
export const HUD_VSI_CHEVRON_X    = HUD_W - 40;
export const HUD_VSI_FULL_SCALE_FPM = 2000;   // chevron saturates at this VSI
export const HUD_VSI_MAX_LEN_PX   = HUD_ALT_TAPE_H / 2; // chevron can extend at most to the tape edge
export const HUD_VSI_STROKE       = 5;

// ---------------------------------------------------------------------
// Flight-path marker
// ---------------------------------------------------------------------
// Magenta circle at the FPM location: vertical offset from horizon =
// (FlightPath - Pitch) × pixels-per-degree. Horizontal offset is from
// the sideslip approximation (LateralG) — see PLAN_HUD_OVERLAY.md
// "FPM lateral motion approximation". In coordinated flight LateralG
// is ~0 so the FPM stays centered (the right behavior visually for a
// HUD, even though a real FPM would slide with yaw rate). In a
// skid/slip LateralG is non-zero and the FPM slides — the case
// OnSpeed pilots care about.

export const HUD_FPM_CX           = HUD_CX;
export const HUD_FPM_CY           = HUD_CY;
export const HUD_FPM_R            = 22;
export const HUD_FPM_WING_INNER   = 22;
export const HUD_FPM_WING_OUTER   = 56;
export const HUD_FPM_TOP_TICK     = 22;
export const HUD_FPM_STROKE       = 4;
// Pixels per g of lateral-G displacement. Calibrated against
// LateralG=0.1 producing ~80 px of lateral slide (a noticeable but
// not screen-edge shift at the levels OnSpeed cares about — uncoordinated
// flight commonly sits in the 0.05–0.15 g band).
export const HUD_FPM_LAT_PX_PER_G = 800;
// Clamp the lateral slide so a saturated value stays inside the frame.
export const HUD_FPM_LAT_MAX_PX   = 360;

// ---------------------------------------------------------------------
// Slip ball (bottom center)
// ---------------------------------------------------------------------
// Reuses the existing SlipBall component. The HUD just supplies a
// frame-scaled position + size.

export const HUD_SLIP_W           = 480;
export const HUD_SLIP_H           = 60;
export const HUD_SLIP_X           = HUD_CX - HUD_SLIP_W / 2;
export const HUD_SLIP_Y           = HUD_H - 120;  // anchored above the bottom margin

// ---------------------------------------------------------------------
// Stroke / glyph defaults
// ---------------------------------------------------------------------
// The HUD draws lines-only over the GoPro footage; there is no sky/
// ground fill. White lines on bright sky are fragile (per plan "Open
// questions"); we paint a black drop-shadow halo on the SVG via CSS
// rather than per-element here.

export const HUD_LINE_COLOR       = 'var(--white)';
export const HUD_HORIZON_COLOR    = 'var(--white)';
export const HUD_FPM_COLOR        = 'var(--magenta)';
export const HUD_BANK_POINTER_COLOR = 'var(--yellow)';
export const HUD_BOX_FILL         = 'rgba(0, 0, 0, 0.72)';
