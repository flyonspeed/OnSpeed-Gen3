// test_replay_wire_completeness.js — wire-frame completeness test for
// LogReplayEngine + BuildDisplayFrame (PR 1.5 of Project B2).
//
// PR 1 closed Invariant 1 (rendering owned by M5 firmware). This test
// closes Invariant 2 (wire bytes feeding the M5 are complete).
//
// Pipeline tested:
//   CSV log row -> LogReplayEngine.step (WASM) -> ReplayStepResult ->
//   wireBridgeForTest -> DisplayBuildInputs -> Core.build_display_frame
//   -> 77 wire bytes -> compare against frozen golden
//
// When the wire format or any engine-populated field changes, the test
// fails on byte-mismatch and the maintainer must regenerate the golden,
// forcing a conscious "is this change correct?" review.
//
// Run:
//   node software/OnSpeed-M5-Display/test/test_replay_wire_completeness.js
//
// Regenerate the golden after a legitimate engine change:
//   node software/OnSpeed-M5-Display/test/test_replay_wire_completeness.js \
//     --update-golden
//
// Exit code 0 = bytes match golden; non-zero on any mismatch or test
// infrastructure failure.

'use strict';

const path = require('path');
const fs   = require('fs');

// PR 2 of Project B2: this test now imports the production wireBridge
// from `tools/web/lib/replay/wireBridge.js`. Pre-PR-2 we had a local
// `wireBridgeForTest.js` here that hand-built the DisplayBuildInputs
// object; that file is gone (its logic is subsumed by wireBridge.js).
//
// `tools/web/` declares `"type": "module"` in its package.json, so the
// production wireBridge is an ES module. `pathToFileURL(...).href` is
// the canonical way to dynamic-import an absolute file path from a
// CJS test file.
const WIRE_BRIDGE_JS = path.resolve(
  __dirname, '..', '..', '..', 'tools', 'web', 'lib', 'replay', 'wireBridge.js');

const FIXTURE_DIR  = path.resolve(__dirname, 'fixtures');
const FIXTURE_LOG  = path.join(FIXTURE_DIR, 'wire_completeness_log.csv');
const GOLDEN_FILE  = path.join(FIXTURE_DIR, 'wire_completeness_golden.bin');

// Output of software/Libraries/onspeed_core/wasm/build_wasm.sh.
const CORE_JS = path.resolve(
  __dirname, '..', '..', 'Libraries', 'onspeed_core', 'wasm', 'dist',
  'onspeed_core.js');

const updateGolden = process.argv.includes('--update-golden');

if (!fs.existsSync(CORE_JS)) {
  console.error(`FATAL: ${CORE_JS} not found.`);
  console.error('Run `bash software/Libraries/onspeed_core/wasm/build_wasm.sh` first.');
  process.exit(2);
}
if (!fs.existsSync(FIXTURE_LOG)) {
  console.error(`FATAL: fixture log not found at ${FIXTURE_LOG}`);
  process.exit(2);
}

// ---------------------------------------------------------------------------
// Fixture log parsing — minimal CSV reader for the test fixture.
//
// The fixture is a deliberately small CSV with only the columns the WASM
// engine reads (not a full SD log). We parse it ourselves rather than
// reusing onspeed_core's `LogCsv` parser because we don't need the full
// schema; we just need numeric / boolean fields off a fixed header.
// ---------------------------------------------------------------------------
function parseFixtureLog(csvPath) {
  const text  = fs.readFileSync(csvPath, 'utf8');
  const lines = text.split('\n').filter(l => l.trim().length > 0);
  if (lines.length < 2) {
    throw new Error(`Fixture log has no data rows: ${csvPath}`);
  }
  const header = lines[0].split(',').map(s => s.trim());
  const rows   = [];
  for (let i = 1; i < lines.length; i++) {
    const fields = lines[i].split(',').map(s => s.trim());
    const row = {};
    for (let c = 0; c < header.length; c++) {
      const key = header[c];
      const val = fields[c];
      if (val === 'true')        row[key] = true;
      else if (val === 'false')  row[key] = false;
      else                       row[key] = Number(val);
    }
    rows.push(row);
  }
  return rows;
}

