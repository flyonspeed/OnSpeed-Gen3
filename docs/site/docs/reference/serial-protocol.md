# Display Serial Protocol

The OnSpeed firmware emits a serial data stream from its display UART intended for an external panel display (the M5Stack secondary display, the LiveView web page's underlying data source uses the WebSocket — not this stream — see [Display serial vs LiveView](#display-serial-vs-liveview-different-protocols) below). Two formats are selectable via the `SERIALOUTFORMAT` configuration field:

- **`ONSPEED`** — the native `#1` framing covered in detail below. Used by the [M5Stack secondary display](../installation/external-display.md), the [m5-replay bench tool](https://github.com/flyonspeed/OnSpeed-Gen3/tree/master/tools/m5-replay), and any third-party panel display reading OnSpeed's full data set.
- **`G3X`** — a Garmin-G3X-compatible subset (`=11` framing) for feeding an EFIS that wants to display OnSpeed AOA without parsing the native format.

This page is the canonical wire-format reference. The source of truth in code is [`software/Libraries/onspeed_core/src/proto/DisplaySerial.h`](https://github.com/flyonspeed/OnSpeed-Gen3/blob/master/software/Libraries/onspeed_core/src/proto/DisplaySerial.h); when the two disagree the header wins and this page is stale — file an issue.

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

Each frame is exactly **94 bytes** of ASCII, terminated by CRLF. Field offsets, widths, and scale factors are fixed — there are no length prefixes, no variable-width fields, and no escapes inside the payload.

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
| 32 | 2 | `percentLift` | `%02u` | ×1 | 0 – 99 | 0 – 99 |
| 34 | 4 | `aoaDeg` | `%+04d` | ×10 | ±99.9° | ±999 |
| 38 | 4 | `vsiFpm10` | `%+04d` | ×1 | ±9 990 fpm | ±999 (already divided by 10) |
| 42 | 3 | `oatC` | `%+03d` | ×1 | ±99 °C | ±99 |
| 45 | 4 | `flightPathDeg` | `%+04d` | ×10 | ±99.9° | ±999 |
| 49 | 3 | `flapsDeg` | `%+03d` | ×1 | ±99° | ±99 |
| 52 | 4 | `stallWarnAoaDeg` | `%+04d` | ×10 | ±99.9° | ±999 |
| 56 | 4 | `onSpeedSlowAoaDeg` | `%+04d` | ×10 | ±99.9° | ±999 |
| 60 | 4 | `onSpeedFastAoaDeg` | `%+04d` | ×10 | ±99.9° | ±999 |
| 64 | 4 | `tonesOnAoaDeg` | `%+04d` | ×10 | ±99.9° (active flap's L/D~MAX~) | ±999 |
| 68 | 4 | `alpha0Deg` | `%+04d` | ×10 | ±99.9° (zero-lift body angle) | ±999 |
| 72 | 4 | `alphaStallDeg` | `%+04d` | ×10 | ±99.9° (stall body angle) | ±999 |
| 76 | 3 | `flapsMinDeg` | `%+03d` | ×1 | ±99° (full retract) | ±99 |
| 79 | 3 | `flapsMaxDeg` | `%+03d` | ×1 | ±99° (full extend) | ±99 |
| 82 | 4 | `gOnsetRate` | `%+04d` | ×100 | ±9.99 g/s | ±999 |
| 86 | 2 | `spinRecoveryCue` | `%+02d` | ×1 | −9 to +9 | −9 to +9 |
| 88 | 2 | `dataMark` | `%02u` | ×1 | 0 – 99 | 0 – 99 |
| 90 | 2 | `checksum` | `%02X` | hex | sum of bytes 0–89, low byte | `00` – `FF` |
| 92 | 1 | terminator | literal | — | CR (`0x0D`) | |
| 93 | 1 | terminator | literal | — | LF (`0x0A`) | |

Sign and width invariants:

- **Signed fields use a leading `+` or `-` sign character** counted in the field width (e.g. `pitchDeg` width 4 = sign + 3 digits).
- **Out-of-range values are clamped, not wrapped.** The producer uses C-style truncation toward zero before clamping, so 99.94° pitch becomes `+999`, −0.05° becomes `-000`.
- **NaN/Inf inputs emit zero** (the producer's `SafeScaledInt` helper).

### Field semantics

Most fields are self-describing. The ones with non-obvious conventions:

- **`lateralG` is negated.** The producer transmits `−AccelLatFilter` (positive wire value = leftward acceleration, matching slip-skid ball direction). The parser stores the wire value as-is — a consumer that wants right-positive must un-negate.
- **`verticalG` is `ceilf(g × 10)`** before the cast to int, not a normal round-to-nearest. This preserves the legacy "always round up to the next 0.1 g" behaviour that the OnSpeed g-limit chime was tuned against.
- **`vsiFpm10` is already divided by 10.** The wire field carries `floor(VSI_fpm / 10)`. Multiply by 10 on receive to get fpm. The cap is ±9 990 fpm.
- **`iasKt`, `aoaDeg`, and `percentLift` go to zero below the audio mute threshold** (`iMuteAudioUnderIAS`). This matches the rule that the audio path stays silent below taxi speed; external displays follow the same gate so they don't show indicator activity while parked.
- **`tonesOnAoaDeg` is the active flap's `fLDMAXAOA`** — the body angle at which the audio L/D~MAX~ chevrons turn on. With negative-`α₀` flap configurations this can itself be negative (e.g. RV-10 full flaps: `fLDMAXAOA = −2.24°`).
- **`alpha0Deg` and `alphaStallDeg` are body angles, not wing AOA.** See the [body-angle convention](../calibration/how-aoa-works.md) note: OnSpeed's AOA is the fuselage-to-relative-wind angle. `alpha0` is the body angle at zero wing lift (often negative because of wing incidence) and serves as the zero-lift floor for percent-lift math. `alphaStall` is the body angle at critical wing AOA. Together they let the consumer reproduce the canonical formula `(aoaDeg − alpha0Deg) / (alphaStallDeg − alpha0Deg)` for the AOA bar's lower band — see [`onspeed_core/aoa/PercentLift.cpp`](https://github.com/flyonspeed/OnSpeed-Gen3/blob/master/software/Libraries/onspeed_core/src/aoa/PercentLift.cpp).
- **`flapsMinDeg` / `flapsMaxDeg` are the configured travel range,** scanned across all entries in `aFlaps`. Useful for a flap-position widget that draws its arc against actual aircraft endpoints rather than hardcoded values.
- **`dataMark` wraps mod 100.** The pilot's data-mark counter increments without bound in the firmware; the wire field carries `counter % 100`.

### Aspirational / not-yet-wired fields

A handful of fields are part of the wire layout today but are either populated with placeholder values by the producer or unused on the consumer side. They occupy their byte offsets so future producers/consumers don't have to bump the protocol again. Treat them as reserved — but don't be surprised if you see real values flowing through them later.

| Field | Status | What's the gap |
| --- | --- | --- |
| `gOnsetRate` (offset 82) | Always `0.0` from the producer today. The M5 already has render code (`if (gOnsetRate != 0.0)` draws a vertical orange tape on the right edge of Primary mode) — the moment the firmware computes a real onset rate and emits it, the existing M5 build picks it up with no display-side work. See [issue #324](https://github.com/flyonspeed/OnSpeed-Gen3/issues/324). | Producer-side computation: a low-pass-filtered `d(verticalG)/dt` from the AHRS accel filter. Not a field-set change. |
| `spinRecoveryCue` (offset 86) | Always `0` from the producer today. Intended as a `−1 / 0 / +1` direction cue (left / none / right) for an upcoming spin-recovery indicator. No consumer renders it yet. | Both ends: producer needs the cue logic; M5 needs a render glyph. |
| `alphaStallDeg` (offset 72) | Producer emits the active-flap value correctly. M5 receives it into the `AlphaStall` global and stores it — but **does not yet recompute PercentLift locally**. Today the M5 displays the producer's pre-computed `percentLift`. The wire value sits ready for a future M5 build to call `ComputePercentLift(AOA, snapshot, iasValid)` instead. See [issue #323](https://github.com/flyonspeed/OnSpeed-Gen3/issues/323). | M5-side cleanup; the firmware path is already correct. |
| `flapsMinDeg` / `flapsMaxDeg` (offset 76, 79) | Producer emits the configured travel range. M5 receives them into `FlapsMinDeg` / `FlapsMaxDeg` globals — but the flap-position widget today still uses hardcoded RV-class detent positions. See [issue #322](https://github.com/flyonspeed/OnSpeed-Gen3/issues/322). | M5-side widget rewrite. Wire is ready. |
| `flapsDeg` (offset 49), `tonesOnAoaDeg` (offset 64) | Both correct today, but **discretely snapped** to the active detent's calibrated values. While a flap is mid-deployment between two configured detents the wire reports the active-detent value rather than an interpolated value, so the M5's L/D~MAX~ pip and flap-position triangle jump at detent boundaries. The display-side geometry is already capable of rendering interpolated values smoothly (PR #320 wire fields make it possible); the producer just hasn't started interpolating yet. See [issue #321](https://github.com/flyonspeed/OnSpeed-Gen3/issues/321) for the firmware-side `SnapshotForDisplay()` change. | Producer-side `SnapshotForDisplay()` that interpolates between adjacent detents in band-fraction space. |

The pattern in all four cases is the same: the wire format ships ahead of full producer + consumer plumbing so that the next bump doesn't have to be a wire-format bump. Anything new added to the M5 firmware that wants `α_stall` or full flap travel can read them off the existing wire.

### Checksum

Two uppercase ASCII hex digits at offset 90, computed over bytes 0–89 inclusive:

```
checksum = sum(payload[0..89]) & 0xFF
```

The reference implementation lives in `onspeed_core/util/Crc.h` (`util::Checksum8`). Lowercase hex is rejected by the parser.

### Worked example

Inputs:

- pitch +5.0°, roll +0.0°, IAS 100.0 kt, P~alt~ 2 500 ft
- yaw rate 0.0°/s, lateralG 0.0, verticalG +1.0
- percentLift 50, AOA +4.2°, VSI 0 fpm, OAT 15 °C, flightPath 0.0°, flaps 16°
- stallWarn 7.17°, onSpeedSlow 3.88°, onSpeedFast 2.44°, L/D~MAX~ 1.11°
- alpha₀ −6.22°, alphaStall 9.57°, flapsMin 0°, flapsMax 33°
- gOnsetRate 0.0, spinCue 0, dataMark 0

Encoded payload (bytes 0–89):

```
#1+050+00001000+02500+0000+00+1050+042+000+15+000+16+071+038+024+011-062+095+00+33+000+000
```

CRC byte = `(sum of those 90 bytes) & 0xFF` = `0xCA`. Full frame on the wire (94 bytes):

```
#1+050+00001000+02500+0000+00+1050+042+000+15+000+16+071+038+024+011-062+095+00+33+000+000CA\r\n
```

Note the truncation in `iasKt` (`100.0 × 10 = 1000`, fits as `1000`), the `+071` for stallWarn (`7.17 × 10` truncates to `71`), and the `+050` for pitch (`5.0 × 10 = 50`, padded to width 4 with sign).

## Parsing recommendations

The `onspeed_core` library ships a reference parser at [`proto/DisplaySerial.h`](https://github.com/flyonspeed/OnSpeed-Gen3/blob/master/software/Libraries/onspeed_core/src/proto/DisplaySerial.h) that runs natively (no Arduino dependency). Two entry points:

- `ParseDisplayFrame(const uint8_t* buf, size_t len)` — one-shot. Hand it a 94-byte buffer; receive an `optional<DisplayFrame>`. Fails closed on bad magic, bad CRC, or any field that fails to parse.
- `DisplayFrameAccumulator::Inject(uint8_t byte)` — byte-stream. Feed it whatever the UART hands you; it returns a parsed frame on the byte that completes a valid frame, or `nullopt` otherwise. Internally it resets to start-of-frame on any `#`, drops frames that don't end with LF, and clears its buffer between frames. The same struct is used by the M5 firmware and is exercised by the native test suite.

If you implement your own parser, the failure modes worth handling are the ones the reference parser handles:

- **Garbage before the next `#1`.** Real wire output is clean, but bench-replay tools, hot reconnect, and partial-frame recovery all produce situations where your parser starts mid-stream. Treat any byte before the first `#` as ignorable.
- **Mid-frame `#`.** A stray `#` at any offset must restart the frame from byte 0. Without this, a transient line glitch can desynchronise the parser indefinitely.
- **Bad CRC.** Drop and resync; do not let bad data through "because it's only one frame."
- **Frame doesn't end with LF.** Treat as out-of-sync and reset the accumulator.

At 20 Hz, a robust parser should re-sync within one frame (50 ms) of any disturbance.

## Producer/consumer alignment

The `#1` format is a **hard versioned protocol**: there is no length prefix, no field-presence bitmap, and no fallback path. A producer and a consumer must agree on the frame size byte-for-byte or no frames will parse. Specifically:

- A consumer expecting 94 bytes will silently drop 80-byte frames (the previous version) — the LF terminator never arrives where it's expected.
- A consumer expecting 80 bytes will desynchronise on 94-byte frames — by the time the 80th byte arrives, the new frame's `flapsMaxDeg` is sitting where CRC should be.

**When updating the protocol, flash both ends in lockstep.** The OnSpeed firmware and the M5 (or third-party display) firmware released for a given version are paired — do not mix major versions.

## Change log

| OnSpeed version | Frame size | Change |
| --- | ---: | --- |
| ≤ 4.18 | 80 bytes | Original layout — pitch through dataMark, no per-flap aerodynamic anchors. |
| 4.19+ | 94 bytes | Added `alpha0Deg` (offset 68), `alphaStallDeg` (72), `flapsMinDeg` (76), `flapsMaxDeg` (79). Required by the M5 indexer and the LiveView AOA bar to render the L/D~MAX~ pip at its calibrated position per active flap (without `alpha0`, any flap configuration with negative L/D~MAX~ body angle clamped the pip to display bottom). See [PR #320](https://github.com/flyonspeed/OnSpeed-Gen3/pull/320) for the rationale and the M5/LiveView geometry update that consumes the new fields. |

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

## Display serial vs LiveView — different protocols

The OnSpeed web LiveView page does **not** consume the display serial stream documented above. It receives a JSON payload over a WebSocket on port 81, broadcast by `DataServer.cpp`. The two paths share underlying engineering values (attitude, AOA, the active flap's setpoints, alpha₀) but the encodings, cadences, target audiences, and even the set of transmitted fields are different. Don't write a parser that assumes one is the other.

| Aspect | Display serial (`#1`) | LiveView WebSocket |
| --- | --- | --- |
| Transport | UART 115200 8N1, one-way | WebSocket port 81, bidirectional |
| Encoding | Fixed-offset ASCII, byte-summed CRC, CRLF-terminated | JSON over WebSocket text frames |
| Cadence | 20 Hz (every 50 ms) | 20 Hz (every 50 ms) — gated on ≥ 1 connected client; both paths are driven by `kDisplaySerialPeriodMs` |
| Frame size | 94 bytes, fixed | Variable (typically ~430 bytes) |
| Audience | Panel display, third-party EFIS | Browser running LiveView |
| Adding a field | Hard protocol change — both ends must flash together | Soft change — old browsers ignore unknown JSON keys |
| `alpha₀` | **yes** (`alpha0Deg`, offset 68) | **yes** (`Alpha0`) |
| `alphaStall` | yes (`alphaStallDeg`, offset 72) | **no** — derived if needed |
| `flapsMin` / `flapsMax` | yes | no |
| `flapIndex` (which detent is active) | no — consumer infers from `flapsDeg` | yes |
| `kalmanVSI`, `coeffP`, `pitchRate`, `decelRate`, `derivedAOA` | no | yes |
| `gOnsetRate`, `spinRecoveryCue`, `dataMark` | yes (reserved / 0–99) | only `dataMark` |

The asymmetry is by design: the display serial path has to commit to a fixed wire layout for embedded parsers, so it ships every field a calibrated panel display might need. The WebSocket path can evolve fields without breaking browsers, so it carries the slightly different set the LiveView UI happens to want.

If a future tool wants both paths to agree exactly — e.g. so a CSV log replay drives both a panel and a browser — the right move is to compute the JSON from the parsed `#1` frame, not to feed both consumers from independent producers.
