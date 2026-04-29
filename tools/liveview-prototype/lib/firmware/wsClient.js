// Firmware-side WebSocket client.
//
// Connection lifecycle preserved from the legacy /live (3 s staleness
// fallback, retry-storm protection on close, blob-frame skipping).
// Maps the firmware's JSON schema to a record shape the modes consume.

const WS_URI = 'ws://192.168.0.1:81';
const STALE_RECONNECT_MS = 3000;
const AGE_TICK_MS = 500;
const AOA_NA_SENTINEL = -20;

// parseInt(undefined, 10) → NaN, which silently corrupts color/threshold
// math downstream. Default to 0 so a partial JSON frame degrades to
// "low AOA" rather than a frozen indexer.
function parseIntOr0(v) {
  const n = parseInt(v, 10);
  return Number.isFinite(n) ? n : 0;
}

export function connect({ onRecord, onStatus, onAge }) {
  let socket = null;
  let connecting = false;
  let lastUpdate = Date.now();

  const setStatus = (msg) => onStatus && onStatus(msg);

  function reconnect() {
    setStatus('CONNECTING...');
    connecting = true;
    setTimeout(open, 300);
  }

  function open() {
    connecting = false;
    if (socket && socket.readyState !== WebSocket.CLOSED && socket.readyState !== WebSocket.CLOSING) {
      socket.onclose = null;
      socket.close();
    }
    socket = new WebSocket(WS_URI);
    socket.onopen = () => setStatus('CONNECTED');
    socket.onclose = () => {
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
      if (onRecord) onRecord({
        aoaDeg:        o.AOA,
        aoaIsValid:    o.AOA > AOA_NA_SENTINEL,
        derivedAoaDeg: parseFloat(o.DerivedAOA),
        pitchDeg:      o.Pitch,
        rollDeg:       o.Roll,
        verticalG:     o.verticalGLoad,
        lateralG:      o.lateralGLoad,
        pitchRate:     parseFloat(o.PitchRate),
        iasKt:         o.IAS,
        paltFt:        o.PAlt,
        oatC:          o.OAT,
        vsiFpm:        o.kalmanVSI,
        flightPathDeg: o.flightPath,
        decelRate:     parseFloat(o.DecelRate) || 0,
        percentLift:        parseIntOr0(o.percentLift),
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
      });
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
      clearInterval(ageIntervalId);
      if (socket) { socket.onclose = null; socket.close(); }
    },
  };
}
