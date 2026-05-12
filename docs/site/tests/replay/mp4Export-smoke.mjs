// mp4Export-smoke.mjs — pure-logic tests for the MP4 export pipeline.
//
// What this covers:
//   - clipToVideoWindow: clip → video-time window mapping with sync
//     and video-duration clamping.
//   - expectedFrameCount: clip-duration × framerate frame count math.
//   - buildClipFromPlayhead / buildClipFromMarkers: clip construction.
//   - validateClipEdit: edit-time validation (start < end, 100ms floor).
//   - updateClipAt / removeClipAt: immutable clip-list operations.
//   - isMp4ExportSupported: browser feature gate (negative case in Node).
//
// What this DOESN'T cover:
//   - The actual VideoEncoder + Muxer pipeline. WebCodecs is a browser-
//     only API; running mp4Export.exportClipAsMp4 in Node would hit the
//     `isMp4ExportSupported() === false` branch immediately. Manual
//     verification: mkdocs serve → load replay page → mark a clip →
//     click Export MP4 → confirm the downloaded .mp4 plays.
//
// Run:
//   node tests/replay/mp4Export-smoke.mjs

import {
  clipToVideoWindow,
  expectedFrameCount,
  isMp4ExportSupported,
  isOverlayExportSupported,
  OVERLAY_MODE_IDS,
  OVERLAY_MODE_ORDER,
  rotationFromTkhdMatrix,
} from '../../docs/data-and-logs/replay/lib/replay/mp4Export.js';

import {
  buildClipFromPlayhead,
  buildClipFromMarkers,
  validateClipEdit,
  updateClipAt,
  removeClipAt,
  defaultClipLabel,
} from '../../docs/data-and-logs/replay/lib/replay/clipBuilder.js';

let passes = 0, fails = 0;
function assertEq(actual, expected, label) {
  const ok = JSON.stringify(actual) === JSON.stringify(expected);
  if (ok) {
    console.log(`  PASS ${label}`);
    passes++;
  } else {
    console.log(`  FAIL ${label}`);
    console.log(`       expected: ${JSON.stringify(expected)}`);
    console.log(`       actual:   ${JSON.stringify(actual)}`);
    fails++;
  }
}
function assertTrue(cond, label) { assertEq(!!cond, true, label); }
function assertFalse(cond, label) { assertEq(!!cond, false, label); }

// ---------------------------------------------------------------------
// clipToVideoWindow
// ---------------------------------------------------------------------
console.log('\n--- clipToVideoWindow ---');

// Sync: log t=10s ↔ video t=2s. clip from log 12000ms..15000ms
// → video 4s..7s.
const sync = { logTakeoffMs: 10_000, videoTakeoffSec: 2 };

assertEq(
  clipToVideoWindow({ startMs: 12_000, endMs: 15_000 }, sync, 60),
  { startVideoSec: 4, endVideoSec: 7 },
  'happy path: in-range clip maps to expected video window'
);

assertEq(
  clipToVideoWindow({ startMs: 12_000, endMs: 15_000 }, sync, 5),
  { startVideoSec: 4, endVideoSec: 5 },
  'end clamps to video duration when clip overruns'
);

assertEq(
  clipToVideoWindow({ startMs: 5_000, endMs: 15_000 }, sync, 60),
  { startVideoSec: 0, endVideoSec: 7 },
  'start clamps to 0 when clip pre-dates video'
);

assertEq(
  clipToVideoWindow({ startMs: 100_000, endMs: 110_000 }, sync, 5),
  null,
  'returns null when clip falls entirely past video end'
);

assertEq(
  clipToVideoWindow({ startMs: 15_000, endMs: 12_000 }, sync, 60),
  null,
  'returns null when endMs <= startMs (inverted clip)'
);

assertEq(
  clipToVideoWindow({ startMs: 12_000, endMs: 15_000 }, null, 60),
  null,
  'returns null when sync is missing'
);

assertEq(
  clipToVideoWindow({ startMs: 12_000, endMs: 15_000 },
                    { logTakeoffMs: NaN, videoTakeoffSec: 2 }, 60),
  null,
  'returns null when sync has NaN log anchor'
);

// ---------------------------------------------------------------------
// expectedFrameCount
// ---------------------------------------------------------------------
console.log('\n--- expectedFrameCount ---');

