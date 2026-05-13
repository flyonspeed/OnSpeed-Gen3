// inset-slots-smoke.mjs — verify the dual inset-slot localStorage
// persistence contract. Each slot is null (off) or a mode id (0..4);
// LS round-trips via empty string for null and decimal-string for a
// mode id.
//
// Pure-data test — runs against a mock localStorage; no DOM, no
// Preact. Mirrors the persistence-smoke style.
//
// Run:
//   node docs/site/tests/replay/inset-slots-smoke.mjs

const LEFT_INSET_LS_KEY  = 'replay-inset-left-v1';
const RIGHT_INSET_LS_KEY = 'replay-inset-right-v1';
const VALID_MODE_IDS = [0, 1, 2, 3, 4];

// Mock localStorage so the test runs under Node without a DOM.
const _ls = new Map();
const lsGet = (k) => _ls.has(k) ? _ls.get(k) : null;
const lsSet = (k, v) => { _ls.set(k, String(v)); };

// Mirror of ReplayPage's parseSlot helper. Empty string → null (slot
// off). Decimal mode id → the id (if valid). Anything else → fallback.
function parseSlot(raw, fallback) {
  if (raw === null || raw === undefined) return fallback;
  if (raw === '') return null;
  const n = parseInt(raw, 10);
  return Number.isFinite(n) && VALID_MODE_IDS.includes(n) ? n : fallback;
}

// Mirror of the setLeftInsetMode / setRightInsetMode persistence call:
// null persists as empty string; numbers persist as their decimal
// string.
function persistSlot(key, mode) {
  lsSet(key, mode == null ? '' : String(mode));
}

let passed = 0;
let failed = 0;
function test(name, fn) {
  try { fn(); passed++; console.log('PASS', name); }
  catch (e) { failed++; console.log('FAIL', name, '-', e.message); }
}

function assertEq(actual, expected, msg) {
  if (actual !== expected) {
    throw new Error(`${msg}: expected ${JSON.stringify(expected)}, got ${JSON.stringify(actual)}`);
  }
}

// ---------------------------------------------------------------------
// Defaults
// ---------------------------------------------------------------------
test('defaults: left=null (Off), right=0 (Energy) on a fresh session', () => {
  _ls.clear();
  const left  = parseSlot(lsGet(LEFT_INSET_LS_KEY),  null);
  const right = parseSlot(lsGet(RIGHT_INSET_LS_KEY), 0);
  assertEq(left,  null, 'left slot default');
  assertEq(right, 0,    'right slot default');
});

// ---------------------------------------------------------------------
// Round-trips
// ---------------------------------------------------------------------
test('setting a slot to a mode persists and reads back as that mode', () => {
  _ls.clear();
  persistSlot(LEFT_INSET_LS_KEY, 2);   // Indexer
  const left = parseSlot(lsGet(LEFT_INSET_LS_KEY), null);
  assertEq(left, 2, 'left slot mode 2 round-trip');
  assertEq(lsGet(LEFT_INSET_LS_KEY), '2', 'left LS raw value');
});

test('setting a slot to null persists as empty string and reads back as null', () => {
  _ls.clear();
  persistSlot(RIGHT_INSET_LS_KEY, null);
  const right = parseSlot(lsGet(RIGHT_INSET_LS_KEY), 0);
  assertEq(right, null, 'right slot null round-trip');
  assertEq(lsGet(RIGHT_INSET_LS_KEY), '', 'right LS raw value');
});

test('null slot survives default-pick: fresh empty string preserves null', () => {
  // The default for right is 0 (Energy). If a pilot explicitly set
  // right to null, the empty-string LS entry must take precedence
  // over the default-fallback rather than re-introducing Energy.
  _ls.clear();
  persistSlot(RIGHT_INSET_LS_KEY, null);
  const right = parseSlot(lsGet(RIGHT_INSET_LS_KEY), 0);
  assertEq(right, null, 'persisted null beats default-fallback 0');
});

test('all 5 mode ids round-trip correctly', () => {
  for (const m of VALID_MODE_IDS) {
    _ls.clear();
    persistSlot(LEFT_INSET_LS_KEY, m);
    const v = parseSlot(lsGet(LEFT_INSET_LS_KEY), null);
    assertEq(v, m, `mode ${m} round-trip`);
  }
});

// ---------------------------------------------------------------------
// Robustness
// ---------------------------------------------------------------------
test('garbage values fall back to the supplied default', () => {
  _ls.clear();
  _ls.set(LEFT_INSET_LS_KEY, 'banana');
  assertEq(parseSlot(lsGet(LEFT_INSET_LS_KEY), null), null,
           'garbage → null fallback');
  _ls.set(LEFT_INSET_LS_KEY, 'banana');
  assertEq(parseSlot(lsGet(LEFT_INSET_LS_KEY), 0), 0,
           'garbage → 0 fallback');
});

test('out-of-range mode ids fall back to the supplied default', () => {
  _ls.clear();
  _ls.set(LEFT_INSET_LS_KEY, '99');
  assertEq(parseSlot(lsGet(LEFT_INSET_LS_KEY), null), null,
           'mode 99 → null fallback');
  _ls.set(LEFT_INSET_LS_KEY, '-1');
  assertEq(parseSlot(lsGet(LEFT_INSET_LS_KEY), null), null,
           'mode -1 → null fallback');
});

test('independent slots: changing left does not touch right and vice versa', () => {
  _ls.clear();
  persistSlot(LEFT_INSET_LS_KEY,  3);
  persistSlot(RIGHT_INSET_LS_KEY, 1);
  assertEq(parseSlot(lsGet(LEFT_INSET_LS_KEY),  null), 3, 'left slot');
  assertEq(parseSlot(lsGet(RIGHT_INSET_LS_KEY), null), 1, 'right slot');
  // Flip just the left.
  persistSlot(LEFT_INSET_LS_KEY, null);
  assertEq(parseSlot(lsGet(LEFT_INSET_LS_KEY),  null), null, 'left after flip');
  assertEq(parseSlot(lsGet(RIGHT_INSET_LS_KEY), null), 1,    'right after left flip');
});

console.log(`${passed} passed, ${failed} failed`);
process.exit(failed === 0 ? 0 : 1);
