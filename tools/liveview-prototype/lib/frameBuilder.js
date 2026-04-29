// Build OnSpeed `#1` display-serial ASCII frames in JavaScript.
// Mirrors tools/m5-replay/replay.py::Frame.to_bytes().
//
// 76-byte frame: 72-byte ASCII payload + 2-byte ASCII hex CRC + CR + LF.
// Used to feed the wasm-live M5 sim with synthetic data via
// `Module.cwrap('inject_serial_byte', null, ['number'])`.
//
// All numeric formatting matches snprintf in
// software/Libraries/onspeed_core/src/proto/DisplaySerial.cpp.

const PAYLOAD_LEN = 72;
const FRAME_LEN   = 76;

function clampInt(v, lo, hi) {
  v = Math.round(v);
  return Math.max(lo, Math.min(hi, v));
}
function clampUint(v, lo, hi) {
  v = Math.round(v);
  return Math.max(lo, Math.min(hi, v));
}

// Format helpers — all produce fixed-width ASCII matching the printf specs
// in DisplaySerial.cpp.
function intStr(v, width, signed) {
  const s = signed ? (v >= 0 ? '+' + Math.abs(v).toString().padStart(width - 1, '0')
                              : '-' + Math.abs(v).toString().padStart(width - 1, '0'))
                    : v.toString().padStart(width, '0');
  return s;
}

export function buildFrame(f) {
  const payload =
    '#1' +
    intStr(clampInt(f.pitchDeg * 10, -999, 999), 4, true) +
    intStr(clampInt(f.rollDeg  * 10, -9999, 9999), 5, true) +
    intStr(clampUint(f.iasKt   * 10, 0, 9999), 4, false) +
    intStr(clampInt(f.paltFt,        -99999, 99999), 6, true) +
    intStr(clampInt(f.turnRateDps * 10, -9999, 9999), 5, true) +
    intStr(clampInt(f.lateralG    * 100, -99, 99), 3, true) +
    intStr(clampInt(f.verticalG   *  10, -99, 99), 3, true) +
    intStr(clampUint(f.percentLift,      0, 99),    2, false) +
    intStr(clampInt(f.vsiFpm / 10,  -999, 999), 4, true) +
    intStr(clampInt(f.oatC,          -99, 99),  3, true) +
    intStr(clampInt(f.flightPathDeg * 10, -999, 999), 4, true) +
    intStr(clampInt(f.flapsDeg,      -99, 99),  3, true) +
    intStr(clampUint(f.tonesOnPctLift,    0, 99), 2, false) +
    intStr(clampUint(f.onSpeedFastPctLift, 0, 99), 2, false) +
    intStr(clampUint(f.onSpeedSlowPctLift, 0, 99), 2, false) +
    intStr(clampUint(f.stallWarnPctLift,   0, 99), 2, false) +
    intStr(clampInt(f.flapsMinDeg,    -99, 99), 3, true) +
    intStr(clampInt(f.flapsMaxDeg,    -99, 99), 3, true) +
    intStr(clampInt(f.gOnsetRate * 100, -999, 999), 4, true) +
    intStr(clampInt(f.spinCue,         -9, 9),   2, true) +
    intStr(clampUint(f.dataMark,        0, 99), 2, false) +
    intStr(clampUint(f.pipPctLift,      0, 99), 2, false);

  if (payload.length !== PAYLOAD_LEN) {
    throw new Error(`payload length ${payload.length} != ${PAYLOAD_LEN}: "${payload}"`);
  }

  // ASCII-byte sum (& 0xFF) → two-digit uppercase hex.
  let crc = 0;
  for (let i = 0; i < payload.length; i++) crc += payload.charCodeAt(i);
  crc &= 0xFF;
  const crcHex = crc.toString(16).toUpperCase().padStart(2, '0');

  const full = payload + crcHex + '\r\n';
  if (full.length !== FRAME_LEN) {
    throw new Error(`frame length ${full.length} != ${FRAME_LEN}`);
  }
  // Return as Uint8Array (each char is ASCII).
  const out = new Uint8Array(FRAME_LEN);
  for (let i = 0; i < FRAME_LEN; i++) out[i] = full.charCodeAt(i);
  return out;
}
