# OnSpeed X-Plane Plugin

OnSpeed AOA tones for X-Plane 12. Plays the same audio cues as the
panel-mounted Gen3 box, driven by X-Plane's flight model.

**End-user docs**: [Simulator → X-Plane Plugin](https://dev.flyonspeed.org/OnSpeed-Gen3/simulator/xplane-plugin/) — install, usage, troubleshooting.

## Building from source

Requires CMake 3.15+, a C++17 compiler, and OpenAL.

| OS | OpenAL |
|---|---|
| macOS | system framework, no install needed |
| Linux | `sudo apt-get install libopenal-dev` |
| Windows | [OpenAL Soft](https://www.openal-soft.org/), set `OPENAL_INCLUDE_DIR` and `OPENAL_LIBRARY` |

```bash
cmake -S . -B build
cmake --build build
```

Output: `build/{mac_x64,lin_x64,win_x64}/AOA-Tone-FlyOnSpeed.xpl`.

To install into a local X-Plane for dev testing (macOS/Linux):

```bash
./scripts/install_dev.sh "/path/to/X-Plane 12"
```

Restart X-Plane to pick up the new build (no plugin hot-reload).

## License

MIT for the plugin code. The vendored X-Plane SDK at `SDK/` is under
its own license — see `SDK/license.txt`.
