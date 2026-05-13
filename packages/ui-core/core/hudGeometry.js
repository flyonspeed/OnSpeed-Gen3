// hudGeometry.js — layout constants for the full-frame HUD overlay.
//
// FlySto-style attitude indicator (pitch ladder + bank arc + FPM)
// rendered directly at 1920x1080. Around the central ADI:
//   - OnSpeed logo top-left
//   - Right-side VVI trend bar
//   - Right-side ALT tape with Garmin-style readout box, centered on
//     the VVI centerline so the two right-side gauges sit paired
//   - SlipBall at bottom-center
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
// OnSpeed logo (top-left)
// ---------------------------------------------------------------------
// White-stroked rendering of the OnSpeed mark. Source viewBox is
// 612x792 (portrait, aspect 0.773). Sized to match the visual weight
// of the other top-edge elements without crowding the pitch ladder.

// Logo is rendered into a square frame; the source paths occupy a
// roughly square sub-region of the original portrait viewBox, and we
// crop to that region in OnSpeedLogo.js.
export const HUD_LOGO_X    = 40;
export const HUD_LOGO_Y    = 40;
export const HUD_LOGO_W    = 180;
export const HUD_LOGO_H    = 180;

// ---------------------------------------------------------------------
// Pitch ladder (yellow ticks at +/-10/+/-20/+/-30, white horizon line)
// ---------------------------------------------------------------------
// Short yellow tick bars at +/-10, +/-20, +/-30 degrees straddling the
// horizon center, plus the horizon line itself. No continuous extension
// to frame edges, no sky/ground fill, no dashed marks. The whole ladder
// rotates with -roll and translates with pitch — classic ADI.

export const HUD_PITCH_PX_PER_DEG     = 18;
export const HUD_HORIZON_HALF_W       = 400;  // shorter than full frame; horizon stops well before the right-side gauges
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
// VVI trend bar (right side, inboard of the ALT tape)
// ---------------------------------------------------------------------
// Centerline at HUD_VVI_CY shared with the ALT tape; bar extends up
// for climb and down for descent. Ticks at +/-1000 and +/-2000 fpm.
// Numeric label when |VVI| exceeds HUD_VVI_THRESHOLD; bar hidden below
// HUD_VVI_BAR_THRESHOLD so the gauge sits still at idle. The VVI sits
// inboard of the ALT tape; the value label renders to the RIGHT of the
// bar (the ALT readout box sits ON the tape column further outboard,
// so there's clear space immediately right of the VVI for the numeric).

export const HUD_VVI_X               = 1620;
// VVI centerline matches the ALT tape so the two right-side gauges
// share a horizontal axis. With HUD_VVI_HALF_H = 220 a centerline at
// y = 480 puts the bar bottom at y = 700, ~30 px clear of the
// bottom-right inset slot (top at y ≈ 731 per overlayPlacement).
export const HUD_VVI_CY              = 480;
export const HUD_VVI_HALF_H          = 220;
export const HUD_VVI_FULL_SCALE_FPM  = 2000;
export const HUD_VVI_BAR_THRESHOLD   = 50;
export const HUD_VVI_THRESHOLD       = 100;
export const HUD_VVI_TICK_FONT_SIZE  = 20;
export const HUD_VVI_VALUE_FONT_SIZE = 36;

// ---------------------------------------------------------------------
// ALT tape (right side, outboard of the VVI)
// ---------------------------------------------------------------------
// FlySto-style altimeter: a vertical stack of horizontal tick lines
// (every 20 ft) with numeric labels at every 100 ft. The tape scrolls
// vertically so the current altitude sits on the centerline. A Garmin-
// style readout box anchored on the left edge of the tape shows the
// current altitude — stationary thousands+hundreds digits beside a
// sliding tens strip clipped to the box. Below the tape, a static
// "29.92in" baro setting (the log doesn't carry baro).
//
// Geometry chosen to match the VVI centerline + half-height so the two
// right-side gauges pair visually. Tape sits outboard of the VVI.

