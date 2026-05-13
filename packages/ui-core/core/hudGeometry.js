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

// Logo PNG dimensions are 1972×2132 (aspect ~0.925). Render width is
// fixed; height scales by the source aspect so the mark + wordmark sit
// proportionally without horizontal stretch.
export const HUD_LOGO_X    = 40;
export const HUD_LOGO_Y    = 40;
export const HUD_LOGO_W    = 180;
export const HUD_LOGO_H    = HUD_LOGO_W * (2132 / 1972);

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
// VVI trend bar (right side, adjacent to the ALT tape's label column)
// ---------------------------------------------------------------------
// Centerline at HUD_VVI_CY shared with the ALT tape; bar extends up
// for climb and down for descent. Ticks at +/-1000 and +/-2000 fpm.
// Numeric label when |VVI| exceeds HUD_VVI_THRESHOLD; bar hidden below
// HUD_VVI_BAR_THRESHOLD so the gauge sits still at idle. Ticks point
// LEFT (toward the ALT tape's label column); the numeric readout sits
// on the RIGHT of the bar (open frame space).

// VVI sits IMMEDIATELY ADJACENT to the right edge of the ALT tape's
// label column. Ticks point LEFT (toward the ALT tape's labels);
// numeric labels and the value readout sit on the RIGHT of the bar.
// Slightly shorter than the ALT tape so the visual hierarchy is:
// ALT tape (tallest, primary) > VVI bar (secondary, attached).
//
// Position calc:
//   ALT box body right edge = HUD_ALT_BOX_RIGHT = 1736. VVI spine at
//   1758 leaves a 22-px gap between box right and bar — tight enough
//   to read as a paired right-side gauge, wide enough that the VVI's
//   scale numerals (at cx+6=1764) don't collide with the box body.
// The VVI spine sits flush against the ALT readout box's right wall;
// ticks point LEFT (toward the ALT tape's label column behind the
// box) and the scale numerals + value readout sit on the RIGHT of
// the spine in the open frame.
export const HUD_VVI_X               = 1758;
// VVI centerline matches the ALT tape so the two right-side gauges
// share a horizontal axis. Bar HALF_H is 65% of the ALT tape's
// half-height (220 * 0.65) so the VVI reads as a smaller sibling
// gauge. Literal kept here (rather than `HUD_ALT_HALF_H * 0.65`) to
// avoid a circular reference — HUD_ALT_CY references HUD_VVI_CY,
// and JS export bindings can't be forward-declared cleanly.
export const HUD_VVI_CY              = 480;
export const HUD_VVI_HALF_H          = 143;          // 0.65 * 220
export const HUD_VVI_FULL_SCALE_FPM  = 2000;
export const HUD_VVI_BAR_THRESHOLD   = 50;
export const HUD_VVI_THRESHOLD       = 100;
export const HUD_VVI_TICK_FONT_SIZE  = 20;
export const HUD_VVI_VALUE_FONT_SIZE = 30;

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
// ALT is the source-of-truth for the right-side tape height; VVI bar
// derives its HALF_H from this as a fraction (see HUD_VVI_HALF_H below).
export const HUD_ALT_HALF_H          = 220;
// X is the LEFT edge of the tick lines. Short ticks extend to
// HUD_ALT_X + HUD_ALT_TICK_SHORT; long ticks to HUD_ALT_X + HUD_ALT_TICK_LONG.
// Pulled 200 px inboard from the original 1820 so the readout box body
// (extending HUD_ALT_BOX_W = 130 to the right) fits inside the 1920-wide
// viewBox: box right = 1620 + 6 + 130 = 1756, comfortably on-frame.
export const HUD_ALT_X               = 1620;
export const HUD_ALT_TICK_SHORT      = 7;    // every 20 ft
export const HUD_ALT_TICK_LONG       = 14;   // every 100 ft
export const HUD_ALT_TICK_STROKE     = 2;
// 75 px per 100 ft → 15 px per 20 ft. 30 ticks (-300..+300 ft from
// current altitude) span 450 px, slightly more than HUD_ALT_HALF_H*2;
// extra ticks scroll off the top/bottom edges.
export const HUD_ALT_PX_PER_100_FT   = 75;
export const HUD_ALT_PX_PER_20_FT    = HUD_ALT_PX_PER_100_FT / 5;   // 15
export const HUD_ALT_LABEL_FONT_SIZE = 22;
export const HUD_ALT_LABEL_OFFSET_X  = 8;    // gap from end of long tick to label
// Tens-strip slide per 20 ft of altitude change. Sized to the box
// HEIGHT so the up/down digits sit fully outside the box body when
// the strip is stationary on a tens-multiple. A shorter slide would
// leave the up/down digits half-visible at the top/bottom of the
// box even at frac=0 (only the curr digit should be visible then).
export const HUD_ALT_TENS_SLIDE_PX   = 60;
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
// ALT shows up to 5 digits (e.g. "12340"). Stationary "hundreds"
// holds up to 3 digits ("123") and the sliding "tens" holds 2
// digits ("40"). Body width 110 keeps a tight ~8 px gap between
// the digit columns. Body height 60 leaves room for the arrow tab
// without colliding with the rounded corners.
export const HUD_ALT_BOX_W           = 110;
export const HUD_ALT_BOX_H           = 60;
export const HUD_ALT_BOX_ARROW_W     = 6;    // arrow-tab depth (notch into tick column)
// Box rendering anchors:
//   - Box body left wall at HUD_ALT_X + 6 (6 px right of left tick stem).
//   - Arrow tip 6 px LEFT of the box body's left wall (lands just past
//     the tick column's left edge) at HUD_ALT_CY.
//   - Box extends HUD_ALT_BOX_W to the right of the left wall.
export const HUD_ALT_BOX_LEFT        = HUD_ALT_X + 6;
export const HUD_ALT_BOX_RIGHT       = HUD_ALT_BOX_LEFT + HUD_ALT_BOX_W;
export const HUD_ALT_BOX_ARROW_TIP_X = HUD_ALT_BOX_LEFT - HUD_ALT_BOX_ARROW_W;
export const HUD_ALT_BOX_TOP         = HUD_ALT_CY - HUD_ALT_BOX_H / 2;
export const HUD_ALT_BOX_BOTTOM      = HUD_ALT_CY + HUD_ALT_BOX_H / 2;
export const HUD_ALT_BOX_FONT_SIZE   = 30;
export const HUD_ALT_BOX_FILL        = 'rgba(46, 46, 46, 0.85)';
// Semi-transparent dark backing strip drawn BEHIND the ticks so the
// labels stay legible over busy GoPro frames. Spans the full vertical
// extent of the tape PLUS a baro endcap region at the bottom that
// holds the "29.92in" baro setting (matches FlySto's tape design,
// where the baro reads inside a slightly-darker bottom section of
// the same rounded backing strip — not a free-floating pill).
// Width covers the tick column + numeric labels (worst case: 5-digit
// label like "12340" at HUD_ALT_LABEL_FONT_SIZE=22 is ~70 px wide).
// Backing left sits 4 px LEFT of HUD_ALT_X for a little air around
// the tick stems.
export const HUD_ALT_BACKING_X       = HUD_ALT_X - 4;
export const HUD_ALT_BACKING_W       = HUD_ALT_TICK_LONG + HUD_ALT_LABEL_OFFSET_X + 80 + 4;
export const HUD_ALT_BACKING_Y       = HUD_ALT_CY - HUD_ALT_HALF_H;
// Baro endcap height — the extra vertical extent appended to the
// backing strip's bottom for the "29.92in" readout. The TICK area
// remains HUD_ALT_HALF_H * 2 tall; the baro section adds underneath.
// 30 px leaves room for a 20-px-tall cyan baro readout with a few
// px of air above/below, without crowding the lowest tape tick.
export const HUD_ALT_BARO_ENDCAP_H   = 30;
export const HUD_ALT_BACKING_H       = HUD_ALT_HALF_H * 2 + HUD_ALT_BARO_ENDCAP_H;
export const HUD_ALT_BACKING_FILL    = 'rgba(0, 0, 0, 0.10)';
export const HUD_ALT_BACKING_RX      = 8;
// Baro endcap geometry. Sits at the bottom of the backing strip,
// width matches the backing, height = HUD_ALT_BARO_ENDCAP_H. Top
// edge aligns with the bottom of the tick area (HUD_ALT_CY +
// HUD_ALT_HALF_H). Renders as a slightly-darker overlay rect on
// top of the backing strip so the endcap reads as an integrated
// section, not a separate floating element. Tick area (above) +
// baro endcap (below) share the backing's rounded corners.
export const HUD_ALT_BARO_ENDCAP_X   = HUD_ALT_BACKING_X;
export const HUD_ALT_BARO_ENDCAP_Y   = HUD_ALT_CY + HUD_ALT_HALF_H;
export const HUD_ALT_BARO_ENDCAP_W   = HUD_ALT_BACKING_W;
export const HUD_ALT_BARO_ENDCAP_FILL = 'rgba(0, 0, 0, 0.18)';
// "29.92in" text — centered horizontally in the endcap, cyan to
// match FlySto's baro readout color (the only non-monochrome bit
// of the tape stack).
export const HUD_ALT_BARO_CX         = HUD_ALT_BACKING_X + HUD_ALT_BACKING_W / 2;
export const HUD_ALT_BARO_CY         = HUD_ALT_BARO_ENDCAP_Y + HUD_ALT_BARO_ENDCAP_H / 2;
export const HUD_ALT_BARO_FONT_SIZE  = 20;
export const HUD_ALT_BARO_COLOR      = '#5eddef';

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
// left. Pulled 200 px inboard from the original 100 to mirror the
// ALT tape's inboard move, keeping the two tapes closer to the pitch
// ladder. Mirror of HUD_ALT_X = 1620 about HUD_CX = 960: 300.
export const HUD_IAS_X               = 300;
export const HUD_IAS_TICK_SHORT      = 7;    // every 5 kt
export const HUD_IAS_TICK_LONG       = 14;   // every 10 kt
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
// IAS shows 3 digits max (e.g. "139"). Box body width 70 is tight
// to the digits: stationary tens at left edge + sliding ones to its
// right with only a thin reading-slot gap, so "138" reads as one
// number, not two columns. Body height 60 keeps a 12-px tab notch
// comfortably between the rounded corners.
export const HUD_IAS_BOX_W           = 70;
export const HUD_IAS_BOX_H           = 60;
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
// Ones-digit slide-per-knot. Sized to the box HEIGHT so the up/down
// digits sit fully outside the box body when the strip is stationary
// on an integer knot — only the curr digit shows at frac=0. A shorter
// slide leaves the up/down digits half-clipped at the top/bottom of
// the box even at rest.
export const HUD_IAS_ONES_SLIDE_PX   = 60;

