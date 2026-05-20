# LiveView WebSocket Protocol

The OnSpeed firmware broadcasts real-time flight data over a WebSocket on port 81. The built-in LiveView web page consumes this stream, but the protocol is not LiveView-specific — any third-party client (browser, native app, recording tool) can connect, parse the JSON, and ingest the same data the LiveView renders.

This page is the canonical specification for that wire format.

## Design intent

The WebSocket is the LiveView's data path, paralleling the [display serial protocol](serial-protocol.md) that feeds the M5 secondary display. The two paths share the same `percent-lift` contract — `percentLift`, `tonesOnPctLift`, `onSpeedFastPctLift`, `onSpeedSlowPctLift`, `stallWarnPctLift`, `pipPctLift` are computed by the same firmware code (`onspeed_core::ComputePercentLift` + `ComputeDisplayPctAnchors`). The JSON formats `percentLift` as `%.1f` (rounded to one decimal); the wire encodes `int(pct × 10)` in `%03u` (truncated to one decimal). Consumers reading either transport reconstruct a whole-percent float; the two reconstructions agree to within 0.1% — the wire's tenths-of-a-percent quantization is the resolution limit, and the truncate-vs-round encoding choice at each boundary can produce a sub-tenth difference on the same source AOA. The four band-edge anchors stay integer-percent on both paths. A shared indexer renderer can run identically off either transport at the granularity that matters for visual rendering.

The encodings differ deliberately. The display serial path is a fixed-offset ASCII frame designed for a low-bandwidth UART to a hardware panel display; bandwidth and parsing simplicity matter, and adding a field is a hard protocol change that requires re-flashing both ends. The WebSocket path is JSON over TCP/WebSocket text frames, designed for browser and software consumers; bandwidth is plentiful, parsing is `JSON.parse()`, and adding a field is a soft change because old consumers ignore unknown keys.

The WebSocket carries a few fields the display serial does not: body-angle `AOA` (degrees) and `DerivedAOA` for the LiveView's numeric corner readouts, plus LiveView-specific instrumentation (`kalmanVSI`, `coeffP`, `PitchRate`, `DecelRate`, `flapIndex`). These have no place on a hardware panel render but are useful for browser overlays, debugging consumers, and any future tool that wants to compare body angle to its derived counterparts.

## Physical layer

| Aspect | Value |
| --- | --- |
| Transport | WebSocket text frames over TCP |
| Port | **81** |
| Path | `/` (no sub-path) |
| URL | `ws://192.168.0.1:81` (when connected to the OnSpeed AP) |
| Encoding | UTF-8 JSON in text frames |
| Cadence | 20 Hz (one frame every 50 ms), gated on ≥ 1 connected client; the display-serial wire and the WebSocket share the same 50 ms tick (`kDisplaySerialPeriodMs` in `HardwareMap.h`) so they update in lockstep |
| Direction | Server → client only; client text frames are accepted but currently no-op |
| Authentication | None — same WiFi-AP-only access model as the rest of the LiveView UI |
| Concurrent clients | Multiple clients are broadcast the same payload (no per-client state) |

The OnSpeed acts as a WiFi access point named `OnSpeed` with password `angleofattack` and assigns itself `192.168.0.1` by default. The LiveView UI lives at `http://192.168.0.1/`; the WebSocket is on the same host, port 81. A client running on the same WiFi can connect with any standard WebSocket library — no handshake extensions, no subprotocol, no compression negotiation.

## Message type

The socket broadcasts one message type:

| Type | WebSocket frame | Cadence | Producer | Consumer |
| --- | --- | --- | --- | --- |
| LiveView JSON | text | 20 Hz, gated on ≥ 1 connected client | `DataServer.cpp::UpdateLiveDataJson()` (port-81 broadcast loop) | LiveView, `/indexer`, third-party software consumers |

A consumer reading the JSON:

```js
ws.onmessage = (evt) => {
  if (typeof evt.data !== 'string') return;   // defensive: skip any non-string frame
  const data = JSON.parse(evt.data);
  // ...
};
```

The non-string guard is defensive — no current producer emits binary frames on this socket — but a future producer that does should expect consumers to type-check before parsing.

When no clients are connected, the broadcast loop early-exits and skips the JSON build entirely.

## Frame structure

Each JSON frame is a single object containing all live data fields, sent at 20 Hz. There is no framing layer above WebSocket text — one JSON object per WebSocket text message, and every frame is independent (no incremental / delta encoding).

