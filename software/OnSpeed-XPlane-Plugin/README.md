# OnSpeed X-Plane Plugin

OnSpeed AOA tones for X-Plane 12. Plays the same audio cues as the
panel-mounted Gen3 box, driven by X-Plane's flight model.

**End-user docs**: [X-Plane Simulator](https://dev.flyonspeed.org/OnSpeed-Gen3/xplane/) — install, indexer, M5 tethering, settings, troubleshooting.

## Building from source

Requires CMake 3.15+, a C++17 compiler, OpenAL, and PlatformIO (for
the M5 libdeps prefetch).  The embedded M5 indexer is built by
default; pass `-DENABLE_M5_INDEXER=OFF` for an audio-only `.xpl`.

The plugin's CMake build fetches SDL2 as source and links it statically,
so the resulting `.xpl` has no `libSDL2` dylib dependency on the pilot's
machine.  First-time configure adds ~30 s for the SDL2 pull; subsequent
builds reuse the cached source.

The M5 indexer prefetch step (`pio run -e native -d ../OnSpeed-M5-Display`,
called out below) is a separate PlatformIO project that links system
SDL2 directly — a developer building the plugin from source still needs
SDL2 installed for that prefetch.  The pilot installing the prebuilt
`.xpl` does not.

| OS | OpenAL (developer) | SDL2 (developer) |
|---|---|---|
| macOS | system framework, no install needed | `brew install sdl2` (for M5 prefetch); plugin links its own static SDL2 |
| Linux | `sudo apt-get install libopenal-dev` | `sudo apt-get install libsdl2-dev` (for M5 prefetch); plugin links its own static SDL2 |
| Windows | [OpenAL Soft](https://www.openal-soft.org/), set `OPENAL_INCLUDE_DIR` and `OPENAL_LIBRARY` | M5 indexer not yet supported (#396) — pass `-DENABLE_M5_INDEXER=OFF` |

To opt out of the bundled SDL2 (faster incremental dev builds at the
cost of a non-portable `.xpl`), pass `-DONSPEED_SYSTEM_SDL2=ON` to use
the system SDL2 already installed for the M5 prefetch.

The embedded M5 indexer reuses the M5 display project's PlatformIO
libdeps cache (M5GFX + M5Unified).  Populate it once before
configuring:

```bash
pip install platformio
pio run -e native -d ../OnSpeed-M5-Display
```

Then build the plugin:

```bash
cmake -S . -B build
cmake --build build
```

Output: `build/{mac_x64,lin_x64,win_x64}/AOA-Tone-FlyOnSpeed.xpl`.

**Audio-only build** (skips SDL2 + the M5 prefetch):

```bash
cmake -S . -B build -DENABLE_M5_INDEXER=OFF
```

To install into a local X-Plane for dev testing (macOS/Linux):

```bash
./scripts/install_dev.sh "/path/to/X-Plane 12"
```

Restart X-Plane to pick up the new build (no plugin hot-reload).

## License and credits

MIT for the plugin code. The vendored X-Plane SDK at `SDK/` is under
its own license — see `SDK/license.txt`.

The plugin was originally written by [Topher Timemachine](https://github.com/TopherTimeMachine)
and [Mrcoole7890](https://github.com/Mrcoole7890), and imported into this repo
from [`flyonspeed/OnSpeed-XPlane`](https://github.com/flyonspeed/OnSpeed-XPlane).
See [CREDITS.md](CREDITS.md) for the full import history and post-import changes.
