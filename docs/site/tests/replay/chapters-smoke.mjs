// chapters-smoke.mjs — pure-logic tests for GoPro chapter ingest.
//
// Covers:
//   - detectGoProChapterPattern: filename → {prefix, seq, chapterIndex}
//   - groupChapterSiblings: largest-cluster selection + sort
//   - buildChapterTimeline: duration aggregation + signature
//   - globalToLocal / localToGlobal: round-trip across boundaries
//   - selectChaptersForClip: within-chapter / cross-boundary / multi-chapter
//   - describeChapterPick: toolbar status text
//
// Run:
//   node docs/site/tests/replay/chapters-smoke.mjs

import {
  detectGoProChapterPattern,
  groupChapterSiblings,
  buildChapterTimeline,
  globalToLocal,
  localToGlobal,
  selectChaptersForClip,
  describeChapterPick,
} from '../../docs/data-and-logs/replay/lib/replay/chapters.js';

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
function assertClose(actual, expected, label, eps = 1e-6) {
  const ok = Math.abs(actual - expected) < eps;
  if (ok) { console.log(`  PASS ${label}`); passes++; }
  else {
    console.log(`  FAIL ${label} expected ${expected} got ${actual}`);
    fails++;
  }
}

// ---------------------------------------------------------------------
// detectGoProChapterPattern
// ---------------------------------------------------------------------
console.log('\n--- detectGoProChapterPattern ---');

assertEq(
  detectGoProChapterPattern('GOPR0314.MP4'),
  { prefix: 'GOPR', seq: '0314', chapterIndex: 0 },
  'GOPR0314.MP4 → chapter 0'
);
assertEq(
  detectGoProChapterPattern('GP010314.MP4'),
  { prefix: 'GP', seq: '0314', chapterIndex: 1 },
  'GP010314.MP4 → chapter 1'
);
assertEq(
  detectGoProChapterPattern('GP090001.MP4'),
  { prefix: 'GP', seq: '0001', chapterIndex: 9 },
  'GP090001.MP4 → chapter 9 (max)'
);
assertEq(
  detectGoProChapterPattern('not-a-gopro-name.MP4'),
  null,
  'arbitrary filename → null'
);
assertEq(
  detectGoProChapterPattern('gopr0314.mp4'),
  null,
  'lowercase variant rejected (GoPro uses uppercase)'
);
assertEq(
  detectGoProChapterPattern(''),
  null,
  'empty string → null'
);
assertEq(
  detectGoProChapterPattern(null),
  null,
  'null → null'
);
// Symlink rename pattern Playwright uses: chapter01-GOPR0314.MP4. The
// detector tolerates a leading non-alphanumeric chunk so the test
// fixtures match without needing copies of the real files.
assertEq(
  detectGoProChapterPattern('chapter01-GOPR0314.MP4'),
  { prefix: 'GOPR', seq: '0314', chapterIndex: 0 },
  'symlink-rename "chapter01-GOPR0314.MP4" still matches'
);
assertEq(
  detectGoProChapterPattern('chapter02-GP010314.MP4'),
  { prefix: 'GP', seq: '0314', chapterIndex: 1 },
  'symlink-rename "chapter02-GP010314.MP4" matches'
);
// Negative discrimination: GP10 would be GP1 + 0... rather than GP01 ... — not a chapter
assertEq(
  detectGoProChapterPattern('GP100314.MP4'),
  null,
  'GP100314.MP4 (chapter 10 — out of GoPro spec) → null'
);
// Negative discrimination: free-text prefixes (space, etc.) must not
// match. The allowed delimiters before the GoPro token are only `-`
// and `_`. Bulldog flagged this as a false-positive risk if a pilot
// names a flight log "My flight GOPR0314.MP4".
assertEq(
  detectGoProChapterPattern('My flight GOPR0314.MP4'),
  null,
  'free-text prefix with space → null'
);
assertEq(
  detectGoProChapterPattern('flight.GOPR0314.MP4'),
  null,
  'free-text prefix with `.` → null'
);
assertEq(
  detectGoProChapterPattern('_GOPR0314.MP4'),
  { prefix: 'GOPR', seq: '0314', chapterIndex: 0 },
  'underscore prefix still matches'
);