assertEq(expectedFrameCount(10, 30), 300, '10 s @ 30 fps = 300 frames');
assertEq(expectedFrameCount(0.5, 60), 30, '500 ms @ 60 fps = 30 frames');
assertEq(expectedFrameCount(0, 30), 0, 'zero duration → 0 frames');
assertEq(expectedFrameCount(-5, 30), 0, 'negative duration → 0 frames');
assertEq(expectedFrameCount(1, 0), 0, 'zero framerate → 0 frames');
assertEq(expectedFrameCount(1, NaN), 0, 'NaN framerate → 0 frames');
// 33ms @ 30fps would be ~1 frame; rounding 0.99 → 1.
assertEq(expectedFrameCount(0.033, 30), 1, 'sub-frame duration rounds up to 1');

// ---------------------------------------------------------------------
// isMp4ExportSupported (Node = no window)
// ---------------------------------------------------------------------
console.log('\n--- isMp4ExportSupported (no window) ---');

assertFalse(isMp4ExportSupported(),
            'Node environment has no window → returns false');

// ---------------------------------------------------------------------
// isOverlayExportSupported (Node = no window)
// ---------------------------------------------------------------------
console.log('\n--- isOverlayExportSupported (no window) ---');

assertFalse(isOverlayExportSupported(),
            'Node environment has no window → returns false');

// ---------------------------------------------------------------------
// OVERLAY_MODE_IDS / OVERLAY_MODE_ORDER
// ---------------------------------------------------------------------
console.log('\n--- Overlay mode tables ---');

// Every UI-facing mode id maps to the same M5Sim displayType the
// IndexerPage / M5_MODES list uses (Energy=0, Attitude=1, Indexer=2,
// Decel=3, Historic-G=4). Locks the protocol.
assertEq(OVERLAY_MODE_IDS['energy'],     0, 'energy → displayType 0');
assertEq(OVERLAY_MODE_IDS['attitude'],   1, 'attitude → displayType 1');
assertEq(OVERLAY_MODE_IDS['indexer'],    2, 'indexer → displayType 2');
assertEq(OVERLAY_MODE_IDS['decel'],      3, 'decel → displayType 3');
assertEq(OVERLAY_MODE_IDS['historic-g'], 4, 'historic-g → displayType 4');

assertEq(OVERLAY_MODE_ORDER.length, 5,
         'OVERLAY_MODE_ORDER lists all five modes');
assertTrue(OVERLAY_MODE_ORDER.every(id => id in OVERLAY_MODE_IDS),
           'every OVERLAY_MODE_ORDER entry has a displayType mapping');

// OVERLAY_MODE_IDS is frozen — locking the wire protocol.
assertTrue(Object.isFrozen(OVERLAY_MODE_IDS),
           'OVERLAY_MODE_IDS is frozen');
assertTrue(Object.isFrozen(OVERLAY_MODE_ORDER),
           'OVERLAY_MODE_ORDER is frozen');

// ---------------------------------------------------------------------
// buildClipFromPlayhead
// ---------------------------------------------------------------------
console.log('\n--- buildClipFromPlayhead ---');

assertEq(
  buildClipFromPlayhead(5, 30, sync, 'first clip'),
  { startMs: 13_000, endMs: 43_000, label: 'first clip' },
  'video 5s + 30s duration → log 13000-43000ms'
);

assertEq(
  buildClipFromPlayhead(5, 30, null, 'no sync'),
  null,
  'no sync → null clip'
);

assertEq(
  buildClipFromPlayhead(NaN, 30, sync, 'bad video time'),
  null,
  'NaN video time → null clip'
);

// ---------------------------------------------------------------------
// buildClipFromMarkers
// ---------------------------------------------------------------------
console.log('\n--- buildClipFromMarkers ---');

assertEq(
  buildClipFromMarkers(5, 10, sync, 'marked clip'),
  { startMs: 13_000, endMs: 18_000, label: 'marked clip' },
  'video [5s, 10s] → log [13000, 18000]ms'
);

assertEq(
  buildClipFromMarkers(10, 5, sync, 'inverted'),
  null,
  'out ≤ in → null clip'
);

assertEq(
  buildClipFromMarkers(5, 5, sync, 'zero-length'),
  null,
  'in === out → null clip'
);

assertEq(
  buildClipFromMarkers(5, 10, null, 'no sync'),
  null,
  'no sync → null clip'
);

// ---------------------------------------------------------------------
// validateClipEdit
// ---------------------------------------------------------------------
console.log('\n--- validateClipEdit ---');

const baseClip = { startMs: 1_000, endMs: 5_000, label: 'a' };

assertEq(
  validateClipEdit(baseClip, { endMs: 8_000 }),
  { startMs: 1_000, endMs: 8_000, label: 'a' },
  'extend endMs → returns merged clip'
);

