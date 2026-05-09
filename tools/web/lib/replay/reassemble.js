// Reassemble engine output (immediates + tail) into row-aligned results.
//
// Engine semantics (see LogReplayEngine.h):
//   - hasPot=true: every step() returns a result. tail is empty.
//     immediates[i] is the result for row i.
//   - hasPot=false: first synthHalfWindowTicks step()s return null
//     while the buffer fills. After that, immediates[i] holds the
//     result for row (i - lag). flush() returns the trailing
//     synthHalfWindowTicks rows that were still buffered at EOF.
//     If N < synthHalfWindowTicks, all step()s return null and
//     tail.length === N.
//
// Parameters:
//   immediates  - Array of results from step(), in order (may contain
//                 leading nulls on the synth path).
//   tail        - Array from flush() (trailing rows drained at EOF).
//   N           - Total row count (= immediates.length).
//   hasPot      - True when the log has flapsRawAdc data (fast path).
//
// Returns results[0..N-1] where results[i] is the ReplayStepResult for
// input row i.
export function reassembleResults(immediates, tail, N, hasPot) {
    const results = new Array(N).fill(null);
    if (hasPot) {
        // Fast path: every step() returns a result immediately.
        // immediates[i] is the result for row i. Tail is empty.
        for (let i = 0; i < N; i++) results[i] = immediates[i];
    } else {
        // Synth path: the circular buffer introduces a lag.
        // immediates[lag..N-1] → results[0..N-lag-1]
        // tail[0..tail.length-1] → results[N-tail.length..N-1]
        //
        // When N < synthHalfWindowTicks every step() returns null, so
        // firstNonNull is -1. In that case lag = N (all rows are in
        // tail) and the immediates loop below is empty.
        const firstNonNull = immediates.findIndex(r => r !== null);
        const lag = (firstNonNull < 0) ? N : firstNonNull;
        for (let i = lag; i < N; i++) results[i - lag] = immediates[i];
        for (let j = 0; j < tail.length; j++) {
            results[N - tail.length + j] = tail[j];
        }
    }
    return results;
}