// ---------------------------------------------------------------------
// groupChapterSiblings
// ---------------------------------------------------------------------
console.log('\n--- groupChapterSiblings ---');

function mkFile(name, size = 1000) {
  return { name, size };
}

assertEq(
  groupChapterSiblings([]),
  [],
  'empty input → empty cluster'
);

assertEq(
  groupChapterSiblings([mkFile('GOPR0314.MP4')]).map(c => c.chapterIndex),
  [0],
  'single GOPR file → single-chapter cluster'
);

// Four siblings unsorted: expect sort by chapterIndex.
{
  const files = [
    mkFile('GP020314.MP4'),
    mkFile('GP010314.MP4'),
    mkFile('GOPR0314.MP4'),
    mkFile('GP030314.MP4'),
  ];
  const got = groupChapterSiblings(files);
  assertEq(
    got.map(c => c.chapterIndex),
    [0, 1, 2, 3],
    '4 unsorted siblings → sorted [0,1,2,3]'
  );
  assertEq(
    got.map(c => c.file.name),
    ['GOPR0314.MP4', 'GP010314.MP4', 'GP020314.MP4', 'GP030314.MP4'],
    '4 siblings → correct file order'
  );
}

// Mixed seq: largest cluster wins. Cluster A (3 files seq 0314), cluster B (2 files seq 0999).
{
  const files = [
    mkFile('GOPR0314.MP4'),
    mkFile('GOPR0999.MP4'),
    mkFile('GP010314.MP4'),
    mkFile('GP010999.MP4'),
    mkFile('GP020314.MP4'),
  ];
  const got = groupChapterSiblings(files);
  assertEq(
    got.length, 3,
    'mixed seq → larger cluster (3) wins over smaller (2)'
  );
  assertEq(
    got.every(c => /0314/.test(c.file.name)),
    true,
    'returned chapters are all seq 0314'
  );
}

// Non-GoPro filenames mixed in are filtered out cleanly.
{
  const files = [
    mkFile('GOPR0314.MP4'),
    mkFile('audio-track.wav'),
    mkFile('GP010314.MP4'),
    mkFile('some-other.mp4'),
  ];
  const got = groupChapterSiblings(files);
  assertEq(got.length, 2, '2 GoPro + 2 non-GoPro → cluster size 2');
}

// All non-GoPro: empty cluster (caller falls back to legacy single-video).
{
  const got = groupChapterSiblings([mkFile('flight.mp4'), mkFile('clip.mov')]);
  assertEq(got, [], 'all non-GoPro → empty cluster');
}

// Symlinked Playwright-fixture names produce the same cluster as the
// canonical names.
{
  const files = [
    mkFile('chapter04-GP030314.MP4'),
    mkFile('chapter01-GOPR0314.MP4'),
    mkFile('chapter03-GP020314.MP4'),
    mkFile('chapter02-GP010314.MP4'),
  ];
  const got = groupChapterSiblings(files);
  assertEq(
    got.map(c => c.chapterIndex),
    [0, 1, 2, 3],
    'Playwright-rename symlinks → sorted cluster'
  );
}

// ---------------------------------------------------------------------
// buildChapterTimeline
// ---------------------------------------------------------------------
console.log('\n--- buildChapterTimeline ---');

async function buildTimelineWithDurations(durations) {
  const chapters = durations.map((_, i) => ({
    file: mkFile(i === 0 ? 'GOPR0314.MP4' : `GP0${i}0314.MP4`, 1_000_000 + i),
    chapterIndex: i,
  }));
  return await buildChapterTimeline(chapters, {
    probeDuration: (f) => Promise.resolve(durations[chapters.findIndex(c => c.file === f)]),
  });
}

