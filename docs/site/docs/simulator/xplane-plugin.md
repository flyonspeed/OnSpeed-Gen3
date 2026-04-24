# X-Plane Plugin

OnSpeed for X-Plane 12 — same audio cues as the panel-mounted Gen3, driven
by X-Plane's flight model. Audio-only for now; visuals are coming
([#221](https://github.com/flyonspeed/OnSpeed-Gen3/issues/221)).

## What it does

While X-Plane is running and the aircraft is above 25 kt IAS, the plugin
plays the OnSpeed tone family in real time:

- **No tone** below LDmax AOA — you have plenty of margin.
- **Low pulse tone** between LDmax and OnSpeed-fast — pulse rate climbs
  as you slow toward OnSpeed.
- **Solid low tone** in the OnSpeed band — the audio "lock" you fly
  approaches against.
- **High pulse tone** above OnSpeed-slow — pulse rate climbs as AOA grows.
- **Fast high pulse (20 pps)** above stall warning AOA — back off now.

The tones come from X-Plane's published AOA dataref, not from any
calibrated airframe model. That's enough for training; it isn't enough
to substitute for an in-aircraft calibration of your real airplane.

## Requirements

- **X-Plane 12.4.0 or newer** (SDK 4.3.0 — the version required by the SDK we ship; older sims need to bump their X-Plane install).
- **macOS** (Apple Silicon or Intel) **or Linux**. Windows builds are
  released manually — see [Windows install](#windows) below.
- A working X-Plane audio output (the plugin uses OpenAL).
- Plugin file: `AOA-Tone-FlyOnSpeed.xpl` (per-platform, downloaded below).

## Install

### Step 1 — Get the plugin

Download from the latest release on
[github.com/flyonspeed/OnSpeed-Gen3/releases](https://github.com/flyonspeed/OnSpeed-Gen3/releases).
Look for files named:

| Platform | File |
|---|---|
| macOS | `onspeed-<version>-xplane-macos.xpl` |
| Linux | `onspeed-<version>-xplane-linux.xpl` |
| Windows | (manual release — ask in Discord, or build from source) |

The plugin version matches your firmware version — both come from the
same git tag.

### Step 2 — Install into X-Plane

Find your X-Plane root directory (the folder that contains the X-Plane
binary, named `X-Plane 12` or similar).

Create this directory structure inside it:

```
<X-Plane root>/
└── Resources/
    └── plugins/
        └── AOA-Tone-FlyOnSpeed/
            └── mac_x64/                  ← or lin_x64/, win_x64/
                └── AOA-Tone-FlyOnSpeed.xpl
```

Drop the downloaded `.xpl` in (rename it from
`onspeed-<version>-xplane-<platform>.xpl` to `AOA-Tone-FlyOnSpeed.xpl`).

### Step 3 — Verify

Launch X-Plane. From the menu bar: **Plugins → Plugin Admin → Enabled**
and confirm **AOA Tone Fly On Speed** is in the list and enabled.

Open **Plugins → AOA Tone Fly On Speed** — a control window appears
with the four AOA threshold values and a status display.

Pick any aircraft, take off, slow down to a stall in level flight. You
should hear the low pulse tone start as you cross LDmax, transition to
solid low at OnSpeed, then high pulse, then fast high pulse (the stall
warning) as you approach the break.

## Usage

### The control window

The plugin's control window has three sections:

- **AOA thresholds** — four editable fields. From low to high:
  - **Below LDmax** — below this AOA, no tone plays.
  - **Below OnSpeed** — between this and the next, low pulse tone.
  - **OnSpeed Max** — between this and the next, solid low tone (you're "On Speed").
  - **Above OnSpeed Max** — above this, high tone (warning region).
- **Live AOA readout** — current X-Plane AOA, updated each frame.
- **Status / version** — plugin enabled state, plus the OnSpeed version
  string.

Edit any threshold and the plugin uses the new value immediately. The
defaults are tuned for a generic GA single — for an aircraft-specific
profile, replace them with your own calibrated values.

### Muting / disabling

**Plugins → AOA Tone Fly On Speed → Audio On/Off** toggles the tones
without unloading the plugin. Useful for quick A/B comparison while
flying or for screen recordings.

### Reload after editing

X-Plane has no hot-reload for plugins. If you replace the `.xpl` (e.g.
to install a newer build), restart X-Plane.

## Building from source

Required if you want a Windows build, or if you want to track the
latest `master` ahead of a release.

Prerequisites:

- CMake 3.15+
- A C++17 compiler (Apple Clang, GCC, MSVC).
- **macOS**: nothing else — OpenAL is a system framework.
- **Linux**: `sudo apt-get install libopenal-dev`.
- **Windows**: install OpenAL SDK from
  [openal-soft.org](https://www.openal-soft.org/), point CMake at it
  via `OPENAL_INCLUDE_DIR` and `OPENAL_LIBRARY`.

```bash
git clone --recursive https://github.com/flyonspeed/OnSpeed-Gen3.git
cd OnSpeed-Gen3/software/OnSpeed-XPlane-Plugin
cmake -S . -B build
cmake --build build
```

Output ends up in:

| OS | Path |
|---|---|
| macOS | `build/mac_x64/AOA-Tone-FlyOnSpeed.xpl` |
| Linux | `build/lin_x64/AOA-Tone-FlyOnSpeed.xpl` |
| Windows | `build/win_x64/AOA-Tone-FlyOnSpeed.xpl` |

To install for development testing on macOS or Linux:

```bash
./scripts/install_dev.sh "/path/to/X-Plane 12"
```

The script copies the just-built `.xpl` into the right per-arch folder
under `Resources/plugins/AOA-Tone-FlyOnSpeed/`. Restart X-Plane.

## Troubleshooting

### Plugin doesn't appear in the Plugin Admin list

X-Plane's `Log.txt` (in the X-Plane root) records plugin load failures.
Open it and search for `AOA-Tone-FlyOnSpeed`. Common causes:

- Wrong directory layout: the `.xpl` must be inside a per-arch
  subdirectory (`mac_x64`, `lin_x64`, `win_x64`), not directly in
  `AOA-Tone-FlyOnSpeed/`.
- Architecture mismatch: a `mac_x64` `.xpl` won't load on Linux.

### Plugin loads but no audio plays

- Check **X-Plane → Settings → Sound** to confirm the master sound
  output is unmuted and routed where you expect.
- Confirm IAS is above the 25-kt gate (the plugin doesn't play tones at
  rest on the runway).
- Confirm AOA is above LDmax — at low AOAs the plugin is correctly
  silent.
- On Linux, install `libopenal1` if it isn't already present:
  `sudo apt-get install libopenal1`.

### Tones sound wrong for my aircraft

The plugin's defaults are generic. To tune for a specific airframe,
edit the four AOA thresholds in the plugin's control window. If your
aircraft has been calibrated on a real OnSpeed installation, copy the
threshold values from the calibration wizard's
**[Verifying Calibration](../calibration/verification.md)** output.

### Windows {#windows}

Windows binaries aren't built by CI yet. If you need one, either:

1. Build from source (see [Building from source](#building-from-source)),
   or
2. Ask in the project Discord — community members have shared Windows
   builds informally.

Adding Windows to the CI matrix needs an OpenAL SDK install step plus
SDK-lib path handling. Tracked as a follow-up to
[issue #220](https://github.com/flyonspeed/OnSpeed-Gen3/issues/220);
PRs welcome.

## Verifying a release locally (release checklist)

Before tagging an OnSpeed release, anyone with X-Plane installed can
spend ~15 minutes confirming the plugin still plays the right cues.
Keep this list short and current — it lives at the bottom of this page
on purpose, so a release manager finds it without hunting.

1. Build from `master`: `cmake --build build`.
2. Install via `./scripts/install_dev.sh ~/X-Plane\ 12`.
3. Launch X-Plane, load the Cessna 172 on a runway.
4. **Plugins → Plugin Admin** confirms the plugin is enabled.
5. **Plugins → AOA Tone Fly On Speed** opens the control window; the
   version string matches the release tag.
6. Take off, climb to 4,000 ft, reduce to 60% power.
7. **Below LDmax**: no tone audible at cruise AOA.
8. **LDmax to OnSpeed**: pulse rate climbs as you slow.
9. **OnSpeed band**: solid low tone at ~65 kt clean.
10. **Above OnSpeed**: high pulse as you slow further.
11. **Stall warning**: rapid high pulse just before the stall break.
12. Toggle **Audio On/Off** — tones cut, then return.

If any step is wrong, the release isn't ready.

## Source

- Plugin source: [`software/OnSpeed-XPlane-Plugin/`](https://github.com/flyonspeed/OnSpeed-Gen3/tree/master/software/OnSpeed-XPlane-Plugin)
- AOA decision logic: [`software/Libraries/onspeed_core/src/audio/ToneCalc.h`](https://github.com/flyonspeed/OnSpeed-Gen3/blob/master/software/Libraries/onspeed_core/src/audio/ToneCalc.h)
  — same code path the firmware uses.
- Tracking issue: [#220 — Move OnSpeed-XPlane plugin into this repo](https://github.com/flyonspeed/OnSpeed-Gen3/issues/220)
