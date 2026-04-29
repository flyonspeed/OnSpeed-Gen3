// Firmware-side WebSocket client.
//
// Lifted (and trimmed) from the legacy /live page at
// software/OnSpeed-Gen3-ESP32/Web/html_liveview.h. The legacy file
// also did SVG manipulation in the same onMessage handler; here we
// keep ONLY the connection lifecycle + JSON parser, and emit a
// `record` to subscribers. Mode-specific rendering happens in the
// per-mode files exactly as the prototype does it.
//
// Connection url is hardcoded to ws://192.168.0.1:81 to match the
// firmware's WebSocket server. If the OnSpeed device's IP ever
// changes (it shouldn't — it's the captive-portal AP), this is the
// one place to update.

const WS_URI = 'ws://192.168.0.1:81';

// 3-second staleness fallback. If no JSON message arrives in 3s we
// re-attempt the connection, matching the legacy /live behavior.
const STALE_RECONNECT_MS = 3000;

// Age display refresh cadence.
const AGE_TICK_MS = 500;

// Sentinel: AOA = -100 means "no data yet" — render fields as N/A.
const AOA_NA_SENTINEL = -20;

export function connect({ onRecord, onStatus, onAge }) {
  let socket = null;
  let connecting = false;
  let lastUpdate = Date.now();

  function setStatus(msg) {
    if (onStatus) onStatus(msg);
  }

  function reconnect() {
    setStatus('CONNECTING...');
    connecting = true;
    setTimeout(open, 300);
  }

  function open() {
    // The init→setTimeout(open, 300) debounce has fired. Clear the
    // "attempt is pending" flag so the staleness fallback below can
    // trigger another retry if this attempt hangs in CONNECTING.
    connecting = false;

    // Close any existing socket before opening a new one. Without
    // this, each 3-second reconnect cycle leaks a WebSocket whose
    // onclose handler can still fire later — producing cascading
    // reconnect storms.
    if (socket &&
        socket.readyState !== WebSocket.CLOSED &&
        socket.readyState !== WebSocket.CLOSING) {
      socket.onclose = null;
      socket.close();
    }

    socket = new WebSocket(WS_URI);
    socket.onopen    = () => setStatus('CONNECTED');
    socket.onclose   = () => {
      setStatus('Reconnecting...');
      if (!connecting) setTimeout(open, 10);
    };
    socket.onmessage = handleMessage;
    socket.onerror   = (evt) => {
      // WebSocket error events carry no payload by spec. Log for
      // diagnostics; the onclose handler that fires right after
      // writes the user-visible status.
      console.log('WebSocket error:', evt);
    };
  }

  function handleMessage(evt) {
    connecting = false;

    // The DataServer broadcasts JSON text frames AND binary #1
    // display frames on the same socket. Default binaryType is
    // "blob"; JSON.parse(blob) would throw 20×/s into the catch
    // below. Drop non-string frames silently.
    if (typeof evt.data !== 'string') return;

    try {
      const o = JSON.parse(evt.data);
      lastUpdate = Date.now();

      // Map firmware JSON field names to the prototype's record
      // shape. Everything the modes consume goes through this map.
      const rec = {
        // Identity / freshness
        aoaDeg:        o.AOA,
        aoaIsValid:    o.AOA > AOA_NA_SENTINEL,
        derivedAoaDeg: parseFloat(o.DerivedAOA),
        // Inertial / attitude
        pitchDeg:      o.Pitch,
        rollDeg:       o.Roll,
        verticalG:     o.verticalGLoad,
        lateralG:      o.lateralGLoad,
        pitchRate:     parseFloat(o.PitchRate),
        // Air data
        iasKt:         o.IAS,
        paltFt:        o.PAlt,
        oatC:          o.OAT,
        vsiFpm:        o.kalmanVSI,
        flightPathDeg: o.flightPath,
        decelRate:     parseFloat(o.DecelRate) || 0,
        // Energy / lift
        percentLift:        parseInt(o.percentLift, 10),
        tonesOnPctLift:     parseInt(o.tonesOnPctLift, 10),
        onSpeedFastPctLift: parseInt(o.onSpeedFastPctLift, 10),
        onSpeedSlowPctLift: parseInt(o.onSpeedSlowPctLift, 10),
        stallWarnPctLift:   parseInt(o.stallWarnPctLift, 10),
        pipPctLift:         parseInt(o.pipPctLift, 10),
        // Flaps
        flapsDeg:    o.flapsPos,
        flapsMinDeg: (o.flapsMinDeg !== undefined) ? o.flapsMinDeg : 0,
        flapsMaxDeg: (o.flapsMaxDeg !== undefined) ? o.flapsMaxDeg : 33,
        // Right-edge gOnset tape
        gOnsetRate:  parseFloat(o.gOnsetRate) || 0,
        // Misc
        dataMark:    o.dataMark || 0,
        spinCue:     0,
      };

      if (onRecord) onRecord(rec);
    } catch (e) {
      console.log('JSON parse error:', e.name, e.message);
    }
  }

  function tickAge() {
    const ageSec = (Date.now() - lastUpdate) / 1000;
    if (onAge) onAge(ageSec);
    if (ageSec >= STALE_RECONNECT_MS / 1000 && !connecting) {
      reconnect();
    }
  }

  setInterval(tickAge, AGE_TICK_MS);
  reconnect();

  return {
    disconnect() {
      if (socket) {
        socket.onclose = null;
        socket.close();
      }
    },
  };
}
