# Install

The plugin ships as a single `.xpl` file. Drop it into the per-arch
directory under your X-Plane `Resources/plugins/` tree and restart
X-Plane.

## Requirements

- **X-Plane 12.4.0 or newer.** The plugin links the X-Plane SDK 4.3.0
  XPLM400 API surface, which is gated on this X-Plane version.
- A working X-Plane audio output (the plugin uses OpenAL).

## Platform support

| Platform | Status |
|---|---|
| **macOS (Apple Silicon)** | Verified — primary development target. |
| **macOS (Intel)** | Builds; not regularly tested. |
| **Windows x86_64** | Builds via CI; M5 indexer build verification tracked in [#396](https://github.com/flyonspeed/OnSpeed-Gen3/issues/396). |
| **Linux x86_64** | Builds via CI; M5 indexer build verification tracked in [#396](https://github.com/flyonspeed/OnSpeed-Gen3/issues/396). |

The audio-only path (without the embedded indexer) builds and runs on
all three platforms via the GitHub Actions matrix. The full indexer
build is only routinely smoke-tested on Apple Silicon today.

## Download

Pull the latest `.xpl` from the
[OnSpeed-Gen3 releases page](https://github.com/flyonspeed/OnSpeed-Gen3/releases).
Assets are named per platform:

| Platform | File |
|---|---|
| macOS | `onspeed-<version>-xplane-macos.xpl` |
| Linux | `onspeed-<version>-xplane-linux.xpl` |
| Windows | `onspeed-<version>-xplane-windows.xpl` |

The plugin version matches the firmware version — both tags come off
the same git commit.

For master-branch builds (no release cut yet), grab the artifact from
the most recent CI run on the
[Actions tab](https://github.com/flyonspeed/OnSpeed-Gen3/actions/workflows/ci.yml)
or via [nightly.link](https://nightly.link/flyonspeed/OnSpeed-Gen3/workflows/ci/master).
PRs include a sticky "Firmware Artifacts" comment with the same links.
Artifacts are retained for 30 days.

## Install into X-Plane

Find your X-Plane root directory (the folder that contains the
`X-Plane.app` bundle on macOS, or `X-Plane.exe` on Windows). Create
this directory tree inside it:

```
<X-Plane root>/
└── Resources/
    └── plugins/
        └── AOA-Tone-FlyOnSpeed/
            ├── mac_x64/                        ← macOS
            │   └── AOA-Tone-FlyOnSpeed.xpl
            ├── win_x64/                        ← Windows
            │   └── AOA-Tone-FlyOnSpeed.xpl
            └── lin_x64/                        ← Linux
                └── AOA-Tone-FlyOnSpeed.xpl
```

Drop the downloaded `.xpl` into the matching per-arch subdirectory,
renaming it from `onspeed-<version>-xplane-<platform>.xpl` to
`AOA-Tone-FlyOnSpeed.xpl`. Only the per-arch subdirectory for your
host OS needs to exist; the others can be omitted.

The `.xpl` is self-contained — no other downloads, installers, or
runtime libraries needed. On Windows the build statically links
libgcc, libstdc++, winpthread, and OpenAL Soft so a fresh sim PC
needs nothing else on PATH. On macOS OpenAL is a system framework.
On Linux, install `libopenal1` if it isn't already present:
`sudo apt-get install libopenal1`.

## Verify

Launch X-Plane. From the menu bar:
**Plugins → Plugin Admin → Enabled** and confirm
**AOA Tone Fly On Speed** appears in the list and is enabled.

Open **Plugins → Fly On Speed → Show** — the audio control window
appears with the four AOA threshold fields, the IAS-mute and
master-volume rows, and a status line that reports the OnSpeed version
string (the same string the panel-mounted Gen3 reports on its web
config page).

If the plugin doesn't load, X-Plane records a reason in `Log.txt` at
the X-Plane root. Search for `AOA-Tone-FlyOnSpeed`. The most common
cause is the wrong directory layout — the `.xpl` must sit inside the
per-arch subdirectory, not directly in `AOA-Tone-FlyOnSpeed/`.

## Building from source

Source lives at
[`software/OnSpeed-XPlane-Plugin/`](https://github.com/flyonspeed/OnSpeed-Gen3/tree/master/software/OnSpeed-XPlane-Plugin).
A build recipe and prerequisite list sit in the plugin's `README.md`.
The CI workflow at `.github/workflows/ci.yml` is the canonical
reference for the per-platform toolchain.
