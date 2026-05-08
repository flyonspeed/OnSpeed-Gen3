// useSmoothedField — sliding-window mean over any stream of records.
// The hook ingests a fresh sample whenever the input reference changes
// (driven by the caller's polling or WebSocket source).  Used by the
// sensor-cal page to settle pitch / roll readings before the pilot
// accepts them: the field shows "pending" while the buffer fills, then
// displays the running mean over the last N samples.  Each new sample
// slides the window by one.
//
//   useSmoothedField(source, accessor, N=40)
//
//     source    — current record (null until first sample).  Polled
//                 endpoint response or WebSocket frame; the hook reads
//                 the latest sample on each render that changes `source`.
//     accessor  — function (record) => number | null.  Return null when
//                 the field is missing or non-numeric so the buffer
//                 doesn't ingest junk; the caller's "pending" state
//                 stays true until N real samples arrive.
//     N         — window size.  Default 40 = 2 seconds at 20 Hz WS rate.
//
//   Returns { value, pending, refresh }:
//     value     — null while pending, otherwise the mean of the last N
//                 buffered samples.
//     pending   — true until the buffer fills the first time; flips
//                 false on the Nth ingested sample and stays false
//                 until refresh() is called.
//     refresh   — () => void.  Clears the buffer back to pending; the
//                 next N samples re-fill it.
//
// Implementation notes:
//
// - The hook owns one ring buffer per call site.  No module-level state
//   so two pages mounting this hook don't cross-talk.
// - We re-ingest only when the record reference changes.  useWebSocket
//   issues a new record per frame (fresh object), so a frame at 20 Hz
//   triggers exactly one ingest.  If a caller passes a stable rec
//   reference, the hook will not re-ingest until rec changes.
// - The buffer is capped at N entries.  At index N we wrap; the head
//   pointer (`writeIdx`) tracks the next write slot, and `count`
//   tracks how many of the N slots are live.
// - Mean is computed lazily on read, scanning at most N entries.  N is
//   small (40) so an O(N) scan per render is fine.
// - `refresh` bumps an internal "epoch" so the effect knows to clear
//   without triggering a re-render of the consuming component beyond
//   the state change refresh already causes.

import { useState, useRef, useEffect } from '../vendor/preact-standalone.js';

// Pure ring-buffer helper exposed for unit testing without dragging
// Preact into the test environment.  The hook below wraps this with
// `useState` / `useRef` / `useEffect`; the algorithm — ring write,
// "pending until full" semantics, mean over `count` slots — is the
// same.
//
// `state` is a plain object with `{ buf, writeIdx, count, N }` (created
// by `makeSmoothState(N)`).  `ingest(state, v)` mutates the buffer
// in place; the caller reads `mean(state)` for the running mean and
// `pending(state)` for the "still filling" flag.

export function makeSmoothState(N) {
  return { buf: new Array(N), writeIdx: 0, count: 0, N };
}

export function ingest(state, v) {
  if (v == null || !Number.isFinite(v)) return;
  state.buf[state.writeIdx] = v;
  state.writeIdx = (state.writeIdx + 1) % state.N;
  if (state.count < state.N) state.count += 1;
}

export function mean(state) {
  if (state.count === 0) return null;
  let sum = 0;
  for (let i = 0; i < state.count; i++) sum += state.buf[i];
  return sum / state.count;
}

export function pending(state) {
  return state.count < state.N;
}

export function clear(state) {
  state.buf = new Array(state.N);
  state.writeIdx = 0;
  state.count = 0;
}

export function useSmoothedField(rec, accessor, N = 40) {
  // Ring-buffer state lives in a ref so re-renders don't reallocate.
  // The only state that drives re-render is the tick counter (ensuring
  // the consuming component re-reads the mean) and the pending flag.
  const stateRef    = useRef(makeSmoothState(N));
  const [, setTick] = useState(0);
  const [isPending, setIsPending] = useState(true);
  const lastRecRef  = useRef(null);

  const refresh = () => {
    clear(stateRef.current);
    setIsPending(true);
    setTick(t => t + 1);
  };

  useEffect(() => {
    // Skip if the record reference is unchanged — useWebSocket emits a
    // new object per frame, so reference equality means "no new frame
    // to ingest" (e.g. immediately after a refresh()).
    if (rec === lastRecRef.current) return;
    lastRecRef.current = rec;

    if (rec == null) return;
    const v = accessor(rec);
    ingest(stateRef.current, v);
    if (!pending(stateRef.current)) setIsPending(false);
    setTick(t => t + 1);
  }, [rec]);

  // Don't expose a partial mean while still pending — the page wants a
  // crisp "pending → ready" transition for the pilot.  After refresh,
  // isPending flips back to true and value re-reads as null even
  // though count > 0 hasn't been re-established yet.
  const value = isPending ? null : mean(stateRef.current);

  return { value, pending: isPending, refresh };
}
