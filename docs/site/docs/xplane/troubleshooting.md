# Troubleshooting

Common issues with the X-Plane plugin. Work through the relevant
section in order; X-Plane's `Log.txt` (in the sim root) is the
canonical source of truth — search it for `FlyOnSpeed:`.

## Plugin doesn't appear in Plugin Admin

X-Plane records plugin load failures in `Log.txt`. Open it and
search for `AOA-Tone-FlyOnSpeed`. Common causes:

- **Wrong directory layout.** The `.xpl` must sit inside a per-arch
  subdirectory (`mac_x64`, `lin_x64`, `win_x64`), not directly in
  `AOA-Tone-FlyOnSpeed/`. See [Install](install.md) for the layout.
- **Architecture mismatch.** A `mac_x64` `.xpl` won't load on Linux.
  Pull the matching per-platform asset from the release page.
- **X-Plane version too old.** The plugin requires X-Plane 12.4.0 or
  newer (XPLM 4.3.0 SDK). Older sims fail to resolve the plugin's
  imported symbols.

## No sound

In rough order of likelihood:

1. **IAS below the mute threshold.** The plugin mutes audio under
   `iMuteAudioUnderIAS` knots (default 25). Sitting on the runway
   produces silence. Take off, or set the threshold to `0` to
   disable the gate.
2. **`audioEnabled` is off.** Open **Plugins → Fly On Speed → Show**
   and check the **Sound: On / Sound: Off** button at the top of the
   window. Click to toggle.
3. **AOA below LDmax.** The plugin is silent below `fLDMAXAOA` by
   design. Slow down past LDmax or, on the ground, lower the
   threshold temporarily to test.
4. **Master volume at zero.** Check `iMasterVolumePct` in the audio
   control window.
5. **X-Plane sound output muted.** Check **X-Plane → Settings → Sound**
   to confirm master sound is unmuted and routed where you expect.
6. **OpenAL init failed.** `Log.txt` records OpenAL device-open
   failures with `FlyOnSpeed: alcOpenDevice failed`. On Linux, install
   `libopenal1`: `sudo apt-get install libopenal1`. On macOS the
   system framework is always present.
7. **Sim is paused or aircraft has crashed.** The plugin gates audio
   on `sim/time/paused` and `sim/flightmodel2/misc/has_crashed` —
   both produce silence intentionally.

## Indexer is gray / blank

The indexer renders X-Plane datarefs through the M5 firmware. A
gray window usually means no fresh data:

- **AOA dataref unbound.** `Log.txt` reports
  `FlyOnSpeed: alpha dataref not found` if `sim/flightmodel/position/alpha`
  isn't resolvable. This shouldn't happen on stock X-Plane 12 — if
  it does, an aircraft model is overriding core datarefs in a way
  that breaks them.
- **Sim paused.** The indexer doesn't refresh while paused; that's
  intentional, matching the firmware's "no fresh frames"
  behavior.
- **Indexer init failed.** `Log.txt` reports
  `FlyOnSpeed: Indexer Init failed`. Most common cause: SDL2 not
  found at runtime on a build that wasn't statically linked.

## M5 not detected

See the [Tethering page](m5-tethered.md#troubleshooting) for the
full rundown. Quick checks:

- Click **Plugins → Fly On Speed → Serial output → Refresh ports**
  after plugging in the M5.
- Confirm the M5 is enumerating at the OS level (`ls /dev/cu.*` on
  macOS, `dmesg | tail` on Linux, Device Manager on Windows).
- Confirm the M5 is running `OnSpeed-M5-Display` firmware, not
  stock M5 demo software.

## Settings not persisting

The plugin writes the per-aircraft `.prf` on every successful Save
and on `XPluginStop`. If your edits don't survive a sim restart:

- **Save was rejected.** Check the audio control window's status
  line for a validation error. Any field marked `!! ` failed
  validation; nothing was applied or written.
- **No aircraft loaded yet.** `XPLMGetNthAircraftModel` returns an
  empty string until X-Plane brings the user aircraft up. The
  plugin defers `OnAircraftLoaded` to the first flight-loop tick,
  but if the sim crashes before that tick fires, no `.prf` write
  happens. Restart X-Plane and let it reach the runway before
  closing.
- **`Output/preferences/` not writable.** Confirm the directory
  exists and isn't read-only. `Log.txt` records
  `FlyOnSpeed: SaveSettings: could not open <path> for writing`.

You can edit the `.prf` file directly with any text editor while
X-Plane is closed. The next launch picks up the new values for
that aircraft.

## Tones sound wrong for my aircraft

The compiled-in defaults are RV-class generic. Edit the four AOA
setpoints in the audio control window to match your airframe.

If you have calibrated setpoints from a real OnSpeed installation,
remember the convention difference: the firmware stores **body
angle**, the plugin reads X-Plane's **wing AOA**. Apply the
wing-incidence offset before pasting numbers across. See
[How OnSpeed Measures AOA](../calibration/how-aoa-works.md) for the
relationship.

Auto-derivation from X-Plane's per-aircraft datarefs is tracked in
[#392](https://github.com/flyonspeed/OnSpeed-Gen3/issues/392).
