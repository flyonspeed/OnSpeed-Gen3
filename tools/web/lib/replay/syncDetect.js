// Anchor auto-detection in an OnSpeed SD log.
//
// We need ONE clean tick the pilot can also spot in the video, so the
// video and log clocks line up. Two events are clean candidates:
//
//   1. **First crosswind turn** (preferred). The pilot rolls into a
//      ~25-30° bank within seconds of clearing the runway. Both the
//      video shows the wing dropping and the log's `Roll` column has
//      a sharp step from ~0° to ~|25°|. This is what most pilots
//      naturally use to sync — it's an unambiguous, easy-to-spot,
//      mid-flight-volume event.
//
//   2. **First rotation** (fallback). When the bank-detect fails
//      (e.g. straight-out departure with no crosswind turn), walk
//      back to the IAS-at-rotation point using the IAS+VSI heuristic.
//
// Returns a row index into the log, or -1 if neither stage matches.

const CROSSWIND_BANK_DEG = 20;     // first |roll| ≥ this counts as a turn
const CROSSWIND_SUSTAIN_ROWS = 25; // ~ 0.5 s at 50 Hz; keeps brief bumps from triggering

// Detect the first crosswind turn — first row where |roll| stays
// ≥ CROSSWIND_BANK_DEG for at least CROSSWIND_SUSTAIN_ROWS rows.
// Returns the row index of the *first* row of the sustained-bank
// run (the moment the airplane started turning), or -1.
//
// We require IAS to also be alive (≥ 30 kt) before counting bank,
// so a ground-handling roll (e.g. taxi over a bumpy ramp) doesn't
// false-trigger.
export function detectFirstCrosswindTurn(log) {
  const N = log.Length;
  if (N < 50 || !log.Roll || !log.IAS || !log.timeStamp) return -1;

  let sustained = 0;
  let runStart  = -1;
  for (let i = 0; i < N; i++) {
    const r = log.Roll[i];
    const v = log.IAS[i];
    if (Number.isFinite(r) && Number.isFinite(v)
        && v >= 30
        && Math.abs(r) >= CROSSWIND_BANK_DEG) {
      if (sustained === 0) runStart = i;
      sustained++;
      if (sustained >= CROSSWIND_SUSTAIN_ROWS) return runStart;
    } else {
      sustained = 0;
      runStart = -1;
    }
  }
  return -1;
}

// Fallback: detect rotation via IAS+VSI heuristic.
//   1. Find the first row where IAS >= 30 kt (well above pitot deadband).
//   2. From there, scan forward for the first sustained VSI > 200 fpm
//      with VSI staying positive for at least 1 s.
//   3. Walk back to the row where IAS first crossed (liftoff IAS - 10) —
//      that's the rotation point.
export function detectRotation(log) {
  const ts = log.timeStamp;
  const ias = log.IAS;
  const vsi = log.VSI;
  if (!ts || !ias || !vsi) return -1;

  const N = log.Length;
  if (N < 50) return -1;

  let i30 = -1;
  for (let i = 0; i < N; i++) {
    if (Number.isFinite(ias[i]) && ias[i] >= 30) { i30 = i; break; }
  }
  if (i30 < 0) return -1;

  const SUSTAIN_ROWS = 50;
  let sustained = 0;
  let liftoffRow = -1;
  for (let i = i30; i < N; i++) {
    if (Number.isFinite(vsi[i]) && vsi[i] > 200) {
      sustained++;
      if (sustained >= SUSTAIN_ROWS) {
        liftoffRow = i - sustained + 1;
        break;
      }
    } else {
      sustained = 0;
    }
  }
  if (liftoffRow < 0) return -1;

  const rotationIas = (ias[liftoffRow] || 0) - 10;
  let rotationRow = liftoffRow;
  for (let i = liftoffRow; i >= i30; i--) {
    if (Number.isFinite(ias[i]) && ias[i] < rotationIas) {
      rotationRow = i + 1;
      break;
    }
  }

  return rotationRow;
}

// Default detector: try the crosswind turn first (matches what
// pilots naturally use to sync), then fall back to rotation.
// Returns { row, kind } where kind is 'crosswind' | 'rotation' or
// 'none' when no anchor was found.
export function detectTakeoff(log) {
  const cw = detectFirstCrosswindTurn(log);
  if (cw >= 0) return cw;
  return detectRotation(log);
}

export function detectTakeoffWithKind(log) {
  const cw = detectFirstCrosswindTurn(log);
  if (cw >= 0) return { row: cw, kind: 'crosswind' };
  const rot = detectRotation(log);
  if (rot >= 0) return { row: rot, kind: 'rotation' };
  return { row: -1, kind: 'none' };
}

// Build a "downsampled-for-plotting" view of the log. For a 30-min
// flight at 50 Hz that's 90,000 rows — too many to plot directly.
// We bin-down to ~2000 points by taking every (Length / 2000)-th row.
// Returns parallel arrays of {tMs, iasKt, vsiFpm} sized to the
// downsample target.
export function downsampleForPlot(log, targetPoints = 2000) {
  const N = log.Length;
  if (N <= targetPoints) {
    return {
      tMs:    Array.from(log.timeStamp.subarray(0, N)),
      iasKt:  log.IAS ? Array.from(log.IAS.subarray(0, N)) : new Array(N).fill(NaN),
      vsiFpm: log.VSI ? Array.from(log.VSI.subarray(0, N)) : new Array(N).fill(0),
    };
  }
  const step = Math.ceil(N / targetPoints);
  const out = { tMs: [], iasKt: [], vsiFpm: [] };
  for (let i = 0; i < N; i += step) {
    out.tMs.push(log.timeStamp[i]);
    out.iasKt.push(log.IAS  ? log.IAS[i]  : NaN);
    out.vsiFpm.push(log.VSI ? log.VSI[i] : 0);
  }
  return out;
}
