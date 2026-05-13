// clipContains — pure helper for the "Contains: Mark NN, Mark NN" line
// shown in an expanded clip row.
//
// A clip is shaped { startMs, endMs }; a mark is shaped
// { logTimeMs, value, label } per dataMarks.js. The relationship is
// computed on read, never stored — rename or delete a mark and every
// clip's contains-list re-derives for free. See PLAN_FLIGHT_JOURNAL.md
// ("What this rules out") for the rationale.
//
// The range is half-open on the upper end so a clip that ends EXACTLY
// at a mark's logTimeMs does not double-count the mark when an
// adjacent clip starts at the same moment (the "Clip → next" button
// produces this pattern). Pilots reading the inline list see each
// mark assigned to one clip, never two.

export function marksWithinClip(clip, marks) {
  if (!clip || !Array.isArray(marks)) return [];
  const { startMs, endMs } = clip;
  if (!Number.isFinite(startMs) || !Number.isFinite(endMs)) return [];
  if (endMs <= startMs) return [];
  const out = [];
  for (const m of marks) {
    if (!m || !Number.isFinite(m.logTimeMs)) continue;
    if (m.logTimeMs >= startMs && m.logTimeMs < endMs) out.push(m);
  }
  return out;
}
