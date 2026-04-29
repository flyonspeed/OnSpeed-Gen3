import { buildFrame } from '../lib/frameBuilder.js';

const exampleRecord = {
  pitchDeg: 5.0, rollDeg: 0.0, iasKt: 100.0, paltFt: 2500,
  turnRateDps: 0.0, lateralG: 0.0, verticalG: 1.0, percentLift: 50,
  vsiFpm: 0, oatC: 70, flightPathDeg: 0, flapsDeg: 33,
  tonesOnPctLift: 33, onSpeedFastPctLift: 51, onSpeedSlowPctLift: 64,
  stallWarnPctLift: 80, flapsMinDeg: 0, flapsMaxDeg: 33,
  gOnsetRate: 0, spinCue: 0, dataMark: 0, pipPctLift: 33,
};

export function run(assert) {
  const buf = buildFrame(exampleRecord);
  assert.equal(buf.length, 76, 'frame length 76');
  // First two bytes are '#' '1'.
  assert.equal(buf[0], 0x23, "byte 0 is '#'");
  assert.equal(buf[1], 0x31, "byte 1 is '1'");
  // Last two bytes CRLF.
  assert.equal(buf[74], 0x0D, 'byte 74 is CR');
  assert.equal(buf[75], 0x0A, 'byte 75 is LF');
  // CRC at offsets 72-73 should be valid hex.
  const crcStr = String.fromCharCode(buf[72], buf[73]);
  assert.truthy(/^[0-9A-F]{2}$/.test(crcStr), `crc is hex: "${crcStr}"`);
  // Recompute crc and verify match.
  let crc = 0;
  for (let i = 0; i < 72; i++) crc += buf[i];
  crc &= 0xFF;
  assert.equal(crc, parseInt(crcStr, 16), 'CRC matches recomputed');
}
