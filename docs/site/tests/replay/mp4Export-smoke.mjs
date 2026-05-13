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
  selectChaptersForClip,
} from '../../docs/data-and-logs/replay/lib/replay/chapters.js';

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

// Strip the freshly-minted UUID `id` field so deep-equality assertions
// against builder output stay readable. Identity tested separately
// below.
function stripId(clip) {
  if (!clip) return clip;
  const { id: _id, ...rest } = clip;
  return rest;
}

// ---------------------------------------------------------------------
// buildClipFromPlayhead
// ---------------------------------------------------------------------
console.log('\n--- buildClipFromPlayhead ---');

assertEq(
  stripId(buildClipFromPlayhead(5, 30, sync, 'first clip')),
  { startMs: 13_000, endMs: 43_000, label: 'first clip' },
  'video 5s + 30s duration → log 13000-43000ms'
);

assertTrue(
  typeof buildClipFromPlayhead(5, 30, sync, 'first clip').id === 'string'
    && buildClipFromPlayhead(5, 30, sync, 'first clip').id.length > 0,
  'buildClipFromPlayhead assigns a non-empty string id'
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
  stripId(buildClipFromMarkers(5, 10, sync, 'marked clip')),
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
// Multi-chapter segment math — the export pipeline iterates segments
// from selectChaptersForClip. Tests below mirror the way exportClipAsMp4
// calls into chapters.js + clipToVideoWindow with a timeline.
// ---------------------------------------------------------------------
console.log('\n--- multi-chapter segment math ---');

// Build a 3-chapter timeline: durations 100s, 200s, 150s. Each
// chapter is its own File-like stub since selectChaptersForClip
// only reads {file, startSec, endSec}.
const chapterFiles = [
  { name: 'GOPR0314.MP4', size: 100_000_000 },
  { name: 'GP010314.MP4', size: 200_000_000 },
  { name: 'GP020314.MP4', size: 150_000_000 },
];
const timeline = {
  chapters: [
    { file: chapterFiles[0], chapterIndex: 0, durationSec: 100, startSec: 0,   endSec: 100 },
    { file: chapterFiles[1], chapterIndex: 1, durationSec: 200, startSec: 100, endSec: 300 },
    { file: chapterFiles[2], chapterIndex: 2, durationSec: 150, startSec: 300, endSec: 450 },
  ],
  totalDurationSec: 450,
};

// clipToVideoWindow with multi-chapter durationSec=totalDurationSec.
// The mapping is unchanged from single-file (mp4Export uses
// timeline.totalDurationSec as the clamp source).
assertEq(
  clipToVideoWindow({ startMs: 12_000, endMs: 15_000 }, sync, timeline.totalDurationSec),
  { startVideoSec: 4, endVideoSec: 7 },
  'multi-chapter: clipToVideoWindow with totalDurationSec'
);

// Cross-chapter clip: log 10s..200s → video 2s..192s with
// sync.logTakeoffMs=10000, sync.videoTakeoffSec=2.
// Spans chapter 0 ([0,100)) and chapter 1 ([100,300)).
{
  const clip = { startMs: 10_000, endMs: 200_000 };
  const window = clipToVideoWindow(clip, sync, timeline.totalDurationSec);
  assertEq(
    window,
    { startVideoSec: 2, endVideoSec: 192 },
    'cross-chapter: clipToVideoWindow returns global window'
  );
  const segs = selectChaptersForClip(timeline, window.startVideoSec, window.endVideoSec);
  assertEq(segs.length, 2, 'cross-chapter: 2 segments selected');
  assertEq(segs[0].chapterIndex, 0, 'cross-chapter: first seg is chapter 0');
  assertEq(segs[1].chapterIndex, 1, 'cross-chapter: second seg is chapter 1');
  // Chapter 0 plays from local 2s up to its end (local 100s)
  assertEq(segs[0].localStartSec, 2,   'cross-chapter: seg 0 localStart=2');
  assertEq(segs[0].localEndSec,   100, 'cross-chapter: seg 0 localEnd=100 (chapter end)');
  // Chapter 1 plays from local 0 to local 92s (global 192s - 100s = 92s)
  assertEq(segs[1].localStartSec, 0,   'cross-chapter: seg 1 localStart=0');
  assertEq(segs[1].localEndSec,   92,  'cross-chapter: seg 1 localEnd=92');
  // Cumulative output-time across both segments equals the clip span.
  const seg0DurOut = segs[0].localEndSec - segs[0].localStartSec; // 98
  const seg1DurOut = segs[1].localEndSec - segs[1].localStartSec; // 92
  assertEq(seg0DurOut + seg1DurOut, window.endVideoSec - window.startVideoSec,
           'cross-chapter: cumulative output equals clip span (190s)');
}

// Spans all three chapters.
{
  const segs = selectChaptersForClip(timeline, 50, 400);
  assertEq(segs.length, 3, 'span-3: 3 segments');
  assertEq(
    segs.map(s => [s.localStartSec, s.localEndSec]),
    [[50, 100], [0, 200], [0, 100]],
    'span-3: per-segment local bounds'
  );
  assertEq(
    segs.map(s => [s.globalStartSec, s.globalEndSec]),
    [[50, 100], [100, 300], [300, 400]],
    'span-3: per-segment global bounds align with chapters'
  );
}

// Sanity check: within-chapter export math
// — segmentOffsetSec for seg 0 is 0 (first segment)
// — seg 1's offset equals seg 0's encoded duration
// This locks the offset math the per-segment encode loop uses.
{
  const segs = selectChaptersForClip(timeline, 90, 210);
  // seg 0: chapter 0, local [90, 100), encoded duration 10
  // seg 1: chapter 1, local [0, 110), encoded duration 110
  const seg0Dur = segs[0].localEndSec - segs[0].localStartSec;
  const seg1Dur = segs[1].localEndSec - segs[1].localStartSec;
  assertEq(seg0Dur, 10,  'segment-offset math: seg 0 duration 10');
  assertEq(seg1Dur, 110, 'segment-offset math: seg 1 duration 110');
  // Output cumulative offset at start of seg 1 equals seg 0's duration.
  // Total clip span = seg 0 dur + seg 1 dur = 120 = 210 - 90.
  assertEq(seg0Dur + seg1Dur, 120, 'segment-offset math: cumulative = total span');
}

// ---------------------------------------------------------------------
// Summary
// ---------------------------------------------------------------------
console.log(`\nTests: ${passes} passed, ${fails} failed`);
if (fails > 0) {
  console.error('At least one assertion failed.');
  process.exit(1);
}
console.log('All assertions passed.');
