// clipNudge-smoke.mjs — pure-helper smoke test for validateClipEdit
// covering the nudge UX path (−1s / −100ms / +100ms / +1s buttons).
//
// validateClipEdit is the gatekeeper for every clip-edit patch the
// ClipRow emits. The nudge buttons rely on it to silently reject
// over-nudges that would shrink the clip below the 100 ms floor.
//
// Run:
//   node docs/site/tests/replay/clipNudge-smoke.mjs

import { fileURLToPath } from 'node:url';
import path from 'node:path';

const __filename = fileURLToPath(import.meta.url);
const __dirname  = path.dirname(__filename);

const modPath = path.resolve(
  __dirname,
  '../../docs/data-and-logs/replay/lib/replay/clipBuilder.js'
);
const { validateClipEdit } = await import(modPath);

let passed = 0;
let failed = 0;
const results = [];

function assertEq(actual, expected, name) {
  const ok = JSON.stringify(actual) === JSON.stringify(expected);
  if (ok) { passed++; results.push(['PASS', name]); }
  else {
    failed++;
    results.push(['FAIL',
      `${name}\n  expected: ${JSON.stringify(expected)}\n  actual:   ${JSON.stringify(actual)}`]);
  }
}

const clip = { id: 'x', startMs: 5000, endMs: 15000, label: 'test' };

// −100 ms nudge on the start edge: valid, leaves an 10.1 s clip.
{
  const patched = validateClipEdit(clip, { startMs: clip.startMs - 100 });
  assertEq(patched && patched.startMs, 4900,
    'startMs - 100 nudge: validateClipEdit returns startMs=4900');
  assertEq(patched && patched.endMs, 15000,
    'startMs - 100 nudge: endMs unchanged');
  assertEq(patched && patched.id, 'x',
    'startMs - 100 nudge: other fields preserved');
}

// −1000 ms nudge on the start edge: still valid.
{
  const patched = validateClipEdit(clip, { startMs: clip.startMs - 1000 });
  assertEq(patched && patched.startMs, 4000,
    'startMs - 1000 nudge: validateClipEdit returns startMs=4000');
}

// +100 ms nudge on the end edge: valid.
{
  const patched = validateClipEdit(clip, { endMs: clip.endMs + 100 });
  assertEq(patched && patched.endMs, 15100,
    'endMs + 100 nudge: validateClipEdit returns endMs=15100');
}

// +1000 ms nudge on the end edge: valid.
{
  const patched = validateClipEdit(clip, { endMs: clip.endMs + 1000 });
  assertEq(patched && patched.endMs, 16000,
    'endMs + 1000 nudge: validateClipEdit returns endMs=16000');
}

// Over-nudge that would put start within 50 ms of end: rejected
// by the 100 ms floor. ClipRow's onPatch handler treats null as
// "drop the patch silently."
{
  const tooClose = validateClipEdit(clip, { startMs: clip.endMs - 50 });
  assertEq(tooClose, null,
    'startMs nudge inside the 100 ms floor: rejected (null)');
}

// Exactly 100 ms apart: still rejected — the guard is strict-less-than.
{
  const exactly100 = validateClipEdit(clip, { startMs: clip.endMs - 100 });
  assertEq(exactly100 && exactly100.startMs, 14900,
    'startMs nudge to exactly 100 ms span: accepted');
}

// Over-nudge from the end side: endMs nudged below startMs.
{
  const tooClose = validateClipEdit(clip, { endMs: clip.startMs + 50 });
  assertEq(tooClose, null,
    'endMs nudge inside the 100 ms floor: rejected (null)');
}

// Sanity: NaN patches return null rather than corrupting the clip.
{
  const bad = validateClipEdit(clip, { startMs: NaN });
  assertEq(bad, null, 'NaN startMs patch: rejected (null)');
}

// --- Per-button disabled predicates (UX guard against silent no-op) ---
//
// ClipRow simulates each nudge through validateClipEdit and disables
// the button when the result is null. These assertions lock in the
// current invariants so a future change to validateClipEdit (e.g.,
// adding a log-bounds clamp) gets caught by the test rather than
// silently flipping the disabled state.
const canNudge = (c, patch) => validateClipEdit(c, patch) != null;

// Normal clip: all four nudge predicates pass.
{
  const c = { id: 'n', startMs: 5000, endMs: 15000, label: '' };
  assertEq(canNudge(c, { startMs: c.startMs - 1000 }), true,
    'normal clip: −1s start nudge predicate is true');
  assertEq(canNudge(c, { startMs: c.startMs - 100 }), true,
    'normal clip: −100ms start nudge predicate is true');
  assertEq(canNudge(c, { endMs:   c.endMs   + 100 }), true,
    'normal clip: +100ms end nudge predicate is true');
  assertEq(canNudge(c, { endMs:   c.endMs   + 1000 }), true,
    'normal clip: +1s end nudge predicate is true');
}

// Boundary: 101 ms wide clip. All four expanding nudges still pass
// (the 100 ms floor is a SPAN floor, and expanding never approaches it).
{
  const tight = { id: 't', startMs: 5000, endMs: 5101, label: '' };
  assertEq(canNudge(tight, { startMs: tight.startMs - 100 }), true,
    '101 ms clip: −100ms start nudge predicate is true (span grows to 201 ms)');
  assertEq(canNudge(tight, { endMs:   tight.endMs   + 100 }), true,
    '101 ms clip: +100ms end nudge predicate is true (span grows to 201 ms)');
}

// Contracting patch on a small clip: the predicate flips to false.
// This guards the path used by Set in here / Set out here when the
// playhead is too close to the opposite edge.
{
  const c = { id: 's', startMs: 5000, endMs: 5050, label: '' };
  assertEq(canNudge(c, { startMs: 5000, endMs: 5050 }), false,
    'sub-floor clip (50 ms span): predicate correctly returns false');
}

for (const [tag, msg] of results) console.log(tag, msg);
console.log(`${passed} passed, ${failed} failed`);
process.exit(failed === 0 ? 0 : 1);