{
  const t = await buildTimelineWithDurations([10, 20, 30]);
  assertEq(t.chapters.length, 3, 'timeline has 3 chapters');
  assertEq(t.totalDurationSec, 60, 'totalDurationSec = sum of chapter durations');
  assertEq(
    t.chapters.map(c => c.startSec),
    [0, 10, 30],
    'startSec accumulates correctly'
  );
  assertEq(
    t.chapters.map(c => c.endSec),
    [10, 30, 60],
    'endSec accumulates correctly'
  );
  // signature is stable + content-keyed
  assertTrue(
    typeof t.signature === 'string' && t.signature.length > 0,
    'signature is a non-empty string'
  );
  assertTrue(
    t.signature.includes('GOPR0314.MP4') && t.signature.includes('GP010314.MP4'),
    'signature includes chapter filenames'
  );
}

// Single-chapter timeline is the degenerate case — same shape as multi.
{
  const t = await buildTimelineWithDurations([42]);
  assertEq(t.chapters.length, 1, 'single chapter → length 1');
  assertEq(t.totalDurationSec, 42, 'single chapter total = its duration');
  assertEq(t.chapters[0].startSec, 0, 'single chapter starts at 0');
  assertEq(t.chapters[0].endSec, 42, 'single chapter ends at its duration');
}

// ---------------------------------------------------------------------
// globalToLocal / localToGlobal round-trip
// ---------------------------------------------------------------------
console.log('\n--- globalToLocal / localToGlobal ---');

{
  const t = await buildTimelineWithDurations([10, 20, 30]);

  // Within chapter 0
  assertEq(globalToLocal(t, 5),  { chapterIndex: 0, localSec: 5 },  'global 5 → chapter 0 local 5');
  // Right at the chapter 0→1 boundary: 10 belongs to chapter 1 (start-inclusive)
  assertEq(globalToLocal(t, 10), { chapterIndex: 1, localSec: 0 },  'global 10 → chapter 1 local 0 (boundary)');
  // Mid chapter 1
  assertEq(globalToLocal(t, 15), { chapterIndex: 1, localSec: 5 },  'global 15 → chapter 1 local 5');
  // Right at chapter 1→2 boundary
  assertEq(globalToLocal(t, 30), { chapterIndex: 2, localSec: 0 },  'global 30 → chapter 2 local 0');
  // Mid chapter 2
  assertEq(globalToLocal(t, 45), { chapterIndex: 2, localSec: 15 }, 'global 45 → chapter 2 local 15');
  // Past end clamps to last chapter's end
  assertEq(globalToLocal(t, 999), { chapterIndex: 2, localSec: 30 }, 'past-end clamps to last chapter end');
  // Negative clamps to start
  assertEq(globalToLocal(t, -5), { chapterIndex: 0, localSec: 0 },  'negative clamps to 0');
  // NaN → start
  assertEq(globalToLocal(t, NaN), { chapterIndex: 0, localSec: 0 }, 'NaN clamps to 0');

  // Inverse mapping
  assertClose(localToGlobal(t, 0, 5),  5,  'chapter 0 local 5  → global 5');
  assertClose(localToGlobal(t, 1, 5),  15, 'chapter 1 local 5  → global 15');
  assertClose(localToGlobal(t, 2, 15), 45, 'chapter 2 local 15 → global 45');
  // Round-trip through boundaries
  for (const g of [0.0, 9.99, 10.0, 10.01, 25, 29.99, 30, 50, 59.9]) {
    const { chapterIndex, localSec } = globalToLocal(t, g);
    const back = localToGlobal(t, chapterIndex, localSec);
    assertClose(back, g, `round-trip at global ${g}`, 1e-9);
  }
  // Out-of-range chapter clamps
  assertClose(localToGlobal(t, -1, 0),  0, 'negative chapterIndex clamps to 0');
  assertClose(localToGlobal(t, 99, 5),  35, 'too-high chapterIndex clamps to last (2), local 5 → 35');
}

// ---------------------------------------------------------------------
// selectChaptersForClip
// ---------------------------------------------------------------------
console.log('\n--- selectChaptersForClip ---');

