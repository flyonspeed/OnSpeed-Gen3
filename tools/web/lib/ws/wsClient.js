// WebSocket client for OnSpeed live data.
//
// Connection lifecycle: 3 s staleness fallback, retry-storm protection
// on close, blob-frame skipping (the DataServer broadcasts JSON text
// frames; the blob-skip guard is defensive against any future binary
// frame producer).  Maps the firmware's JSON schema to a record shape
// the page components consume.

import { useEffect, useState } from '../../../../packages/ui-core/vendor/preact-standalone.js';

const STALE_RECONNECT_MS = 3000;
const AGE_TICK_MS = 500;
const AOA_NA_SENTINEL = -20;

// Resolve the WebSocket URI.  In firmware, the page is served at
// http://192.168.0.1/<page>, so a same-origin ws:// URL on port 81 is
// what we want.  In the dev-server proxy mode, the static page is
// served from localhost but the WS still goes to the device, so the
// dev-server injects a `<meta name="onspeed-ws" content="ws://...">`
// tag the page can read.  Falls back to `ws://<host>:81` for firmware.
function resolveWsUri() {
  if (typeof document !== 'undefined') {
    const meta = document.querySelector('meta[name="onspeed-ws"]');
    if (meta && meta.content) return meta.content;
  }
  if (typeof location !== 'undefined') {
    const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
    const host = location.hostname || '192.168.0.1';
    return `${proto}//${host}:81`;
  }
  return 'ws://192.168.0.1:81';
}

// parseInt(undefined, 10) -> NaN, which silently corrupts color/threshold
// math downstream.  Default to 0 so a partial JSON frame degrades to
// "low AOA" rather than a frozen indexer.
function parseIntOr0(v) {
  const n = parseInt(v, 10);
  return Number.isFinite(n) ? n : 0;
}

// Same as parseIntOr0 but for fields that carry sub-percent fidelity
// (percentLift since v4.23).  Falls back to 0 for missing/non-numeric.
function parseFloatOr0(v) {
  const n = parseFloat(v);
  return Number.isFinite(n) ? n : 0;
}

// Map a raw firmware JSON frame to the record shape the page components
// expect.  Exported so tests can build records without touching the
// network.
export function frameToRecord(o) {
  return {
    aoaDeg:        o.AOA,
    // AOA is "valid" when the producer ships a numeric value above the
    // -20 threshold.  The producer emits JSON null when air data is
    // invalid (matches IAS / percentLift gating since #431 / #455);
    // the typeof === 'number' guard correctly rejects null.  The > -20
    // threshold is belt-and-suspenders against any future drift that
    // re-introduces a numeric "no data" sentinel.
    aoaIsValid:    typeof o.AOA === 'number' && o.AOA > AOA_NA_SENTINEL,
    derivedAoaDeg: parseFloat(o.DerivedAOA),
    pitchDeg:      o.Pitch,
    rollDeg:       o.Roll,
    verticalG:     o.verticalGLoad,
    lateralG:      o.lateralGLoad,
    pitchRate:     parseFloat(o.PitchRate),
    iasKt:         o.IAS,
    paltFt:        o.PAlt,
    oatC:          o.OAT,
    vsiFpm:        o.vsiFpm,
    flightPathDeg: o.flightPath,
    decelRate:     parseFloat(o.DecelRate) || 0,
    percentLift:        parseFloatOr0(o.percentLift),
    tonesOnPctLift:     parseIntOr0(o.tonesOnPctLift),
    onSpeedFastPctLift: parseIntOr0(o.onSpeedFastPctLift),
    onSpeedSlowPctLift: parseIntOr0(o.onSpeedSlowPctLift),
    stallWarnPctLift:   parseIntOr0(o.stallWarnPctLift),
    pipPctLift:         parseIntOr0(o.pipPctLift),
    flapsDeg:    o.flapsPos,
    flapsMinDeg: (o.flapsMinDeg !== undefined) ? o.flapsMinDeg : 0,
    flapsMaxDeg: (o.flapsMaxDeg !== undefined) ? o.flapsMaxDeg : 33,
    gOnsetRate:  parseFloat(o.gOnsetRate) || 0,
    dataMark:    o.dataMark || 0,
    // Pass the raw JSON through too, so pages that need fields the
    // canonical record shape doesn't expose can read them directly.
    raw: o,
  };
}

// Imperative API.  Returns `{disconnect}`.  Pages that need the
// connection lifecycle outside a Preact component can call this
// directly; the `useWebSocket` hook below wraps it for component use.
export function connect({ onRecord, onStatus, onAge, uri = null }) {
  const wsUri = uri || resolveWsUri();
  let socket = null;
  let connecting = false;
  let closed = false;
  let lastUpdate = Date.now();

  const setStatus = (msg) => onStatus && onStatus(msg);

  function reconnect() {
    if (closed) return;
    setStatus('CONNECTING...');
    connecting = true;
    setTimeout(open, 300);
  }

  function open() {
    if (closed) return;
    connecting = false;
    if (socket && socket.readyState !== WebSocket.CLOSED && socket.readyState !== WebSocket.CLOSING) {
      socket.onclose = null;
      socket.close();
    }
    socket = new WebSocket(wsUri);
    socket.onopen = () => setStatus('CONNECTED');
    socket.onclose = () => {
      if (closed) return;
      setStatus('Reconnecting...');
      if (!connecting) setTimeout(open, 10);
    };
    socket.onmessage = handleMessage;
    socket.onerror = (evt) => console.log('WebSocket error:', evt);
  }

  function handleMessage(evt) {
    connecting = false;
    if (typeof evt.data !== 'string') return;
    try {
      const o = JSON.parse(evt.data);
      lastUpdate = Date.now();
      if (onRecord) onRecord(frameToRecord(o));
    } catch (e) {
      console.log('JSON parse error:', e.name, e.message);
    }
  }

  function tickAge() {
    const ageSec = (Date.now() - lastUpdate) / 1000;
    if (onAge) onAge(ageSec);
    if (ageSec >= STALE_RECONNECT_MS / 1000 && !connecting) reconnect();
  }

  const ageIntervalId = setInterval(tickAge, AGE_TICK_MS);
  reconnect();
  return {
    disconnect() {
      closed = true;
      clearInterval(ageIntervalId);
      if (socket) { socket.onclose = null; socket.close(); }
    },
  };
}

// Preact hook: subscribes a component to the live record stream.
// Returns `{rec, status, ageSec}`.  The page handles the chrome.
//
// Per the architecture lock (no module-level state, no shared
// singleton): each call mounts its own connection.  Pages should call
// this once near the top of the component tree and pass the record
// down via props.
export function useWebSocket(opts = {}) {
  const [rec, setRec] = useState(null);
  const [status, setStatus] = useState('CONNECTING...');
  const [ageSec, setAgeSec] = useState(0);

  useEffect(() => {
    const handle = connect({
      onRecord: setRec,
      onStatus: setStatus,
      onAge: setAgeSec,
      uri: opts.uri,
    });
    return handle.disconnect;
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [opts.uri]);

  return { rec, status, ageSec };
}