export const HUD_ALT_CY              = HUD_VVI_CY;   // 480
export const HUD_ALT_HALF_H          = HUD_VVI_HALF_H; // 220
// X is the LEFT edge of the tick lines. Short ticks extend to
// HUD_ALT_X + HUD_ALT_TICK_SHORT; long ticks to HUD_ALT_X + HUD_ALT_TICK_LONG.
export const HUD_ALT_X               = 1820;
export const HUD_ALT_TICK_SHORT      = 14;   // every 20 ft
export const HUD_ALT_TICK_LONG       = 28;   // every 100 ft
export const HUD_ALT_TICK_STROKE     = 2;
// 75 px per 100 ft → 15 px per 20 ft. 30 ticks (-300..+300 ft from
// current altitude) span 450 px, slightly more than HUD_ALT_HALF_H*2;
// extra ticks scroll off the top/bottom edges.
export const HUD_ALT_PX_PER_100_FT   = 75;
export const HUD_ALT_PX_PER_20_FT    = HUD_ALT_PX_PER_100_FT / 5;   // 15
export const HUD_ALT_LABEL_FONT_SIZE = 22;
export const HUD_ALT_LABEL_OFFSET_X  = 8;    // gap from end of long tick to label
// Tens-strip slide per 20 ft of altitude change. Matches the slide
// rate FlySto uses (proportional to the tape's 100-ft pitch).
export const HUD_ALT_TENS_SLIDE_PX   = HUD_ALT_PX_PER_20_FT * 2;     // 30, mirrors FlySto's 30.7
// Garmin-style readout box, CENTERED ON the tape's tick column. The
// box body's left wall sits ~6 px right of the tape's left tick stem;
// an arrow tab notches LEFT from the box left wall, with the tip
// landing ~2 px LEFT of the tape's left tick stem (notching INTO the
// tick column toward the tape's centerline). The box extends well
// past the tape's right edge so the digits clear the tick labels.
//
// FlySto reference: tape-rect x=1288 w=86 (right 1374), ALT-box-rect
// x=1286 w=102 (right 1388). Box left ≈ tape left; box extends ~14 px
// past tape right; arrow tip at x=886 = 2 px past tape left at x=888.
export const HUD_ALT_BOX_W           = 130;
export const HUD_ALT_BOX_H           = 56;
export const HUD_ALT_BOX_ARROW_W     = 8;    // arrow-tab depth (notch into tick column)
// Box rendering anchors:
//   - Box body left wall at HUD_ALT_X + 6 (6 px right of left tick stem).
//   - Arrow tip at HUD_ALT_X - 2 (2 px left of left tick stem),
//     at HUD_ALT_CY.
//   - Box extends HUD_ALT_BOX_W to the right of the left wall.
export const HUD_ALT_BOX_LEFT        = HUD_ALT_X + 6;
export const HUD_ALT_BOX_RIGHT       = HUD_ALT_BOX_LEFT + HUD_ALT_BOX_W;
export const HUD_ALT_BOX_ARROW_TIP_X = HUD_ALT_X - 2;
export const HUD_ALT_BOX_TOP         = HUD_ALT_CY - HUD_ALT_BOX_H / 2;
export const HUD_ALT_BOX_BOTTOM      = HUD_ALT_CY + HUD_ALT_BOX_H / 2;
export const HUD_ALT_BOX_FONT_SIZE   = 30;
export const HUD_ALT_BOX_FILL        = 'rgba(46, 46, 46, 0.85)';
// "29.92in" baro static text — below the bottom of the tape.
export const HUD_ALT_BARO_Y          = HUD_ALT_CY + HUD_ALT_HALF_H + 38;
export const HUD_ALT_BARO_FONT_SIZE  = 22;

// ---------------------------------------------------------------------
// IAS tape (left side, mirror of ALT)
// ---------------------------------------------------------------------
// FlySto-style airspeed tape on the LEFT side of the HUD, mirroring the
// ALT tape's structure across the vertical centerline. Vertical stack
// of horizontal tick lines (every 5 kt) with numeric labels every 10
// kt. The tape scrolls vertically so the current IAS sits on the
// centerline. A Garmin-style readout box anchored on the tape's RIGHT
// edge shows the current IAS — stationary hundreds+tens digits beside
// a sliding ones digit clipped to the box.
//
// Monochrome: white ticks + white labels. Color bands (Vne/Vno/Vfe)
// are deferred until the config plumbing exists to pull per-aircraft
// V-speeds; the live page has no source for them today.
//
// Geometry mirrors the ALT side around HUD_CX so the two tapes appear
// paired. The OnSpeed logo (y=40..220) sits above the tape (y=260..700),
// and the bottom-left inset slot (top ≈ y=731) sits below the tape.

