// wire-validity.mjs — verify that parsed v4.24 frames expose the
// validity bitmap to JS callers.
//
// Builds a v4.24 frame via the wasm core's build_display_frame, then
// round-trips through parse_display_frame and asserts:
//   - the returned object has both `iasIsValid` and `validity`
//   - `validity` low bits match what was encoded
//   - kIas mask in `validity` matches iasIsValid
//
// Run:
//   node --test tools/web/test/wire-validity.mjs

import test from 'node:test';
import assert from 'node:assert/strict';
import { fileURLToPath } from 'url';
import path from 'path';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const wasmJsPath = path.resolve(
    __dirname,
    '../../../software/Libraries/onspeed_core/wasm/dist/onspeed_core.js'
);

// AirDataValid bit constants (mirror types/AirDataValid.h).
const VALID = {
    kOatRaw:       1 << 0,
    kOatSat:       1 << 1,
    kIas:          1 << 2,
    kPalt:         1 << 3,
    kTas:          1 << 4,
    kDensityAlt:   1 << 5,
    kDerivedAoa:   1 << 6,
    kVsi:          1 << 7,
    kPitch:        1 << 8,
    kRoll:         1 << 9,
    kPercentLift:  1 << 10,
    kFlapsPos:     1 << 11,
};

let core = null;
try {
    const mod = await import(wasmJsPath);
    core = await mod.default();
} catch (e) {
    console.warn(`[wire-validity] wasm not available: ${e.message}`);
    console.warn('  Build it first: bash software/Libraries/onspeed_core/wasm/build_wasm.sh');
}

const wasmAvailable = core !== null;

test('parse_display_frame exposes validity bitmap', { skip: !wasmAvailable }, () => {
    // Build a frame with kIas + kPalt + kOatRaw set.
    const bits = VALID.kIas | VALID.kPalt | VALID.kOatRaw;
    const bytes = core.build_display_frame({
        pitchDeg: 0, rollDeg: 0, iasKt: 100,
        validity: bits,
        paltFt: 1000,
    });
    assert.equal(bytes.length, 83, 'v4.24 frame must be 83 bytes');

    const parsed = core.parse_display_frame(bytes);
    assert.ok(parsed !== null, 'parse must succeed');
    assert.equal(typeof parsed.validity, 'number', 'validity is a number');
    assert.equal(parsed.validity & 0xFFFF, bits & 0xFFFF,
        'low 16 bits of validity round-trip');
    assert.equal(parsed.iasIsValid, true,
        'iasIsValid follows kIas bit');
});

test('parse_display_frame reflects cleared kIas', { skip: !wasmAvailable }, () => {
    // kPalt only — kIas cleared; iasIsValid must be false.
    const bits = VALID.kPalt;
    const bytes = core.build_display_frame({
        pitchDeg: 0, rollDeg: 0, iasKt: 100,
        validity: bits,
        paltFt: 2000,
    });
    assert.equal(bytes.length, 83, 'v4.24 frame must be 83 bytes');

    const parsed = core.parse_display_frame(bytes);
    assert.ok(parsed !== null, 'parse must succeed');
    assert.equal(parsed.validity & VALID.kIas, 0, 'kIas bit cleared');
    assert.equal(parsed.iasIsValid, false, 'iasIsValid follows kIas');
});

test('validity field covers independent channel bits', { skip: !wasmAvailable }, () => {
    // All air-data channels set.
    const bits = VALID.kOatRaw | VALID.kOatSat | VALID.kIas | VALID.kPalt |
                 VALID.kTas | VALID.kDensityAlt | VALID.kDerivedAoa | VALID.kVsi |
                 VALID.kPitch | VALID.kRoll | VALID.kPercentLift | VALID.kFlapsPos;
    const bytes = core.build_display_frame({
        pitchDeg: 5, rollDeg: -10, iasKt: 85, paltFt: 3000,
        validity: bits,
    });
    const parsed = core.parse_display_frame(bytes);
    assert.ok(parsed !== null, 'parse must succeed');
    assert.equal((parsed.validity & bits), bits,
        'all set channel bits survive round-trip');
});