// ---------------------------------------------------------------------------
// Test config — multiple flap detents with distinct anchor values, so a
// sabotage that corrupts `out.flapsIndex` (e.g. always picks index 0)
// shifts every per-flap anchor and `flapsDeg` on the wire.
//
// Detent 0 (clean):
//   alpha_0 = -3.0, alpha_stall = 13.0  → span 16°
//   For aoa = 5° → percentLift = (5-(-3))/16 * 100 = 50.0%
//
// Detent 20 (full flap): different alpha_0/alpha_stall and anchors so
// when the fixture sits on this detent the wire bytes for `flapsDeg` and
// the anchor bytes are demonstrably different from detent 0.
// ---------------------------------------------------------------------------
const TEST_CONFIG = {
  aoaSmoothing: 4,
  pressureSmoothing: 4,
  muteUnderIas: 30,
  // Other scalar fields use parse_config defaults.
  flaps: [
    {
      degrees:        0,
      potPosition:    2048,
      alpha0:         -3.0,
      alphaStall:     13.0,
      ldmaxAoa:        2.0,    // → ~31%
      onSpeedFastAoa:  4.0,    // → ~44%
      onSpeedSlowAoa:  6.0,    // → ~56%
      stallWarnAoa:   11.0,    // → ~88%
      kFit:           0.0,
    },
    {
      degrees:        20,
      potPosition:    1024,
      alpha0:         -2.0,
      alphaStall:     11.0,
      ldmaxAoa:        1.0,    // → ~23%
      onSpeedFastAoa:  3.0,    // → ~38%
      onSpeedSlowAoa:  5.0,    // → ~54%
      stallWarnAoa:    9.5,    // → ~88%
      kFit:           0.0,
    }
  ],
};

const SAMPLE_RATE_HZ = 50;          // 50 ms between fixture rows -> 20 Hz nominal,
                                    // but engine takes "log sample rate" — 50 Hz
                                    // is a real iLogRate value the firmware uses.

// ---------------------------------------------------------------------------
// Run the pipeline and produce the concatenated wire bytes.
// ---------------------------------------------------------------------------
async function runPipeline() {
  const coreUrl     = require('url').pathToFileURL(CORE_JS).href;
  const CoreFactory = (await import(coreUrl)).default;
  const Core        = await CoreFactory();

  if (typeof Core.LogReplayEngine !== 'function') {
    throw new Error(
      'onspeed_core WASM is missing the LogReplayEngine export — '
      + 'rebuild via wasm/build_wasm.sh');
  }
  if (typeof Core.build_display_frame !== 'function') {
    throw new Error(
      'onspeed_core WASM is missing build_display_frame — '
      + 'rebuild via wasm/build_wasm.sh');
  }

  // Production wireBridge — ESM module under tools/web/lib/replay/. The
  // golden bytes were generated by PR 1.5's local helper; PR 2's
  // wireBridge keeps the same DisplayBuildInputs schema, so the same
  // bytes come out. If they don't, this test catches the drift.
  const wireBridgeUrl  = require('url').pathToFileURL(WIRE_BRIDGE_JS).href;
  const { buildDisplayInputs } = await import(wireBridgeUrl);

  const engine = new Core.LogReplayEngine(
    TEST_CONFIG, SAMPLE_RATE_HZ, /*flapsRawAdcAvailable=*/true);
  try {
    const rows  = parseFixtureLog(FIXTURE_LOG);
    const wireFrames = [];
    for (const row of rows) {
      const stepResult = engine.step(row);
      if (stepResult === null || stepResult === undefined) {
        // Synth-path lag wouldn't fire here (flapsRawAdcAvailable=true)
        // — skipping for safety.
        continue;
      }
      // Production wireBridge takes the full cfg object and derives
      // flapsMin/flapsMax internally — the test cfg's `.flaps` array is
      // the same shape parse_config produces.
      const inputs = buildDisplayInputs(stepResult, TEST_CONFIG);
      const frameBytes = Buffer.from(Core.build_display_frame(inputs));
      if (frameBytes.length !== 77) {
        throw new Error(
          `Frame length ${frameBytes.length}, expected 77 — wire format drift?`);
      }
      wireFrames.push(frameBytes);
    }
    return Buffer.concat(wireFrames);
  } finally {
    engine.delete();
  }
}

