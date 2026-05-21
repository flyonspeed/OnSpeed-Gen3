// Anchor auto-detection in an OnSpeed SD log.
//
// We need ONE clean tick the pilot can also spot in the video, so the
// video and log clocks line up. The auto-detect provides a good
// initial guess; the pilot fine-tunes via the nudge buttons or
// Pause/Attach.
//
// Detection target: **first rotation**. VSI-positive after IAS-alive —
// the unambiguous moment the wheels release. Happens within seconds of
// takeoff on every flight. Wide-spread enough to auto-detect reliably;
// close-enough-to-truth that nudging by a few seconds gets it dead-on.
//
// Returns a row index into the log, or -1 if no rotation found.

// Detect rotation via IAS+VSI heuristic.
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
