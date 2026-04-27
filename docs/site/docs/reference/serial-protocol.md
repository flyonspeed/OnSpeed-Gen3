# Display Serial Protocol

The OnSpeed firmware emits a serial data stream from its display UART intended for an external panel display (the M5Stack secondary display, the LiveView web page's underlying data source uses the WebSocket — not this stream — see [Display serial vs LiveView](#display-serial-vs-liveview-the-two-data-paths) below). Two formats are selectable via the `SERIALOUTFORMAT` configuration field:

- **`ONSPEED`** — the native `#1` framing covered in detail below. Used by the [M5Stack secondary display](../installation/external-display.md), the [m5-replay bench tool](https://github.com/flyonspeed/OnSpeed-Gen3/tree/master/tools/m5-replay), and any third-party panel display reading OnSpeed's full data set.
- **`G3X`** — a Garmin-G3X-compatible subset (`=11` framing) for feeding an EFIS that wants to display OnSpeed AOA without parsing the native format.

This page is the canonical wire-format reference. The source of truth in code is [`software/Libraries/onspeed_core/src/proto/DisplaySerial.h`](https://github.com/flyonspeed/OnSpeed-Gen3/blob/master/software/Libraries/onspeed_core/src/proto/DisplaySerial.h); when the two disagree the header wins and this page is stale — file an issue.

## Design intent

The OnSpeed `#1` wire is a **percent-lift contract**, not a body-angle contract. Every AOA-related quantity on the wire is expressed as a percentage of the wing's lift envelope (the honest single-linear normalization `(AOA − α₀) / (α_stall − α₀) × 100`, clamped 0..99). The four band-edge percents (`tonesOnPctLift`, `onSpeedFastPctLift`, `onSpeedSlowPctLift`, `stallWarnPctLift`) are the per-flap setpoints put through the same formula. They vary per flap because the underlying body-angle calibration varies per flap.

The consumer renders entirely in percent space — one mapping function from percent to screen y, with the four anchors as inputs. Body-angle setpoints stay inside the firmware. See [`onspeed_core/aoa/PercentLift.h`](https://github.com/flyonspeed/OnSpeed-Gen3/blob/master/software/Libraries/onspeed_core/src/aoa/PercentLift.h) for the formula and [`how-aoa-works.md`](../calibration/how-aoa-works.md) for the aerodynamic background.

## Physical layer

| Parameter | Value |
| --- | --- |
| Baud rate | 115200 |
| Frame format | 8N1 |
| Levels | TTL or RS-232 (auto-detected by the M5; pin invert depends on which power-board variant feeds the line) |
| Pinout (Gen3) | TX on GPIO 10 (`kDisplayTx` in `HardwareMap.h`); shares R1\_OUT with EFIS RX |
| Frame cadence | 20 Hz nominal (50 ms period); driven by `kDisplaySerialPeriodMs` in `HardwareMap.h` |
| Direction | One-way, OnSpeed → display. The display does not transmit. |

The OnSpeed firmware's `WriteDisplayDataTask` runs at the cadence above and re-aligns to the current tick if it ever runs late — it does not catch up with back-to-back frames. Consumers should measure their own per-frame `dt` rather than assuming exactly 50 ms; bench-replay tools and a slightly-late tick can drift the actual interval into the 40–60 ms band.

## OnSpeed format (`#1` framing)

### Frame structure

Each frame is exactly **74 bytes** of ASCII, terminated by CRLF. Field offsets, widths, and scale factors are fixed — there are no length prefixes, no variable-width fields, and no escapes inside the payload.

| Offset | Width | Field | printf format | Wire scale | Engineering range | Wire range |
| ---: | ---: | --- | --- | ---: | --- | --- |
| 0 | 2 | `magic` | literal | — | `"#1"` | `"#1"` |
| 2 | 4 | `pitchDeg` | `%+04d` | ×10 | ±99.9° | ±999 |
| 6 | 5 | `rollDeg` | `%+05d` | ×10 | ±999.9° | ±9999 |
| 11 | 4 | `iasKt` | `%04u` | ×10 | 0 – 999.9 kt | 0 – 9999 |
| 15 | 6 | `paltFt` | `%+06d` | ×1 | ±99 999 ft | ±99999 |
| 21 | 5 | `turnRateDps` | `%+05d` | ×10 | ±999.9°/s | ±9999 |
| 26 | 3 | `lateralG` | `%+03d` | ×100 | ±0.99 g (negated, see below) | ±99 |
| 29 | 3 | `verticalG` | `%+03d` | ×10 | ±9.9 g (ceiling-rounded) | ±99 |
| 32 | 2 | `percentLift` | `%02u` | ×1 | 0 – 99 (current AOA, envelope fraction) | 0 – 99 |
| 34 | 4 | `vsiFpm10` | `%+04d` | ×1 | ±9 990 fpm | ±999 (already divided by 10) |
| 38 | 3 | `oatC` | `%+03d` | ×1 | ±99 °C | ±99 |
| 41 | 4 | `flightPathDeg` | `%+04d` | ×10 | ±99.9° | ±999 |
| 45 | 3 | `flapsDeg` | `%+03d` | ×1 | ±99° | ±99 |
| 48 | 2 | `tonesOnPctLift` | `%02u` | ×1 | 0 – 99 (L/D~MAX~ percent for active flap) | 0 – 99 |
| 50 | 2 | `onSpeedFastPctLift` | `%02u` | ×1 | 0 – 99 (OnSpeedFast percent for active flap) | 0 – 99 |
| 52 | 2 | `onSpeedSlowPctLift` | `%02u` | ×1 | 0 – 99 (OnSpeedSlow percent for active flap) | 0 – 99 |
| 54 | 2 | `stallWarnPctLift` | `%02u` | ×1 | 0 – 99 (StallWarn percent for active flap) | 0 – 99 |
| 56 | 3 | `flapsMinDeg` | `%+03d` | ×1 | ±99° (full retract) | ±99 |
| 59 | 3 | `flapsMaxDeg` | `%+03d` | ×1 | ±99° (full extend) | ±99 |
| 62 | 4 | `gOnsetRate` | `%+04d` | ×100 | ±9.99 g/s | ±999 |
| 66 | 2 | `spinRecoveryCue` | `%+02d` | ×1 | −9 to +9 | −9 to +9 |
| 68 | 2 | `dataMark` | `%02u` | ×1 | 0 – 99 | 0 – 99 |
| 70 | 2 | `checksum` | `%02X` | hex | sum of bytes 0–69, low byte | `00` – `FF` |
| 72 | 1 | terminator | literal | — | CR (`0x0D`) | |
| 73 | 1 | terminator | literal | — | LF (`0x0A`) | |

Sign and width invariants:

- **Signed fields use a leading `+` or `-` sign character** counted in the field width (e.g. `pitchDeg` width 4 = sign + 3 digits).
- **Out-of-range values are clamped, not wrapped.** The producer uses C-style truncation toward zero before clamping, so 99.94° pitch becomes `+999`, −0.05° becomes `-000`.
- **NaN/Inf inputs emit zero** (the producer's `SafeScaledInt` helper).

### Field semantics

Most fields are self-describing. The ones with non-obvious conventions:

- **`lateralG` is negated.** The producer transmits `−AccelLatFilter` (positive wire value = leftward acceleration, matching slip-skid ball direction). The parser stores the wire value as-is — a consumer that wants right-positive must un-negate.
- **`verticalG` is `ceilf(g × 10)`** before the cast to int, not a normal round-to-nearest. This preserves the legacy "always round up to the next 0.1 g" behaviour that the OnSpeed g-limit chime was tuned against.
- **`vsiFpm10` is already divided by 10.** The wire field carries `floor(VSI_fpm / 10)`. Multiply by 10 on receive to get fpm. The cap is ±9 990 fpm.
- **`percentLift` and the band-edge percents are computed via the canonical [`ComputePercentLift`](https://github.com/flyonspeed/OnSpeed-Gen3/blob/master/software/Libraries/onspeed_core/src/aoa/PercentLift.h)**, the honest single-linear `(AOA − α₀) / (α_stall − α₀) × 100`. Below α₀ reads 0; above α_stall clamps at 99 (saturation, never reads 100).
- **`percentLift` goes to 0 below the audio mute threshold** (`iMuteAudioUnderIAS`). The wire stays silent for the AOA region while the aircraft is parked.
- **`tonesOnPctLift` is the L/D~MAX~ body angle put through `ComputePercentLift`** — the percent at which the audio L/D~MAX~ chevrons turn on. Varies per flap. Used by the consumer to position the L/D~MAX~ pip on the indexer.
- **`flapsMinDeg` / `flapsMaxDeg` are the configured travel range,** scanned across all entries in `aFlaps`. Useful for a flap-position widget that draws its arc against actual aircraft endpoints rather than hardcoded values.
- **`dataMark` wraps mod 100.** The pilot's data-mark counter increments without bound in the firmware; the wire field carries `counter % 100`.

### Aspirational / not-yet-wired fields

Two fields are part of the wire layout today but populated with placeholder values by the producer. They occupy their byte offsets so future producers/consumers don't have to bump the protocol again. Treat them as reserved — but don't be surprised if you see real values flowing through them later.

| Field | Status | What's the gap |
| --- | --- | --- |
| `gOnsetRate` (offset 62) | Always `0.0` from the producer today. The M5 already has render code (`if (gOnsetRate != 0.0)` draws a vertical orange tape on the right edge of Primary mode) — the moment the firmware computes a real onset rate and emits it, the existing M5 build picks it up with no display-side work. See [issue #324](https://github.com/flyonspeed/OnSpeed-Gen3/issues/324). | Producer-side computation: a low-pass-filtered `d(verticalG)/dt` from the AHRS accel filter. Not a field-set change. |
| `spinRecoveryCue` (offset 66) | Always `0` from the producer today. Intended as a `−1 / 0 / +1` direction cue (left / none / right) for an upcoming spin-recovery indicator. No consumer renders it yet. | Both ends: producer needs the cue logic; M5 needs a render glyph. |

### Checksum

Two uppercase ASCII hex digits at offset 70, computed over bytes 0–69 inclusive:

```
checksum = sum(payload[0..69]) & 0xFF
```

The reference implementation lives in `onspeed_core/util/Crc.h` (`util::Checksum8`). Lowercase hex is rejected by the parser.

## Parsing recommendations

The `onspeed_core` library ships a reference parser at [`proto/DisplaySerial.h`](https://github.com/flyonspeed/OnSpeed-Gen3/blob/master/software/Libraries/onspeed_core/src/proto/DisplaySerial.h) that runs natively (no Arduino dependency). Two entry points:

- `ParseDisplayFrame(const uint8_t* buf, size_t len)` — one-shot. Hand it a 74-byte buffer; receive an `optional<DisplayFrame>`. Fails closed on bad magic, bad CRC, or any field that fails to parse.
- `DisplayFrameAccumulator::Inject(uint8_t byte)` — byte-stream. Feed it whatever the UART hands you; it returns a parsed frame on the byte that completes a valid frame, or `nullopt` otherwise. Internally it resets to start-of-frame on any `#`, drops frames that don't end with LF, and clears its buffer between frames. The same struct is used by the M5 firmware and is exercised by the native test suite.

If you implement your own parser, the failure modes worth handling are the ones the reference parser handles:

- **Garbage before the next `#1`.** Real wire output is clean, but bench-replay tools, hot reconnect, and partial-frame recovery all produce situations where your parser starts mid-stream. Treat any byte before the first `#` as ignorable.
- **Mid-frame `#`.** A stray `#` at any offset must restart the frame from byte 0. Without this, a transient line glitch can desynchronise the parser indefinitely.
- **Bad CRC.** Drop and resync; do not let bad data through "because it's only one frame."
- **Frame doesn't end with LF.** Treat as out-of-sync and reset the accumulator.

At 20 Hz, a robust parser should re-sync within one frame (50 ms) of any disturbance.

## Producer/consumer alignment

The `#1` format is a **hard versioned protocol**: there is no length prefix, no field-presence bitmap, and no fallback path. A producer and a consumer must agree on the frame size byte-for-byte or no frames will parse.

**When updating the protocol, flash both ends in lockstep.** The OnSpeed firmware and the M5 (or third-party display) firmware released for a given version are paired — do not mix major versions across a wire-format bump.

## Change log

| OnSpeed version | Frame size | Change |
| --- | ---: | --- |
| ≤ 4.18 | 80 bytes | Original layout — pitch through dataMark, no per-flap aerodynamic anchors. |
| 4.19 | 94 bytes | Briefly carried per-flap body-angle anchors (`alpha0Deg`, `alphaStallDeg`, four AOA setpoints, `aoaDeg`) so consumers could reproduce the percent-lift formula. Superseded by 4.20 before any production release. |
| 4.20+ | 74 bytes | **Wire becomes a percent-lift contract.** The body-angle anchors come off the wire; in their place the producer emits `tonesOnPctLift`, `onSpeedFastPctLift`, `onSpeedSlowPctLift`, `stallWarnPctLift` — each per-flap setpoint put through the canonical `ComputePercentLift`. Consumers render entirely in percent space. The `ComputePercentLift` function itself was simplified to the honest single-linear formula at the same time. See [PR #320](https://github.com/flyonspeed/OnSpeed-Gen3/pull/320) for the rationale. |

## G3X format (`=11` framing)

Selected by setting `SERIALOUTFORMAT=G3X`. This format exists for Garmin-EFIS users who want OnSpeed AOA on their PFD without writing a parser. It carries a strict subset of the data — pitch, roll, IAS, P~alt~, lateralG, verticalG, and percentLift — formatted to match the Garmin G3X attitude-and-AHRS sentence the EFIS already understands.

| Offset | Width | Field | printf format | Wire scale |
| ---: | ---: | --- | --- | ---: |
| 0 | 2 | magic | literal `"=1"` | — |
| 2 | 8 | reserved (zeros) | literal `"00000000"` | — |
| 10 | 4 | `pitchDeg` | `%+04d` | ×10 |
| 14 | 5 | `rollDeg` | `%+05d` | ×10 |
| 19 | 3 | reserved | literal `"___"` | — |
| 22 | 4 | `iasKt` | `%04u` | ×10 |
| 26 | 6 | `paltFt` | `%+06d` | ×1 |
| 32 | 4 | reserved | literal `"____"` | — |
| 36 | 3 | `lateralG` | `%+03d` | ×100 |
| 39 | 3 | `verticalG` | `%+03d` | ×10 |
| 42 | 2 | `percentLift` | `%02u` | ×1 |
| 44 | 10 | reserved | literal `"__________"` | — |
| 54 | 2 | checksum | `%02X` | sum of bytes 0–53 |
| 56 | 2 | terminator | CR LF | — |

Total: 58 bytes per frame. Same 20 Hz cadence, same 115200 8N1 wire. AOA setpoints, derived data, and aerodynamic anchors are not transmitted — Garmin EFISes don't have a place to render them.

## Display serial vs LiveView — the two data paths

The OnSpeed web LiveView page does **not** consume the display serial stream documented above. It receives a JSON payload over a WebSocket on port 81, broadcast by `DataServer.cpp`. The two paths share the same percent-lift contract — `percentLift`, `tonesOnPctLift`, `onSpeedFastPctLift`, `onSpeedSlowPctLift`, `stallWarnPctLift` are computed by the same firmware code and travel byte-for-byte equivalent values on either path. A future shared indexer renderer can run identically off either transport.

The encodings differ:

| Aspect | Display serial (`#1`) | LiveView WebSocket |
| --- | --- | --- |
| Transport | UART 115200 8N1, one-way | WebSocket port 81, bidirectional |
| Encoding | Fixed-offset ASCII, byte-summed CRC, CRLF-terminated | JSON over WebSocket text frames |
| Cadence | 20 Hz (every 50 ms) | 20 Hz — gated on ≥ 1 connected client; both paths driven by `kDisplaySerialPeriodMs` |
| Audience | Panel display, third-party EFIS | Browser running LiveView |
| Adding a field | Hard protocol change — both ends must flash together | Soft change — old browsers ignore unknown JSON keys |
| Body-angle `AOA` (degrees) | not on wire | yes (`AOA`) — used for the numeric corner readout |
| Body-angle `DerivedAOA` (degrees) | not on wire | yes (`DerivedAOA`) — for advanced overlays / debug |
| `kalmanVSI`, `coeffP`, `pitchRate`, `decelRate` | not on wire | yes — LiveView-specific instrumentation |
| `flapIndex` (which detent is active) | not on wire | yes |

The asymmetry is by design: the panel displays render *the indexer*, so the wire ships percent anchors. The LiveView additionally shows numeric body-angle AOA so a pilot can compare it to DerivedAOA — those degrees-units fields stay on the WebSocket but never on the panel-serial wire.