Example payload (formatted; on the wire it's compact, no whitespace). Values are illustrative — actual ranges and per-flap calibration vary by aircraft:

```json
{
  "AOA": 4.20,
  "Pitch": -2.10,
  "Roll": 3.50,
  "IAS": 87.45,
  "PAlt": 1234.00,
  "verticalGLoad": 1.02,
  "lateralGLoad": -0.04,
  "flapsPos": 0,
  "flapIndex": 0,
  "coeffP": 0.85,
  "dataMark": 12,
  "kalmanVSI": -50.30,
  "flightPath": -0.40,
  "PitchRate": 0.10,
  "DecelRate": -0.15,
  "OAT": 22.50,
  "DerivedAOA": 4.18,
  "percentLift": 34.7,
  "tonesOnPctLift": 18,
  "onSpeedFastPctLift": 32,
  "onSpeedSlowPctLift": 48,
  "stallWarnPctLift": 72,
  "pipPctLift": 18
}
```

Field ordering is stable — the firmware emits keys in the order shown above (the source-of-truth `snprintf` template fixes the order at compile time). Consumers should not rely on this ordering for JSON parsing (real JSON parsers don't care), but tools that snapshot the raw text for diffing can.

Typical compacted frame size: **~390 bytes** in cruise; up to **~460 bytes** in the worst case (large negative floats and 5-digit integers in every field). The firmware allocates a fixed 512-byte buffer; if `snprintf` would overflow, the producer emits the literal `{}` instead of partial-and-invalid JSON. Consumers that see `{}` should treat it as a one-frame skip — every other frame is well-formed.

## Field reference

Every field appears in every frame. Floats are formatted with 2 decimal places (`%.2f`) by default; the live `percentLift` field is `%.1f` (one decimal — matches the wire's tenths resolution) and integers are bare. Numeric values are guarded against `NaN` / `Inf` — any non-finite source value is replaced with a documented fallback (typically 0 or a sentinel) so the JSON is always parseable.

A note on source selection: several attitude/air-data fields read from different sources depending on the calibration-source config:

- **EFIS mode + VN-300** (`CALWIZ_SOURCE = EFIS` and the configured EFIS is VectorNav VN-300): Pitch/Roll come from the VN-300 directly; VSI from VN-300 NED-down velocity; IAS still from OnSpeed pitot.
- **EFIS mode + non-VN-300** (any other supported EFIS): Pitch/Roll/IAS/OAT from the EFIS; VSI is still OnSpeed `KalmanVSI`; flight path derived from EFIS VSI ÷ EFIS TAS.
- **Internal mode** (`CALWIZ_SOURCE` set to anything other than `EFIS`, typically `INTERNAL`): all attitude and air data from OnSpeed sensors and the AHRS algorithm (Madgwick or EKF6 per the `AHRS_ALGORITHM` setting).

The per-field tables below note source variations where they apply.

### Attitude

| Field | Type | Units | Notes |
| --- | --- | --- | --- |
| `Pitch` | float | degrees | Smoothed pitch. Source: VN-300 (`g_EfisSerial.suVN300.Pitch`) in VN-300 mode, EFIS (`g_EfisSerial.suEfis.Pitch`) in non-VN-300 EFIS mode, `g_AHRS.SmoothedPitch` in internal mode. |
| `Roll` | float | degrees | Smoothed roll, same source rules as `Pitch`. |
| `flightPath` | float | degrees | Flight-path angle (positive = climbing). In internal mode, taken directly from `g_AHRS.FlightPath`. In EFIS modes, computed at this call site via `arcsin(VSI / TAS)` — the VSI/TAS sources vary: VN-300 uses VN-300's NED-down velocity over `g_AHRS.fTAS`; non-VN-300 uses the EFIS's VSI and TAS when both are present, falling back to `KalmanVSI / g_AHRS.fTAS`. Falls back to `0` if no usable TAS is available. |
| `PitchRate` | float | deg/s | Body-frame pitch rate, `g_AHRS.gPitch` (filtered gyro). Always sourced from the AHRS regardless of calibration-source mode. |

### Air data

| Field | Type | Units | Notes |
| --- | --- | --- | --- |
| `IAS` | float \| null | knots | Indicated airspeed. From the EFIS (`g_EfisSerial.suEfis.IAS`) **only** in non-VN-300 EFIS mode. VN-300 EFIS mode and internal mode both use OnSpeed pitot-derived `g_Sensors.IAS` — VN-300 itself does not provide IAS. Emits JSON `null` when `bIasAlive` is false (air data invalid). Consumers should check `typeof === 'number'` before using. |
| `PAlt` | float | feet | Pressure altitude. From `g_AHRS.KalmanAlt` (Kalman-filtered, in metres) converted to feet. Always sourced from OnSpeed regardless of calibration-source mode. |
| `kalmanVSI` | float | feet/min | Vertical speed. **Despite the name**, this is `g_AHRS.KalmanVSI` in both internal mode and non-VN-300 EFIS mode; only in VN-300 mode does it become VN-300's `-VelNedDown` (NED-down velocity, sign-inverted to make positive = climb). The non-VN-300 EFIS mode does not use the EFIS's own VSI here. |
| `OAT` | float | °C | Outside air temperature. From the EFIS in any EFIS mode (including VN-300) via `g_EfisSerial.suEfis.OAT`; from `g_Sensors.OatC` if `OATSENSOR = true` in config; otherwise `0.0`. |
| `DecelRate` | float | knots/s | IAS-decel rate, `g_Sensors.fDecelRate` — the raw output of a 15-tap Savitzky-Golay first-derivative filter on smoothed IAS, scaled to kt/s. **Not** additionally EMA-smoothed; consumers smooth per-surface. The M5 hardware decel gauge applies an EMA at α=0.04 (~1.25 s τ at 20 Hz; see `software/OnSpeed-M5-Display/src/SerialRead.cpp`); `/indexer` Mode 3 mirrors the same α=0.04 to match the M5's time-constant during normal continuous operation; `/calwiz`'s decel step exposes a SMOOTH ↔ RESPONSIVE slider (α range 0.02–0.50, default 0.05) for the gauge needle only — recorded samples remain raw. Negative = decelerating. Always from OnSpeed sensors. |

### G-loads

| Field | Type | Units | Notes |
| --- | --- | --- | --- |
| `verticalGLoad` | float | g | Installation-corrected body-vertical acceleration, EMA-smoothed by AHRS::Process (α ≈ 0.06). `g_AHRS.AccelVertFilter.get()`. 1.0 g level, 2.0 g in a 60° bank. Same source the display-serial wire's `verticalG` field uses, so M5 hardware and LiveView agree to within rounding. Note: `GLimitDecision` reads the unsmoothed `AccelVertCorr` for over-G warnings; the smoothing here is purely a presentation choice. |
| `lateralGLoad` | float | g | Installation-corrected body-lateral acceleration, EMA-smoothed (α ≈ 0.06). `g_AHRS.AccelLatFilter.get()`. **Sign**: positive = right — body-frame, the IMU's view. At v4.23 the display-serial wire's `lateralG` field uses the same body-frame convention; both transports agree. Slip-skid ball renderers negate locally at the rendering site (ball lags opposite the airframe's centripetal acceleration). Numeric "Lat G" readouts read the field as-is. Full physics + sign rationale: [`proto/DisplaySerial.h::DisplayBuildInputs::lateralG`](https://github.com/flyonspeed/OnSpeed-Gen3/blob/master/software/Libraries/onspeed_core/src/proto/DisplaySerial.h). |

### AOA & lift

| Field | Type | Units | Notes |
| --- | --- | --- | --- |
| `AOA` | float \| null | degrees | **Body angle**, not wing AOA. The fuselage-to-wind angle. See [How OnSpeed Measures AOA](../calibration/how-aoa-works.md) for the convention. Emits JSON `null` when `bIasAlive` is false (air data invalid). Consumers should check `typeof === 'number'` before using. |
| `DerivedAOA` | float \| null | degrees | Body angle derived from the AHRS (pitch and flight path), `g_AHRS.DerivedAOA`. Useful for comparing pitot-derived AOA against attitude-derived AOA during tuning. Emits JSON `null` when `bIasAlive` is false (air data invalid). Consumers should check `typeof === 'number'` before using. |
| `percentLift` | float \| null | 0.0–99.9 | Honest single-linear envelope fraction of the current body angle, computed by `onspeed_core/aoa/PercentLift::ComputePercentLift`: `(AOA − α₀) / (α_stall − α₀) × 100`, clamped to `[0.0, 99.9]`. Emitted with one decimal of precision (e.g. `34.7`) so the LiveView indexer bar can advance at sub-pixel temporal smoothness off the 20 Hz cadence — same fidelity the M5 wire carries via its `%03u` tenths-of-a-percent field. Consumers expecting an integer can `Math.round` / `int()` at the readout site. Uses the **active-detent** flap calibration (matches what the audio path uses). Emits JSON `null` when `bIasAlive` is false (air data invalid). Consumers should check `typeof === 'number'` before using. |
| `coeffP` | float | dimensionless | Ratiometric pressure coefficient (the "CP3" form in the firmware): `P45 / Pfwd`, where `Pfwd` is the differential pitot pressure and `P45` is the differential AOA pressure from the angled-port probe. Returns `0.0` when `Pfwd ≤ 0` to avoid division-by-zero on the ground. The textbook-form Cp `(P_aoa − P_static) / q` is **not** what's emitted here — the firmware uses the ratiometric form because it stays well-behaved through the AOA-port pressure zero-crossing on Dynon-style probes. Implementation: `onspeed_core/util/OnSpeedTypes.h::pressureCoeff()`. |

### Indexer percent-lift anchors

Five fields driving the LiveView indexer's band edges and L/Dmax pip. The first four are the per-flap setpoints expressed as percent-lift, **snapped to the active detent's** calibrated values; they stay in lockstep with the audio cues that fire at the same calibrated body angles. The fifth (`pipPctLift`) is the L/Dmax pip's screen position, which **interpolates** smoothly with the flap lever instead of snapping. All come from `onspeed_core/aoa/DisplayPctAnchors`.

| Field | Type | Units | Notes |
| --- | --- | --- | --- |
| `tonesOnPctLift` | int | 0–99 | **Snapped** to the active detent. L/Dmax body angle through the percent-lift formula. Below this percent, audio is silent. |
| `onSpeedFastPctLift` | int | 0–99 | **Snapped**. OnSpeedFast threshold — the lower edge of the donut band. |
| `onSpeedSlowPctLift` | int | 0–99 | **Snapped**. OnSpeedSlow threshold — the upper edge of the donut band. |
| `stallWarnPctLift` | int | 0–99 | **Snapped**. StallWarn threshold — the chevron's flash-on point. |
| `pipPctLift` | int | 0–99 | **Interpolated** linearly clean → full-flap across the configured flap range, ignoring intermediate detents. The L/Dmax pip dot's visual position; deliberately separated from `tonesOnPctLift` (which snaps to detents) so the pip can slide smoothly with the lever. See the [indexer spec](indexer-spec.md) for the rationale. |

### Flap state

| Field | Type | Units | Notes |
| --- | --- | --- | --- |
| `flapsPos` | int | degrees | Current flap angle. **Interpolates** across the bracket containing the lever, so the numeric readout slides smoothly during deployment. Falls back to the snapped detent position when the flap calibration is empty. |
| `flapIndex` | int | — | Index of the active flap detent (`g_Flaps.iIndex`). 0-based. The audio path and the four band-edge anchors above all reference this same detent. |
| `flapsMinDeg` | int | degrees | Minimum configured flap travel — the smallest `iDegrees` across `g_Config.aFlaps`. Drives the LiveView flap-circle widget's lower endpoint. Defaults to `0` when the calibration is empty. |
| `flapsMaxDeg` | int | degrees | Maximum configured flap travel. Defaults to `33` when the calibration is empty. |

### Other

| Field | Type | Units | Notes |
| --- | --- | --- | --- |
| `gOnsetRate` | float | g/s | Low-pass-filtered `d(verticalG)/dt`, 250 ms time constant. Same source the display-serial wire's `gOnsetRate` field uses (`g_AHRS.gOnsetRate`, ticked in `AHRS::Process`). Sign follows `verticalGLoad` — positive = G load increasing. |
| `dataMark` | int | unsigned | User-pressable button counter, increments on each press. Used to mark interesting moments in flight logs. The display serial wire applies a `mod 100` wrap to fit its 2-digit field; the WebSocket emits the raw counter without wrapping, so it can grow arbitrarily large during a long session. |

## Sentinels and fallbacks

The producer never emits `nan`, `inf`, or other invalid JSON tokens. Every float passes through `SafeJsonFloat()` which substitutes a documented fallback when the source value is non-finite:

| Field | Fallback when source is invalid | Why |
| --- | --- | --- |
| `AOA`, `DerivedAOA`, `IAS`, `percentLift` | JSON `null` when `bIasAlive=false` | Air-data fields the producer can't honestly emit when the pitot-side validity flag (`bIasAlive`, hysteresis 20 kt rising / 15 kt falling) is false. Consumers `typeof === 'number'` before reading; null falls through to N/A renderings. |
| `Pitch`, `Roll`, `kalmanVSI`, `flightPath`, `verticalGLoad`, `OAT`, `coeffP`, `PitchRate`, `DecelRate`, `lateralGLoad`, `PAlt` | `0.0f` fallback on NaN (still numeric) | 0 is a physically meaningful reading for these fields (level pitch, no roll, no climb, etc.), so the fallback won't be confused with real data the way a numeric AOA fallback would be. |

There is no top-level "validity" flag; the protocol is best-effort 20 Hz. Consumers should detect staleness by tracking the elapsed time since the last frame.

## Consumer recommendations

A minimal browser consumer:

```js
const ws = new WebSocket("ws://192.168.0.1:81");
ws.onmessage = (evt) => {
  // Defensive: skip any non-string frame. No current producer emits
  // binary frames on this socket, but the guard is cheap insurance.
  if (typeof evt.data !== 'string') return;
  const data = JSON.parse(evt.data);
  // Skip the {} truncation marker.
  if (data.AOA === undefined) return;
  // AOA emits null when air data is invalid; check typeof first.
  const aoa = (typeof data.AOA === 'number') ? data.AOA.toFixed(1) + "°" : "N/A";
  console.log(`AOA=${aoa} pct=${data.percentLift}%`);
};
```

A minimal command-line consumer using [`websocat`](https://github.com/vi/websocat):

```bash
websocat ws://192.168.0.1:81
```

This streams compact JSON lines at 20 Hz; pipe through `jq -c '{AOA, percentLift, IAS}'` to reduce.

**Reconnection.** The OnSpeed firmware does not actively notify clients of going-away; if the OnSpeed reboots or the WiFi link drops, the client sees a normal WebSocket close and should retry. The bundled LiveView re-attempts every 3 seconds whenever no message has arrived in the last 3 s; **3 seconds is the recommended staleness threshold** for any consumer.

**Discovery and topology.** The OnSpeed runs as a WiFi access point only — there is no station-mode WebSocket today. The IP `192.168.0.1` is hard-coded by the AP DHCP config and there is no mDNS / zeroconf advertisement. A consumer must connect to the OnSpeed AP first, then dial the literal IP. If the AP IP is ever reconfigured, consumers will need to update their URL.

**Schema versioning.** The JSON has no top-level version field. The intended forward-compat strategy is "ignore unknown keys" — new fields will appear in future firmware versions; consumers must tolerate that. **The project does not currently have a test pinning the JSON schema**, so consumers cannot rely on a guarantee that field types and units never change for existing keys; treat the schema as documentation of current behavior, not a stability contract. The change log at the bottom of this page records breaking changes when they happen, but is dependent on someone remembering to update it.

**Truncation handling.** If `snprintf` would overflow the 512-byte buffer, the producer emits the literal `{}`. A defensive consumer should check that an expected key exists (or do `if (data.AOA === undefined) skipFrame()`) rather than blindly indexing.

**Null on invalid air data.** The `AOA`, `DerivedAOA`, `IAS`, and `percentLift` fields emit JSON `null` when `bIasAlive` is false (the canonical sensor-level air-data validity flag, hysteresis 20 kt rising / 15 kt falling, backed by the pitot deadband). A third-party consumer should always check `typeof data.AOA === 'number'` (or equivalent in its language) before calling `.toFixed()` or doing arithmetic — null fields fall through to N/A renderings rather than producing spikes or NaN propagation.

**Per-field native rates.** A frame is a near-simultaneous snapshot of fields each filtered at their own native rate (gyro at AHRS rate, decel at the Savitzky-Golay window, flap index at 1 Hz, etc.). Two consecutive frames will not show identical values for slow-moving fields like `flapIndex` even when nothing changed — the snapshot is consistent, the underlying filters are not.

**Coordinate consistency with display serial.** Where a field exists in both transports the values are derived from the same source and agree to within rounding. The wire is fixed-width and applies tighter clamps; the WebSocket is unclamped. As of v4.23, sign conventions also match — the wire's `lateralG` is body-frame (positive = right), same as `lateralGLoad` in JSON. Slip-skid ball renderers negate locally at the rendering site on either transport.

## Display serial vs LiveView — the two data paths

| Aspect | Display serial (`#1`) | LiveView WebSocket |
| --- | --- | --- |
| Transport | UART 115200 8N1, one-way | WebSocket port 81, bidirectional (server-broadcast only in practice) |
| Encoding | Fixed-offset ASCII, byte-summed CRC, CRLF-terminated | JSON in text frames |
| Cadence | 20 Hz (every 50 ms) | 20 Hz (every 50 ms), gated on ≥ 1 connected client; both paths share the same 50 ms tick |
| Audience | Panel display, third-party EFIS | Browser running LiveView, third-party software consumers; `/indexer` tablet view |
| Adding a field | Hard protocol change — both ends must flash together | Soft change — old consumers ignore unknown keys |
| Body-angle `AOA` (degrees) | not on wire | yes in JSON (`AOA`) — used for the numeric corner readout |
| Body-angle `DerivedAOA` (degrees) | not on wire | yes in JSON (`DerivedAOA`) — for advanced overlays / debug |
| `kalmanVSI`, `coeffP`, `PitchRate`, `DecelRate` | not on wire | yes in JSON — LiveView-specific instrumentation |
| `flapIndex` (which detent is active) | not on wire | yes in JSON |

The asymmetry is by design: the panel displays render *the indexer*, so the wire ships percent anchors. The LiveView additionally shows numeric body-angle AOA so a pilot can compare it to DerivedAOA — those degrees-units fields stay on the JSON path but never on the `#1` wire.

## Producer alignment

The single source of truth for the wire format is `software/sketch_common/src/web_server/DataServer.cpp::UpdateLiveDataJson()`. Field semantics, units, and computation match the display serial wire wherever the same field exists in both, because the producer reads the same firmware globals and uses the same `onspeed_core` helpers (`ComputePercentLift`, `ComputeDisplayPctAnchors`).

Unlike the display-serial wire, **the WebSocket schema is not currently pinned by a unit test**. The display-serial spec has byte-precise round-trip tests in `test/test_display_serial/`; the JSON has no equivalent. Schema drift is therefore possible across firmware versions if a contributor changes `UpdateLiveDataJson` without updating this page. Filing a "pin the JSON schema" test is on the project roadmap.

## Change log

| Date | Change |
| --- | --- |
| 2026-05-19 | Removed the binary `#1` display-serial mirror broadcast. The mirror was added (2026-04-28) for a WASM `/indexer` consumer that has since been replaced by a Preact/SVG renderer reading JSON only. The cross-task broadcast (DisplaySerial on Core 1 + DataServer on Core 0 sharing the same WSclient array without locking) was racing client-disconnect cleanup, producing NULL-tcp panics under reconnect chaos. Removing the mirror eliminates the race. JSON path is unaffected. |
| 2026-05-05 | `percentLift` field on the wire widens from `%02u` to `%03u` (tenths of a percent). The JSON `percentLift` already carries one decimal of precision, so the JSON path is unchanged. The wire's `lateralG` flips to body-frame (positive = right) to match `lateralGLoad` in JSON; the wire-vs-JSON sign mismatch noted in the prior change-log entry no longer applies. See [PR #386](https://github.com/flyonspeed/OnSpeed-Gen3/pull/386). |
| 2026-04-30 | `verticalGLoad` and `lateralGLoad` are now EMA-smoothed (α ≈ 0.06) on the producer side, matching the source the display-serial wire uses. Previously both fields shipped the raw `AccelVertCorr` / `AccelLatCorr` values, which made the LiveView slip ball and G readouts visibly twitchier than the M5 hardware. `lateralGLoad` sign convention is engineering (positive = right). |
| 2026-04-30 | Added `flapsMinDeg`, `flapsMaxDeg`, `gOnsetRate` to the schema (PR #354's broadcast additions for the indexer's flap-circle widget and gOnset edge tape). |
| 2026-04-28 | Added binary `#1` display-serial mirror frames alongside the existing JSON text frames. Consumers must inspect the WebSocket message type before parsing — see [Two message types](#two-message-types). |
| 2026-04-28 | Initial WebSocket protocol reference page covering the schema in master at the time of writing. |
