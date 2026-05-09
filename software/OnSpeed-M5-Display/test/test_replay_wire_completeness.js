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

const { buildDisplayInputs } = require('./wireBridgeForTest.js');

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
// Test config — single flap detent with diverse anchor values so the
// golden's anchor bytes vary independently of percentLift.
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
      // Pick alpha values so percentLift math gives interesting numbers.
      // alpha_0 = -3.0, alpha_stall = 13.0  → span = 16°
      // For aoa = 5° → percentLift = (5-(-3))/16 * 100 = 50.0%
      alpha0:         -3.0,
      alphaStall:     13.0,
      ldmaxAoa:        2.0,    // → ~31% (rounds to 31 on the wire)
      onSpeedFastAoa:  4.0,    // → ~44%
      onSpeedSlowAoa:  6.0,    // → ~56%
      stallWarnAoa:   11.0,    // → ~88%
      kFit:           0.0,
    }
  ],
};

const FLAP_FOR_BUILD = TEST_CONFIG.flaps[0];
const FLAPS_MIN_DEG  = TEST_CONFIG.flaps[0].degrees;
const FLAPS_MAX_DEG  = TEST_CONFIG.flaps[0].degrees;
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
      const inputs = buildDisplayInputs(
        stepResult, FLAP_FOR_BUILD, FLAPS_MIN_DEG, FLAPS_MAX_DEG);
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

  // ---------------------------------------------------------------------------
  // Bonus: tone_calc smoke check (PR 1.5 deliverable)
  // ---------------------------------------------------------------------------
  console.log('\nBonus: tone_calc embind binding smoke check');
  const coreUrl     = require('url').pathToFileURL(CORE_JS).href;
  const CoreFactory = (await import(coreUrl)).default;
  const Core        = await CoreFactory();

  if (typeof Core.tone_calc !== 'function') {
    console.error('FAIL: Core.tone_calc not exported. Rebuild WASM.');
    process.exit(1);
  }
  if (typeof Core.tone_calc_muted !== 'function') {
    console.error('FAIL: Core.tone_calc_muted not exported. Rebuild WASM.');
    process.exit(1);
  }

  // AOA = 8.0 sits between OnSpeedSlow (7.0) and StallWarn (9.0):
  //   the "high tone, pulsed 1.5..6.2 PPS interpolated" region.
  // ToneThresholds order: { fLDMAXAOA, fONSPEEDFASTAOA, fONSPEEDSLOWAOA, fSTALLWARNAOA }
  const r = Core.tone_calc(8.0, 5.0, 6.0, 7.0, 9.0);
  if (r.enTone !== 'High') {
    console.error(`FAIL: tone_calc enTone got "${r.enTone}", expected "High"`);
    process.exit(1);
  }
  if (r.pulseFreq < 1.5 || r.pulseFreq > 6.2) {
    console.error(
      `FAIL: tone_calc pulseFreq ${r.pulseFreq} out of [1.5..6.2] band`);
    process.exit(1);
  }
  console.log(`  PASS tone_calc(8.0, 5,6,7,9) -> ` +
              `{ enTone: '${r.enTone}', pulseFreq: ${r.pulseFreq.toFixed(2)} }`);

  // tone_calc_muted: with stallWarnAoa=10, muteUnderIas=30, IAS=80, AOA=12 —
  // above stall, above mute floor — should fire stall warning at 20 PPS.
  const m = Core.tone_calc_muted(12.0, 80.0, 10.0, 30);
  if (m.enTone !== 'High' || Math.abs(m.pulseFreq - 20.0) > 0.001) {
    console.error(
      `FAIL: tone_calc_muted aoa=12 above stall got ` +
      `{ enTone: '${m.enTone}', pulseFreq: ${m.pulseFreq} }, ` +
      `expected { enTone: 'High', pulseFreq: 20 }`);
    process.exit(1);
  }
  console.log(`  PASS tone_calc_muted(12.0, 80, 10, 30) -> ` +
              `{ enTone: '${m.enTone}', pulseFreq: ${m.pulseFreq.toFixed(2)} }`);

  console.log('\nAll wire-completeness assertions passed.');
}

main().catch(err => {
  console.error(err);
  process.exit(1);
});