// Semi-transparent dark backing strip behind the IAS ticks. Mirror
// of the ALT backing across HUD_CX, with labels rendered to the LEFT
// of the ticks (text-anchor="end" at HUD_IAS_X - HUD_IAS_TICK_LONG -
// HUD_IAS_LABEL_OFFSET_X). Width covers the label column + tick stems
// + a few px of air on either side.
export const HUD_IAS_BACKING_W       = HUD_IAS_TICK_LONG + HUD_IAS_LABEL_OFFSET_X + 80 + 4;
export const HUD_IAS_BACKING_X       = HUD_IAS_X + 4 - HUD_IAS_BACKING_W;
export const HUD_IAS_BACKING_Y       = HUD_IAS_CY - HUD_IAS_HALF_H;
export const HUD_IAS_BACKING_H       = HUD_IAS_HALF_H * 2;
export const HUD_IAS_BACKING_FILL    = 'rgba(0, 0, 0, 0.10)';
export const HUD_IAS_BACKING_RX      = 8;

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
// Text baseline helper
// ---------------------------------------------------------------------
// SVG `dominant-baseline="central"` is interpreted inconsistently
// across browsers and fonts — Safari and Chrome sometimes land glyphs
// 6-8 px off-center, which made the ALT/IAS slide strips look smeared
// (two digits stacked, both partially clipped by the box outline).
// Compute an explicit y offset from font metrics instead: the glyph
// center sits roughly `fontSize * 0.35` BELOW the SVG y coordinate
// when using the default `alphabetic` baseline. Anchoring with
// `y = cy + hudGlyphOffset(fontSize)` and no dominant-baseline
// attribute renders deterministically across browsers.
export const hudGlyphOffset = (fontSize) => fontSize * 0.35;