// ---------------------------------------------------------------------------
// Compare two buffers byte-for-byte. Returns the first differing index, or
// -1 on full match.
// ---------------------------------------------------------------------------
function firstDiff(a, b) {
  const len = Math.min(a.length, b.length);
  for (let i = 0; i < len; i++) {
    if (a[i] !== b[i]) return i;
  }
  return (a.length === b.length) ? -1 : len;
}

// ---------------------------------------------------------------------------

async function main() {
  console.log(`Running wire-completeness test`);
  console.log(`  fixture: ${FIXTURE_LOG}`);
  console.log(`  golden:  ${GOLDEN_FILE}`);

  const actual = await runPipeline();
  console.log(`  pipeline produced ${actual.length} bytes ` +
              `(${actual.length / 77} frames)`);

  if (updateGolden) {
    fs.mkdirSync(FIXTURE_DIR, { recursive: true });
    fs.writeFileSync(GOLDEN_FILE, actual);
    console.log(`  WROTE golden: ${actual.length} bytes -> ${GOLDEN_FILE}`);
    process.exit(0);
  }

  if (!fs.existsSync(GOLDEN_FILE)) {
    console.error(`FAIL: golden file not found at ${GOLDEN_FILE}`);
    console.error('      Generate it with --update-golden after verifying the new behavior.');
    process.exit(1);
  }

  const expected = fs.readFileSync(GOLDEN_FILE);
  if (actual.length !== expected.length) {
    console.error(
      `FAIL: byte count mismatch — actual ${actual.length}, expected ${expected.length}`);
    process.exit(1);
  }

  const diffIdx = firstDiff(actual, expected);
  if (diffIdx >= 0) {
    const frameIdx  = Math.floor(diffIdx / 77);
    const offsetInFrame = diffIdx % 77;
    console.error(
      `FAIL: byte mismatch at index ${diffIdx} ` +
      `(frame ${frameIdx}, byte ${offsetInFrame} in frame)`);
    console.error(
      `  expected: 0x${expected[diffIdx].toString(16).padStart(2, '0')} ` +
      `(${String.fromCharCode(expected[diffIdx])})`);
    console.error(
      `  actual:   0x${actual[diffIdx].toString(16).padStart(2, '0')} ` +
      `(${String.fromCharCode(actual[diffIdx])})`);
    // Print an aligned context window around the mismatch within the frame.
    const fStart = frameIdx * 77;
    const winStart = Math.max(fStart, fStart + offsetInFrame - 8);
    const winEnd   = Math.min(fStart + 77, fStart + offsetInFrame + 8);
    const e = expected.slice(winStart, winEnd).toString('latin1');
    const a = actual  .slice(winStart, winEnd).toString('latin1');
    console.error(`  context (printable, ±8 bytes within frame):`);
    console.error(`    expected: "${e}"`);
    console.error(`    actual:   "${a}"`);
    console.error('');
    console.error(`Re-run with --update-golden to accept the new bytes,`);
    console.error(`or fix the engine to restore the prior wire output.`);
    process.exit(1);
  }
  console.log(`  PASS: ${actual.length} bytes match golden exactly`);
  console.log('\nAll wire-completeness assertions passed.');
  // tone_calc / tone_calc_muted smoke checks live in
  // tools/web/test/wasm-smoke.mjs — the canonical home for new WASM
  // exports. Keeping them there keeps this file focused on its named
  // purpose and avoids a redundant second WASM module load per run.
}

main().catch(err => {
  console.error(err);
  process.exit(1);
});
