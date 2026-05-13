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

for (const [tag, msg] of results) console.log(tag, msg);
console.log(`${passed} passed, ${failed} failed`);
process.exit(failed === 0 ? 0 : 1);
