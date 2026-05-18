# Air-data hardening: SAT correction + per-channel validity + wire v4.24

**Date:** 2026-05-18
**Status:** Approved, ready for implementation plan
**Branch:** `feat/airdata-hardening`
**Worktree:** `~/code/onspeed-worktrees/airdata-hardening/`

## Motivation

Three audits (`afs-audit`, `g3x-audit`, `dynon-audit`) found that no
shipping commercial EFIS we examined applies a ram-rise correction to
OAT before computing TAS. Dynon's air-data pipeline runs raw TAT
through the density math; Garmin's G3X audit didn't reach the air-data
state vector but the OAT field on the `=11` wire is uncorrected
degrees Celsius; AFS reuses the Dynon binary wholesale.

OnSpeed inherits the same gap. `Ahrs::updateTas_` accepts an in-band
OAT (`oatInBand`, |T| < 100°C) and feeds it straight into the density
correction. The correction grows with M² and with altitude (TAS/IAS
grows as 1/√σ and a(SAT)/a₀ grows as √(T₀/SAT)). At N720AK 8000 ft
cruise (147 KIAS, +5°C TAT, K=0.75) SAT is 2.75°C cooler than TAT
and density-alt shifts ~330 ft. At Lancair FL250 (200 KIAS, −25°C
TAT) SAT is 8.84°C cooler and density-alt shifts ~1060 ft — large
enough to materially miscalibrate TAS.

Independently, the firmware has three different conventions for
signaling "this field is invalid" — `9999` for IAS, `0` for
percent-lift (ambiguous with calibrated zero), clamped-to-real-looking
numbers for OAT (no signal at all). When OAT silently goes bad, TAS
silently falls back to a crude `1 + Palt/1000 × 0.02` divisor; the
downstream consumer can't tell.

This PR closes both gaps in one breaking wire change.

## Scope

1. **`onspeed::types::AirDataValid`** — single `uint32_t` flags type,
   one bit per channel. Producers set; consumers check.
2. **`onspeed::sensors::CorrectSat()`** — pure helper applying the
   ram-rise correction `SAT_K = TAT_K / (1 + K·0.2·M²)` where `M` is
   the true Mach number, recovered by Newton-iterating from `IAS` and
   `paltFt` via the local σ and `a(T_static)`.
3. **`fOatRecoveryFactor`** config parameter, default 0.75, range
   [0.0, 1.0]. K=0 disables the correction (escape hatch).
