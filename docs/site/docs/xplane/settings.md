# Per-Aircraft Settings

The plugin stores its tunable state per X-Plane aircraft, so the
RV-10's setpoints don't bleed into the Cessna 172. Settings live in a
plain text `.prf` file and load automatically each time you switch
aircraft.

## File location

Settings file path:

```
<X-Plane root>/Output/preferences/AOA-Tone-FlyOnSpeed-<acf>.prf
```

`<acf>` is the aircraft `.acf` filename with the extension stripped
and any path separators (`/`, `\`, `:`) replaced with underscores.
For example, `Aircraft/Laminar Research/Cessna 172 SP/Cessna_172SP.acf`
becomes `AOA-Tone-FlyOnSpeed-Cessna_172SP.prf`.

Switching aircraft mid-session reloads from the new aircraft's file
on the next `XPLM_MSG_PLANE_LOADED` callback.

## Format

Plain `key = value\n` lines. Unknown keys, malformed lines, and
missing files are silently ignored — defaults stand in. A partial
file is fine; the plugin only writes keys it knows.

```
fLDMAXAOA = 6.000
fONSPEEDFASTAOA = 7.300
fONSPEEDSLOWAOA = 9.600
fSTALLWARNAOA = 12.500
iMuteAudioUnderIAS = 25
iMasterVolumePct = 100
iAoaMedianWindow = 5
iAoaMeanWindow = 10
audioEnabled = 1
serialPortPath = /dev/cu.usbmodem11201
```

## Fields

| Field | Type | Range | Purpose |
|---|---|---|---|
| `fLDMAXAOA` | float (degrees) | 0 – AOA_MAX | Below this AOA, audio is silent. Best-glide / best-climb reference. |
| `fONSPEEDFASTAOA` | float (degrees) | 0 – AOA_MAX | Lower edge of the OnSpeed band. Below it: low-pitch pulsing. |
| `fONSPEEDSLOWAOA` | float (degrees) | 0 – AOA_MAX | Upper edge of the OnSpeed band. Above it: high-pitch pulsing. |
| `fSTALLWARNAOA` | float (degrees) | 0 – AOA_MAX | Stall-warning threshold. Above it: stall buzz at 20 PPS. |
| `iMuteAudioUnderIAS` | int (knots) | 0 – 250 | Audio is silenced below this IAS. `0` disables the gate. Hysteresis: unmute at `iMuteAudioUnderIAS + 5`, re-mute at `iMuteAudioUnderIAS`. |
| `iMasterVolumePct` | int (percent) | 0 – 100 | Master volume scaling, applied before pan splits. |
| `iAoaMedianWindow` | int (samples) | 1 – 100 | Median-despike window size on the AOA dataref. `1` disables. |
| `iAoaMeanWindow` | int (samples) | 1 – 100 | Running-mean window size after the median. `1` disables. |
| `audioEnabled` | int 0/1 | — | Sound on/off toggle. Persists across sim restart. |
| `serialPortPath` | string | OS-dependent | USB-serial port to drive a tethered M5. Empty = no output. |

The four AOA setpoints share the same ordering invariant as the
firmware: `fLDMAXAOA < fONSPEEDFASTAOA < fONSPEEDSLOWAOA < fSTALLWARNAOA`.
Save fails with the same error wording the firmware uses
(`OnSpeedConfig::SuFlaps::SetpointOrderError`) if the order is
violated.

`AOA_MAX` is the universal `onspeed::AOA_MAX_VALUE` from
`onspeed_core/util/OnSpeedTypes.h` — plugin and firmware agree on
what counts as a sane AOA value.

## Editing from the UI

Open **Plugins → Fly On Speed → Show**. The audio control window
exposes the AOA setpoints, IAS mute threshold, master volume, and
smoothing windows as editable text rows; `audioEnabled` is the
**Sound: On / Sound: Off** button. `serialPortPath` is set
separately via **Plugins → Fly On Speed → Serial output → \<port\>**
(see [Tethering a Physical M5](m5-tethered.md)).

Click **Save** to validate, apply, and write to the per-aircraft
`.prf`. Validation runs in two passes:

1. Per-field range and parse check. Fields that fail get an
   in-place `!! ` prefix in the row's text. Invalid fields show
   their failing reason in the status line.
2. Cross-field ordering check on the four AOA setpoints. If the
   ordering is violated, all four setpoint rows are flagged
   together — the validator can't tell which value the user
   mis-typed.

A successful Save clears every `!! ` marker, applies the values
to the live audio engine, rebuilds the AOA smoothers if the
window sizes changed, and writes the `.prf`. Any single
validation failure means none of the live state changes — fix
the marked rows and Save again.

**Restore Defaults** reverts every editable field to the compiled-in
defaults (LDmax 6.0°, OnSpeedFast 7.3°, OnSpeedSlow 9.6°, StallWarn
12.5°, IAS gate 25 kt, volume 100%, median 5, mean 10) without
saving. Click **Save** to commit them; otherwise they're gone on the
next aircraft load.

**Reload Plugins** is an escape hatch in the same window. If OpenAL
gets wedged (no audio with the toggle on, no error in `Log.txt`), or
the indexer texture doesn't update after an aircraft change, this
button reloads the plugin without restarting X-Plane. Equivalent to
**Plugins → Reload All Plugins** but routed through this plugin's
own teardown so settings are saved first.

## Limitations

The four AOA setpoints are a single set, not per-flap. The firmware
stores six setpoints per flap detent and switches sets when the lever
moves. The plugin currently has no flap awareness — the same
`fLDMAXAOA` is used at every flap setting. Per-flap support is
tracked in [#393](https://github.com/flyonspeed/OnSpeed-Gen3/issues/393).

Default setpoints are RV-class. Auto-derivation from X-Plane's
per-aircraft stall-AOA datarefs is tracked in
[#392](https://github.com/flyonspeed/OnSpeed-Gen3/issues/392). Until
then, edit the four fields by hand for any non-RV airframe.