assertEq(
  validateClipEdit(baseClip, { startMs: 4_000 }),
  { startMs: 4_000, endMs: 5_000, label: 'a' },
  'narrow startMs → returns merged clip'
);

assertEq(
  validateClipEdit(baseClip, { startMs: 5_000 }),
  null,
  'startMs === endMs → null (zero-span)'
);

assertEq(
  validateClipEdit(baseClip, { startMs: 4_950 }),
  null,
  'span < 100 ms → null (below floor)'
);

assertEq(
  validateClipEdit(baseClip, { startMs: 6_000 }),
  null,
  'startMs > endMs → null (inverted)'
);

assertEq(
  validateClipEdit(baseClip, { label: 'renamed' }),
  { startMs: 1_000, endMs: 5_000, label: 'renamed' },
  'label-only edit → returns merged clip'
);

// ---------------------------------------------------------------------
// updateClipAt / removeClipAt
// ---------------------------------------------------------------------
console.log('\n--- updateClipAt / removeClipAt ---');

const clips = [
  { startMs: 1_000, endMs: 5_000, label: 'a' },
  { startMs: 6_000, endMs: 9_000, label: 'b' },
  { startMs: 10_000, endMs: 12_000, label: 'c' },
];

const updated = updateClipAt(clips, 1, { label: 'B-renamed' });
assertEq(updated.length, 3, 'updateClipAt preserves array length');
assertEq(updated[1].label, 'B-renamed', 'updateClipAt patches the target row');
assertEq(updated[0].label, 'a', 'updateClipAt leaves other rows alone');
assertTrue(updated !== clips, 'updateClipAt returns a new array');
assertTrue(updated[1] !== clips[1], 'updateClipAt returns a new row object');

const removed = removeClipAt(clips, 1);
assertEq(removed.length, 2, 'removeClipAt shrinks array by 1');
assertEq(removed.map(c => c.label), ['a', 'c'], 'removeClipAt removes the right row');
assertTrue(removed !== clips, 'removeClipAt returns a new array');

// ---------------------------------------------------------------------
// defaultClipLabel
// ---------------------------------------------------------------------
console.log('\n--- defaultClipLabel ---');

assertEq(defaultClipLabel(0), 'clip 01', 'index 0 → "clip 01"');
assertEq(defaultClipLabel(9), 'clip 10', 'index 9 → "clip 10"');
assertEq(defaultClipLabel(99), 'clip 100', 'index 99 → "clip 100"');

// ---------------------------------------------------------------------
// rotationFromTkhdMatrix — recover display rotation from tkhd matrix.
// Canonical matrices in 16.16 fixed-point. GoPro -180° is the case
// that triggered this code path.
// ---------------------------------------------------------------------
console.log('\n--- rotationFromTkhdMatrix ---');

// Identity (no rotation): a=1, b=0.
assertEq(
  rotationFromTkhdMatrix([65536, 0, 0, 0, 65536, 0, 0, 0, 1073741824]),
  0,
  'identity matrix → 0°'
);

// 90° clockwise: a=0, b=1.
assertEq(
  rotationFromTkhdMatrix([0, 65536, 0, -65536, 0, 0, 0, 0, 1073741824]),
  90,
  '90° rotation matrix → 90°'
);

// 180°: a=-1, b=0. This is GoPro's case (also expressed as rotation=-180
// in ffprobe — same orientation, different sign convention).
assertEq(
  rotationFromTkhdMatrix([-65536, 0, 0, 0, -65536, 0, 0, 0, 1073741824]),
  180,
  '180° rotation matrix → 180° (GoPro upside-down case)'
);

// 270° (a.k.a. -90°): a=0, b=-1.
assertEq(
  rotationFromTkhdMatrix([0, -65536, 0, 65536, 0, 0, 0, 0, 1073741824]),
  270,
  '270° rotation matrix → 270°'
);

// Already-float matrix (some upstream parsers unpack 16.16 → float
// on parse, so the helper must accept either shape).
assertEq(
  rotationFromTkhdMatrix([-1, 0, 0, 0, -1, 0, 0, 0, 1]),
  180,
  'float-unpacked 180° matrix → 180°'
);

// Missing / undersized matrix.
assertEq(rotationFromTkhdMatrix(null), 0, 'null matrix → 0°');
assertEq(rotationFromTkhdMatrix([1, 0]), 0, 'undersized matrix → 0°');

// ---------------------------------------------------------------------
// Summary
// ---------------------------------------------------------------------
console.log(`\nTests: ${passes} passed, ${fails} failed`);
if (fails > 0) {
  console.error('At least one assertion failed.');
  process.exit(1);
}
console.log('All assertions passed.');