4. **Wire-format v4.23 → v4.24** breaking change, bundles three things:
   - Wire-version field (issue #402's design).
   - Validity bitmap field (4 ASCII hex chars = low 16 bits of
     `AirDataValid`).
   - CRC-8 checksum (poly 0x07) replaces sum-mod-256.
5. **Log columns:** `oatRecoveryAppliedC` (delta TAT − SAT for
   post-flight validation), `airDataValidFlags` (hex-encoded uint32).
6. **JS replay updates:** presentation-filter skip-on-invalid;
   MP4 burn-in dashes-on-invalid.

## Out of scope

- **Probe thermal-lag correction (τ)** — deferred. Captured as
  follow-up; needs in-flight OAT logs from a climb profile to validate
  before designing.
- **SAT exposed on wire or log as its own field** — not exposed. Pilot
  still sees raw TAT (matches every other EFIS in the panel); TAS
  internally uses SAT.
- **Per-source K (different for internal probe vs each EFIS)** —
  rejected. Single global K=0.75 default covers the common case; users
  with shielded probes set 1.0; users with EFIS-pre-corrected OAT set
  K=0 globally. Documented in `configuration/advanced.md`.

## Architecture

### Type: `AirDataValid`

```cpp
// software/Libraries/onspeed_core/src/types/AirDataValid.h
namespace onspeed::types {

struct AirDataValid {
    uint32_t bits = 0;

    enum Bit : uint32_t {
        kOatRaw            = 1u <<  0,  // raw TAT, passed FilterOat
        kOatSat            = 1u <<  1,  // ram-rise corrected SAT
        kIas               = 1u <<  2,  // post iasAlive
        kPalt              = 1u <<  3,
        kTas               = 1u <<  4,  // needs kOatSat + kIas + kPalt
        kDensityAlt        = 1u <<  5,  // needs kOatSat + kPalt
        kDerivedAoa        = 1u <<  6,  // needs kTas + kVsi
        kVsi               = 1u <<  7,
        kPitch             = 1u <<  8,
        kRoll              = 1u <<  9,
        kPercentLift       = 1u << 10,
        kFlapsPos          = 1u << 11,
        // bits 12..14 reserved
        kFrameSelfConsistent = 1u << 15,  // pre-allocated; producer impl follows
        // bits 16..31 reserved
    };

    constexpr bool has(Bit b) const { return (bits & b) != 0; }
    constexpr void set(Bit b)       { bits |= b; }
    constexpr void clear(Bit b)     { bits &= ~static_cast<uint32_t>(b); }
};

}
```

### Helper: `CorrectSat`

```cpp
// software/Libraries/onspeed_core/src/sensors/SatCorrect.h
namespace onspeed::sensors {

// Static air temperature (°C) from indicated TAT and IAS-proxied Mach.
//
//   M_proxy = IAS_mps / a0,   a0 = 340.294 m/s (ISA sea-level)
//   SAT_K   = TAT_K / (1 + K * 0.2 * M_proxy²)
//   SAT_C   = SAT_K - 273.15
//
// K is the probe recovery factor:
//   - K = 0.0      disables correction (returns TAT unchanged)
//   - K = 0.75     bare/exposed thermistor (typical GA install)
//   - K = 1.0      ideal TAT probe (Kiel/shielded)
//
// Mach is computed from TAS/a(SAT), where TAS = IAS / sqrt(σ) and
// σ depends on paltFt and SAT.  The helper Newton-iterates two
// passes — empirically one suffices to bring relative SAT error
// below 1e-5 across the subsonic envelope.
//
// Returns std::nullopt if any input is non-finite, out of range, or
// the corrected SAT_K would be non-positive.
std::optional<float> CorrectSat(float tatCelsius,
                                float iasKt,
                                float paltFt,
                                float recoveryFactorK);

}
```

### Producer-side rules

| Producer | Sets/clears which bit |
|---|---|
| `OatConvert::FilterOat` (existing) | `kOatRaw` per existing validation |
| `SatCorrect::CorrectSat` (new) | `kOatSat` iff `kOatRaw`+`kIas`+`kPalt` set AND inputs in physical range |
| `IasAlive` (existing) | `kIas` |
| `PressureConvert::StaticMbarToPaltFt` (existing) | `kPalt` iff result >0 |
| `Ahrs::updateTas_` (existing, modified) | `kTas` iff `kOatSat`+`kIas`+`kPalt` AND `tas_>0` finite |
| `DensityAltitudeFt` (existing, modified) | `kDensityAlt` iff `kOatSat`+`kPalt` |
| EFIS parsers (existing, modified) | Per-channel bits from `kEfisFieldAbsent` checks |

### Consumer-side rules

| Consumer | Behavior on invalid |
|---|---|
| Display wire IAS field | Existing `9999` sentinel; bit cleared in new validFlags field |
| Display wire OAT field | Emit `0`, clear `kOatRaw` bit (fixes "real -99°C indistinguishable from missing") |
| Display wire percentLift | Emit `0`, clear `kPercentLift` bit (fixes calibrated-zero ambiguity) |
| Log CSV | Existing empty-cell convention; new `airDataValidFlags` column carries the hex word |
| Tone calculator | If `kDerivedAoa` clear, gate on `kIas` only; if `kIas` clear, mute |
| JSON liveview | `null` for fields whose bit is clear |
| MP4 burn-in (replay) | Dashes-on-invalid in HUD overlay |
| Presentation filters (replay) | Dynon-style filter→skip on cleared bit, not filter→update with stale value |

## Ram-rise math

### The formula

```
TAS  = IAS / sqrt(σ(paltFt, SAT))
a    = sqrt(γ · R · SAT_K),   γ = 1.4,  R = 287.058 J/(kg·K)
M    = TAS / a
SAT_K_new = TAT_K / (1 + K · 0.2 · M²)
```

SAT enters σ and a(SAT) on both sides of the equation.  The helper
Newton-iterates: seed SAT = TAT, recompute TAS → M → SAT.  Two
iterations produce relative SAT error below 1e-5 across the
subsonic envelope (M < 0.8); one is empirically sufficient but two
is forgiving for extreme inputs.  The K=0 fast path short-circuits
to the TAT identity without iterating.

### Worked examples

**N720AK at 8000 ft cruise, 147 KIAS, +5°C TAT, K=0.75:**

```
seed: SAT_K = 278.15 (= TAT_K)
iter 1: TAS = 168.7 kt → M = 0.258 → SAT_K = 275.40
iter 2: TAS unchanged → SAT_K = 275.40 (converged)
SAT = +2.25°C  (2.75°C cooler than TAT)
```

**Lancair IV-P at FL250, 200 KIAS, -25°C TAT, K=0.75:**

```
seed: SAT_K = 248.15 (= TAT_K)
iter 1: TAS = 295.7 kt → M = 0.496 → SAT_K = 239.31
iter 2: TAS unchanged → SAT_K = 239.31 (converged)
SAT = -33.84°C  (8.84°C cooler than TAT)
density-alt shift: ~1060 ft cooler than panel TAT suggests
TAS shift: ~1.6%
```

The IAS-as-Mach proxy that an earlier draft of this spec used (M_proxy = IAS/a0, ignoring altitude) under-corrected the Lancair case to -28.34°C (only 3.34°C cooler than TAT).  The true-Mach calculation here is materially larger at altitude because TAS/IAS grows with `1/sqrt(σ)` and a(SAT)/a0 grows with `sqrt(T0/SAT)`; both amplify M_true over the sea-level proxy.

### Integration in `Ahrs::updateTas_`

Pseudocode for the modified block (was Ahrs.cpp:158–198):

```cpp
// existing OAT selection
if (in.useEfisOat)        { fOatC = in.efisOatCelsius; haveOat = oatInBand(fOatC); }
if (!haveOat && useInt)   { fOatC = in.sensors.oatCelsius; haveOat = oatInBand(fOatC); }

float fSatC = fOatC;  // fall back to raw TAT if correction fails
if (haveOat) {
    auto satC = onspeed::sensors::CorrectSat(fOatC, in.sensors.iasKt,
                                             cfg_.oatRecoveryFactor);
    if (satC.has_value()) {
        fSatC = *satC;
        outputs_.valid.set(AirDataValid::kOatSat);
    }
    // else: kOatSat stays clear; downstream knows TAS is degraded
}

// existing density-correction math, but using fSatC instead of fOatC
// (lines 172-198 substitute fSatC for fOatC throughout)
```

The fallback chain stays in place — if `CorrectSat` returns nullopt
(IAS not yet valid, TAT out of range), `fSatC = fOatC` preserves the
existing legacy behavior. Downstream consumers see `kOatSat` clear and
treat TAS as a soft-degrade. Pilot doesn't lose TAS, just loses the
"trustworthy" bit on it.

## Wire format v4.24

### Layout (83 bytes)

```
Offset  Width  Field               Format    Notes
------  -----  ------------------  --------  ---------------------------------
 0       2    magic               literal   "#1"
 2       2    wireVersion         %02u      v4.24 → "24"
 4       4    pitchDeg            %+04d     ×10
 8       5    rollDeg             %+05d     ×10
13       4    iasKt               %04u      ×10 (9999 sentinel preserved)
17       6    paltFt              %+06d
23       5    turnRateDps         %+05d     ×10
28       3    lateralG            %+03d     ×100
31       3    verticalG           %+03d     ×10
34       3    percentLift         %03u      ×10
37       4    vsiFpm10            %+04d
41       3    oatC                %+03d     raw TAT; bit drives display
44       4    flightPathDeg       %+04d     ×10
48       3    flapsDeg            %+03d
51       2    tonesOnPctLift      %02u
53       2    onSpeedFastPctLift  %02u
55       2    onSpeedSlowPctLift  %02u
57       2    stallWarnPctLift    %02u
59       3    flapsMinDeg         %+03d
62       3    flapsMaxDeg         %+03d
65       4    gOnsetRate          %+04d     ×100
69       2    spinRecoveryCue     %+02d
71       2    dataMark            %02u
73       2    pipPctLift          %02u
75       4    validFlags          %04X      low 16 bits of AirDataValid
79       2    checksum            ASCII hex CRC-8 of bytes 0..78, poly 0x07
81       2    terminator          CR LF
```

Net: +6 bytes vs v4.23 (2 wireVersion + 4 validFlags). Checksum width
unchanged (algorithm changes from sum-mod-256 to CRC-8 poly 0x07).

### Version dispatch

The M5 reader peeks byte 4 of the frame:
- Digit (`0`–`9`) → v4.24 layout, dispatch on `wireVersion` field.
- Sign character (`+`, `-`) → v4.23 layout, use legacy parser.

Documented as a one-time version-detection invariant. v4.25+ has the
wireVersion field always present at offset 2 for clean dispatch.

### v4.23 fallback parser in M5

Kept for one release. Logs a `wire_legacy_v423` counter. Removed in
v4.25 along with the deprecated `DisplayBuildInputs::iasValid` bool.

### CRC-8 specifics

Poly 0x07, init 0x00, no XOR-out, no reflection. Same algorithm as
SMBus. 256-entry precomputed lookup table (256 bytes ROM). One xor +
table-lookup per byte; ~5 µs to checksum a 79-byte payload on
ESP32-S3.

### Ripple list (full toolchain)

| Layer | File(s) | Change |
|---|---|---|
| C++ producer | `onspeed_core/src/proto/DisplaySerial.{h,cpp}` | Schema v4.24; takes `AirDataValid` param; CRC-8 |
| C++ M5 decoder | `software/OnSpeed-M5-Display/src/SerialRead.cpp` | Version-dispatch parser; v4.23 fallback; CRC-8 |
| C++ bench decoder | `tools/m5-replay/parse_frame.cpp` | Auto via shared `kDisplayFrameSizeBytes`; add validity display |
| JS replay encoder | `docs/site/docs/data-and-logs/replay/lib/replay/buildWireFrames.js` | Auto via C++ wasm rebuild |
| Python encoder | `tools/onspeed_py/frame.py` | Hand-maintained; bump constants; CRC-8; new `validity` field. **High drift risk** — add parity test |
| Python tests | `tools/m5-replay/test_replay.py`, `tools/onspeed_py/tests/` | Update fixture lengths; new parity test |
| Bench README | `tools/m5-replay/README.md` | "77 bytes" → "83 bytes (v4.24)" |
| Spec doc | `docs/site/docs/reference/serial-protocol.md` | Full v4.24 with version-detect + CRC-8 reference |
| Dev server | `tools/web/dev-server/server.mjs` | Should be transparent (serves PROGMEM); confirm via Playwright |
| Replay docs | `docs/site/docs/data-and-logs/replay/getting-started.md` | Version-compat note |
| Replay filter | `docs/site/docs/data-and-logs/replay/lib/replay/presentationFilter.js` | Skip-on-invalid pattern (Dynon `filter->skip()` lifted to JS) |
| MP4 export | `docs/site/docs/data-and-logs/replay/lib/replay/mp4Export.js` | Dashes-on-invalid in burn-in HUD |

### Why not other audit-suggested wire changes

| Considered | Decision | Reason |
|---|---|---|
| Underscore sentinels (Garmin convention) in addition to bits | Skip | Pick one mechanism. We picked bits. |
| Frame-type identifier in magic (`!1`/`!11`/`=11` style) | Skip | YAGNI; we have one frame type |
| Reserved bytes for future fields | Skip | Reserved fields rot. wireVersion is the right forward-compat |
| Binary framing | Skip | Whole toolchain is ASCII; rewriting it has no payoff |
| Frame counter | Skip | M5 doesn't reorder; StaleOverlay handles miss-detection |
| Timestamp field | Skip | No synced clock between Gen3 and M5 |

## Configuration

### New parameter: `fOatRecoveryFactor`

| Layer | Change |
|---|---|
| `ConfigDefaults.h` | `static constexpr float kfOatRecoveryFactor = 0.75f;` |
| `Config.h` `SuConfig` | `float fOatRecoveryFactor;` |
| `Config.cpp::SetDefaults` | Init from `kfOatRecoveryFactor` |
| `ConfigXmlParse.cpp`/`ConfigXmlEmit.cpp` | `<OatRecoveryFactor>` element; clamp [0,1]; out-of-range falls back to default with `g_ErrorLogger.warn()` |
| `ConfigDefaults.json`, `ConfigJson.{cpp,h}` | `"oatRecoveryFactor"` key; same validation |
| `ConfigWebServer.cpp` | AHRS section; slider 0.0–1.0 step 0.05; help text: "Bare probe ≈ 0.75 (default). Shielded TAT ≈ 1.0. Set to 0 to disable." |
| `Ahrs::AhrsConfig` | New `float oatRecoveryFactor` field; sketch's config-save wires through existing `Ahrs::Reconfigure` |

### Live reconfigurability

`Ahrs::Reconfigure` already supports mid-flight K changes (per
existing config-save flow at Ahrs.cpp:134). K takes effect on next
`Step()`. No special wiring.

### Documentation

| File | Section |
|---|---|
| `docs/site/docs/configuration/advanced.md` | New "OAT recovery factor" section; TAT vs SAT; K=0 escape hatch; "your EFIS probably already corrects" note |
| `docs/site/docs/reference/glossary.md` | Add `Recovery Factor (K)`, `SAT`, `TAT`, `Ram Rise` |
| `docs/site/docs/calibration/how-aoa-works.md` | One-paragraph note on the SAT correction |

## Tests

### New C++ test suites

```
test/test_air_data_valid/      ~12 tests
  - Bit constants unique, fit in low 16 of uint32
  - has/set/clear round-trip
  - Default-constructed has bits == 0
  - OR semantics

test/test_sat_correct/         ~20 tests
  - K=0 identity for full TAT range
  - K=0.75 worked examples match section 3
  - Monotonic in IAS at fixed TAT
  - Monotonic in K at fixed IAS/TAT
  - NaN/Inf → nullopt
  - IAS ≤ 0 → nullopt
  - TAT outside [-100, +100] → nullopt
  - K outside [0, 1] → nullopt
  - SAT_K ≤ 0 → nullopt
  - Mach-proxy accuracy at 200 KIAS within 1% of textbook
  - Sea-level edge case
```

### Existing C++ tests extended

```
test/test_pressure_convert/    DensityAltitudeFt: corrected SAT vs raw OAT
                               produces expected 200–400 ft delta
test/test_ahrs/                updateTas_ uses SAT when valid;
                               falls back to TAT on nullopt;
                               kTas bit set/cleared correctly;
                               K=0 pin-test: identical output vs legacy
test/test_display_serial/      v4.24 frame builds to 83 bytes;
                               all bit permutations of AirDataValid;
                               CRC-8 matches reference table;
                               v4.23 fallback parser accepts old fixtures;
                               wireVersion field round-trips;
                               byte-equivalence extended to v4.24
test/test_oat_select/          EFIS OAT consumed → ram-rise still applies
                               under user's global K (documented behavior)
```

### Python parity test (closes the drift risk)

```
tools/onspeed_py/tests/test_v424_byte_parity.py
  - C++-produced golden v4.24 frame fixture (regenerated under
    tools/regression/fixtures/v424_golden.bin)
  - Python decodes the fixture, re-encodes, asserts field-for-field
    AND byte-for-byte equality with the C++ golden
  - CI job regenerates fixture if onspeed_core/proto changes
```

### Regression harness

`tools/regression/run_snapshot.py`: `host_main.cpp` extends to include
the SAT-correction path. Golden CSV regenerates with the two new
columns (`oatRecoveryAppliedC`, `airDataValidFlags`). The regenerated
golden ships with the PR.

### JS replay tests

```
tools/web/test/wire-validity.mjs   (new)
  - C++ wasm produces v4.24 frame, JS decoder sees same validity bits
  - presentationFilter skips samples where relevant bit is clear
  - mp4Export burn-in renders dashes when validity bit clear

docs/site/tests/replay/m5sim-smoke.mjs   (extended)
  - Replay fixture log; assert M5 sim receives v4.24 frames;
    confirm wireVersion=24 in decoded snapshot
```

### Manual browser verification (mandatory per CLAUDE.md)

```
# Both servers parallel:
node tools/web/dev-server/server.mjs --mock --port 9001
cd docs/site && uv run --with "mkdocs>=1.6,<2" --with mkdocs-material mkdocs serve

# Drive Playwright through:
- http://localhost:9001/indexer      (live indexer)
- http://127.0.0.1:8000/data-and-logs/replay/  (docs-site replay)

# Confirm:
- OAT displays raw TAT (not SAT)
- validFlags decode in dev console
- StaleOverlay behavior unchanged
- Replay: synthetic OAT-loss segment shows HUD dashes
- MP4 export: same segment shows dashes in burn-in
- Empty console except known versions.json 404
```

## Rollout

### The dual-flash event

Same shape as v4.22 → v4.23 (PR #386). Pilots flash Gen3 + M5 from
the same v4.24.0 release tag. Old-Gen3-with-new-M5 = M5 fallback
parser keeps it working at v4.23 semantics (graceful degrade). New-
Gen3-with-old-M5 = M5 sees unrecognized magic, shows existing
"unknown frame" UI (no worse than today).

### Order

1. `feat/airdata-hardening` PR lands. Atomic squash-merge.
2. Release v4.24.0 (Gen3 + M5 binaries from same tag).
3. After one stable release cycle, v4.25 removes the v4.23 fallback
   parser and the deprecated `DisplayBuildInputs::iasValid` bool.

### Rollback

| Failure mode | Rollback |
|---|---|
| User sees TAS/density-alt drift after upgrade | Set `fOatRecoveryFactor = 0` in config; behavior reverts to v4.23 math without a re-flash |
| User sees wire decode errors | Re-flash Gen3 to v4.23 firmware; M5 fallback parser handles it automatically |
| Project-wide rollback needed | `git revert` the squash-merge commit; one atomic operation |

### Memory/perf budget

| Resource | Cost |
|---|---|
| RAM | ~20 bytes (`AirDataValid` × 5 structs) |
| ROM | 256 bytes (CRC-8 table) |
| CPU at 50 Hz TAS update | +2 µs (`CorrectSat`: 1 sqrtf + 1 div) |
| CPU at 20 Hz wire frame | +4 µs (CRC-8 vs sum-mod-256, ~5 µs vs ~1 µs) |

All negligible. Total CPU impact: ~180 µs/sec on the dominant Core 1 task.

## Open questions

None at design time. Two follow-ups recorded for future PRs:

1. **Probe thermal-lag correction** (`fOatProbeTauSec`, default 0).
   Designed off in-flight OAT logs from N720AK climb/descent profiles.
2. **`kFrameSelfConsistent` producer implementation** — the bit is
   pre-allocated in this PR but never cleared. Future PR adds the
   cross-channel sanity layer (Dynon `ADAHRSSensorChecker` lift from
   `dynon-audit/docs/architecture/ONSPEED_TRANSFERABLE.md:99`).

## References

- Compressible-IAS prior PR: #580 (master, 2026-05-18)
- Wire-version field issue: #402
- `afs-audit/docs/ONSPEED_PRIOR_ART.md` (synthesis of all three
  audits, Tier-1 #1 = validity bits, Tier-1.5 #10 = compressible IAS)
- `dynon-audit/docs/DEEP_ANALYSIS.md:556–605` (AHRS_ias / AHRS_eas)
- `dynon-audit/docs/architecture/ONSPEED_TRANSFERABLE.md` (filter
  skip/update pattern, validity-bit pattern)
- `g3x-audit/docs/G3X_DEEP_TECHNICAL.md:402–469` (G3X `=11` wire
  format, underscore sentinels)
- Project memory: `aerodynamics_body_angle.md`, `project_wire_v423.md`
- Eshelby, *Aircraft Performance Theory* ch. 2 (ram-rise / recovery
  factor textbook reference)