export const HUD_IAS_CY              = HUD_ALT_CY;     // 480 — shared centerline
export const HUD_IAS_HALF_H          = HUD_ALT_HALF_H; // 220
// X is the RIGHT (inboard) edge of the tick column — ticks extend
// LEFTWARD from this anchor toward the outboard side, labels further
// left. Mirror of HUD_ALT_X = 1820 about HUD_CX = 960: 100.
export const HUD_IAS_X               = 100;
export const HUD_IAS_TICK_SHORT      = 14;   // every 5 kt
export const HUD_IAS_TICK_LONG       = 28;   // every 10 kt
export const HUD_IAS_TICK_STROKE     = 2;
// 75 px per 10 kt → 37.5 px per 5 kt. Matches ALT's 75-px-per-100-ft
// density so the two tapes' tick spacing reads as one visual unit.
export const HUD_IAS_PX_PER_10_KT    = 75;
export const HUD_IAS_PX_PER_5_KT     = HUD_IAS_PX_PER_10_KT / 2;   // 37.5
export const HUD_IAS_PX_PER_KT       = HUD_IAS_PX_PER_10_KT / 10;  // 7.5
export const HUD_IAS_LABEL_FONT_SIZE = 22;
export const HUD_IAS_LABEL_OFFSET_X  = 8;    // gap from end of long tick to label

// Garmin-style readout box CENTERED ON the tape's tick column, body
// extending LEFT (outboard) past the labels. Arrow tab on the box's
// RIGHT side notches RIGHT into the tick column, tip landing ~2 px
// PAST the rightmost tick stem (inboard toward the ADI center).
// Mirror of ALT box geometry across HUD_CX.
export const HUD_IAS_BOX_W           = 130;
export const HUD_IAS_BOX_H           = 56;
export const HUD_IAS_BOX_ARROW_W     = 8;
// Box rendering anchors (mirror of ALT):
//   - Box body right wall at HUD_IAS_X - 6 (6 px left of right tick
//     stem) — body sits on the OUTBOARD side, away from frame center.
//   - Arrow tip at HUD_IAS_X + 2 (2 px right of right tick stem) at
//     HUD_IAS_CY.
//   - Box extends HUD_IAS_BOX_W to the LEFT of the right wall.
export const HUD_IAS_BOX_RIGHT       = HUD_IAS_X - 6;
export const HUD_IAS_BOX_LEFT        = HUD_IAS_BOX_RIGHT - HUD_IAS_BOX_W;
export const HUD_IAS_BOX_ARROW_TIP_X = HUD_IAS_X + 2;
export const HUD_IAS_BOX_TOP         = HUD_IAS_CY - HUD_IAS_BOX_H / 2;
export const HUD_IAS_BOX_BOTTOM      = HUD_IAS_CY + HUD_IAS_BOX_H / 2;
export const HUD_IAS_BOX_FONT_SIZE   = 30;
export const HUD_IAS_BOX_FILL        = 'rgba(46, 46, 46, 0.85)';
// Ones-digit slide-per-knot. Set equal to the box font size so each
// 1-kt step is a visible ~one-font-height jog, with the digit fully
// replacing the next over a 1-kt change. Larger than the tape pitch
// (7.5 px/kt) so the spinning ones digit reads as motion, not as
// imperceptible drift.
export const HUD_IAS_ONES_SLIDE_PX   = HUD_IAS_BOX_FONT_SIZE;       // 30

// ---------------------------------------------------------------------
// Slip ball (bottom center)
// ---------------------------------------------------------------------
// Reuses the existing SlipBall component. The HUD just supplies a
// frame-scaled position + size.

export const HUD_SLIP_W = 480;
export const HUD_SLIP_H = 60;
export const HUD_SLIP_X = HUD_CX - HUD_SLIP_W / 2;
export const HUD_SLIP_Y = HUD_H - 120;
