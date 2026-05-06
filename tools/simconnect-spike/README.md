# SimConnect Spike

Throwaway WIN32 console exe that opens SimConnect, subscribes to a handful of
flight variables at 20 Hz, prints them to stdout. Used to validate the
SimConnect SDK path, P3D Developer License activation, and Parallels TCP
routing before committing to the full
[OnSpeed Sim Bridge](../../docs/superpowers/specs/2026-05-06-sim-bridge-design.md)
plan.

This is **not** the bridge. It is a 100-line proof of concept. After the
spike succeeds, this directory can be deleted.

## What it does

Per second:

```
t=  1.05s  alpha=  2.34deg  ias= 88.1kt  latG= -0.02  paused=0  onGround=0
t=  1.10s  alpha=  2.36deg  ias= 88.0kt  latG= -0.02  paused=0  onGround=0
...
```

If any of those numbers look right while you fly the default Cessna in P3D,
the SimConnect surface works for the bridge's needs and we can begin Phase 0.

## Prerequisites

- **Mac development host**: same Mac you build the X-Plane plugin on.
- **Parallels Desktop** with a Windows VM. (16 GB RAM recommended for P3D.)
- **Prepar3D Developer License** — $9.95 from
  <https://www.prepar3d.com/product-overview/prepar3d-license-comparison/>.
  Install P3D inside the Parallels VM. Default install location is fine.
- **MinGW-w64** on the Mac for cross-compiling: `brew install mingw-w64`.
- **CMake 3.15+**: should already be installed for the X-Plane plugin work.

## Acquiring the SimConnect SDK

P3D ships SimConnect headers + lib at:

```
C:\Program Files\Lockheed Martin\Prepar3D v6\SDK\Core Utilities Kit\SimConnect SDK\
```

Copy two files into `tools/simconnect-spike/SDK/`:

- `SimConnect.h` (header)
- `lib/SimConnect.lib` (Win64 static lib)

These are not committed to the repo (P3D EULA, plus they're VM-local
artifacts). The CMake config below points at this directory; if your SDK
location differs, edit the path.

## Building

From the spike directory:

```bash
cd tools/simconnect-spike
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=cmake/cross-win64.cmake
cmake --build build
```

Output: `build/simconnect-spike.exe` plus a copy of `SimConnect.dll` (vendored
from the SDK).

## Running

Inside the Parallels VM, from a folder with `simconnect-spike.exe` and
`SimConnect.dll`:

1. Start Prepar3D.
2. Load the default scenario (any aircraft, any airport).
3. Double-click `simconnect-spike.exe` (a console window opens).
4. You should see a stream of 20 Hz lines with current alpha / IAS / lateral G.
5. Pause the sim — `paused` field flips to 1, values stop changing.
6. Touch a runway — `onGround` flips to 1.

If any of the above misbehaves, the spike has succeeded at its actual job
(surface validation) — we know what to fix before writing the bridge proper.

## What this validates

- [ ] SimConnect SDK headers + lib resolve correctly under MinGW cross-compile.
- [ ] `SimConnect_Open` connects to a running P3D instance over loopback.
- [ ] Period-based subscription delivers data at 20 Hz (no callback storms,
      no missed dispatches).
- [ ] Pause and on-ground state flip cleanly.
- [ ] The five variables we care most about for the bridge — `INCIDENCE ALPHA`,
      `AIRSPEED INDICATED`, `ACCELERATION BODY X`, `SIM PAUSED`,
      `SIM ON GROUND` — are populated correctly.
- [ ] Cross-compile from Mac via MinGW produces a binary that runs on Windows
      without runtime errors.

## What this does NOT validate

- MSFS-specific behavior (pinned to v2 of bridge work; P3D and MSFS differ
  only at the edges for our subset).
- Network-mode SimConnect (loopback only in this spike).
- Any of the bridge's audio, indexer, prefs, or USB-serial paths. All of
  those are exercised in Phase 1+ regardless of SimConnect.

## After the spike

If the spike succeeds, this directory and the `feat/simconnect-spike` branch
can be deleted (or merged into the bridge branch as historical reference).
The SimConnect adapter in the bridge proper will be ~150 lines and live at
`software/OnSpeed-Sim-Bridge/src/sources/SimConnectSource.cpp`, not here.