{
  // chapters 0..2 with [0,10), [10,30), [30,60) durations 10/20/30
  const t = await buildTimelineWithDurations([10, 20, 30]);

  // Entirely within chapter 1.
  {
    const segs = selectChaptersForClip(t, 12, 18);
    assertEq(segs.length, 1, 'within-chapter: 1 segment');
    assertEq(segs[0].chapterIndex, 1, 'within-chapter: chapter 1');
    assertClose(segs[0].localStartSec, 2, 'within-chapter: localStart');
    assertClose(segs[0].localEndSec,   8, 'within-chapter: localEnd');
    assertClose(segs[0].globalStartSec, 12, 'within-chapter: globalStart');
    assertClose(segs[0].globalEndSec,   18, 'within-chapter: globalEnd');
  }

  // Crosses one boundary (chapter 1 → 2).
  {
    const segs = selectChaptersForClip(t, 25, 35);
    assertEq(segs.length, 2, 'cross-boundary: 2 segments');
    assertEq(segs[0].chapterIndex, 1, 'cross-boundary: seg 0 = chapter 1');
    assertEq(segs[1].chapterIndex, 2, 'cross-boundary: seg 1 = chapter 2');
    assertClose(segs[0].localStartSec, 15, 'cross-boundary: seg 0 localStart');
    assertClose(segs[0].localEndSec,   20, 'cross-boundary: seg 0 localEnd (= chapter 1 duration)');
    assertClose(segs[1].localStartSec, 0,  'cross-boundary: seg 1 localStart 0');
    assertClose(segs[1].localEndSec,   5,  'cross-boundary: seg 1 localEnd 5');
    // global ranges align with seg local ranges
    assertClose(segs[0].globalStartSec, 25, 'cross-boundary: seg 0 globalStart');
    assertClose(segs[1].globalEndSec,   35, 'cross-boundary: seg 1 globalEnd');
  }

  // Spans all three chapters.
  {
    const segs = selectChaptersForClip(t, 5, 55);
    assertEq(segs.length, 3, 'span-3: 3 segments');
    assertEq(segs.map(s => s.chapterIndex), [0, 1, 2], 'span-3: chapters 0,1,2');
    assertClose(segs[0].localStartSec, 5,  'span-3: seg 0 localStart');
    assertClose(segs[0].localEndSec,  10,  'span-3: seg 0 localEnd (full chapter end)');
    assertClose(segs[1].localStartSec, 0,  'span-3: seg 1 localStart 0');
    assertClose(segs[1].localEndSec,  20,  'span-3: seg 1 localEnd (full chapter end)');
    assertClose(segs[2].localStartSec, 0,  'span-3: seg 2 localStart 0');
    assertClose(segs[2].localEndSec,  25,  'span-3: seg 2 localEnd 25');
  }

  // Clip starts before timeline, ends mid-chapter-1.
  {
    const segs = selectChaptersForClip(t, -10, 15);
    assertEq(segs.length, 2, 'pre-start: 2 segments');
    assertClose(segs[0].localStartSec, 0, 'pre-start: seg 0 starts at 0 (clamped)');
    assertClose(segs[0].globalStartSec, 0, 'pre-start: globalStart clamps to 0 too');
  }

  // Clip past end.
  {
    const segs = selectChaptersForClip(t, 1000, 2000);
    assertEq(segs.length, 0, 'past-end: 0 segments');
  }

  // Inverted clip
  {
    const segs = selectChaptersForClip(t, 30, 20);
    assertEq(segs.length, 0, 'inverted clip: 0 segments');
  }
}

// ---------------------------------------------------------------------
// describeChapterPick
// ---------------------------------------------------------------------
console.log('\n--- describeChapterPick ---');

{
  const tSingle = await buildTimelineWithDurations([120]);
  assertEq(
    describeChapterPick(tSingle, 'GOPR0314.MP4'),
    'GOPR0314.MP4',
    'single chapter → just the filename'
  );
  const tMulti = await buildTimelineWithDurations([60, 120, 180]);
  const label = describeChapterPick(tMulti, 'GOPR0314.MP4');
  assertTrue(
    label.includes('GOPR0314.MP4') && label.includes('2 chapters') && label.includes('m'),
    `multi chapter label includes name, "+2 chapters", and duration: "${label}"`
  );
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
