# X-Plane Plugin: Glareshield-Mounted Indexer + Per-Aircraft Verification + L/D Wizard

Date: 2026-05-18
Status: Design — pending approval
Owner: Sam (with feedback from Vac)

## Context

Vac is using the OnSpeed X-Plane plugin and asked for two things:

1. **Place the indexer in 3D space, anchored to each aircraft.** Today the indexer is a floating 2D window in X-Plane's chrome (or popped out to its own OS window). Vac wants the equivalent of bolting it to the glareshield: when he pans the view, the indexer slides like a real instrument; when he switches aircraft, the position remembers where it was for that specific airframe.
2. **Verify that settings actually stick per aircraft.** He reported that they don't. Reading the code, they should — there's already a `.prf` file per aircraft with full state. But the report needs empirical verification before extending the same machinery with new 3D-position fields. Sam will run this verification separately.

An earlier draft included a third deliverable — an L/D max calibration wizard — but X-Plane exposes no aerodynamic-polar dataref we can build on, and the rule-of-thumb (Vbg ≈ 1.5·Vs) the auto-setpoint code already uses is the same approximation any wizard would have to start from. Pilots who want a measured number can tune `fLDMAXAOA` in the audio control window. Removed from scope.

This spec covers deliverable B (mounted indexer) as the headline feature, plus a small side-task to support deliverable A (drop a legacy-format JSON in `Custom Data/` so the user can sanity-check the legacy-import path on a real aircraft load).

## Background: what's already in the plugin

Three relevant subsystems are already in place at master:

### Per-aircraft settings persistence

`software/OnSpeed-XPlane-Plugin/src/aoa_audio.cpp` writes a per-aircraft preferences file:

```
Output/preferences/AOA-Tone-FlyOnSpeed-<acf_basename>.prf
```

The file uses plain `key = value` lines. Currently persisted:

- AOA setpoints: `fLDMAXAOA`, `fONSPEEDFASTAOA`, `fONSPEEDSLOWAOA`, `fSTALLWARNAOA`, `fALPHASTALL`
- Audio: `iMuteAudioUnderIAS`, `iMasterVolumePct`, `audioEnabled`, `bMuteStallHorn`
- AOA filtering: `iAoaMedianWindow`, `iAoaMeanWindow`
- Reference: `iVs1G` (clean stall, KIAS)
- Serial output: `serialPortPath`
- Audio control window: `audioWindowLeft/Top/Width/Height`, `audioWindowVisible`
- **Indexer window** (already): `indexerVisible`, `indexerMode`, `indexerPoppedOut`, `indexerFloatLeft/Top/Width/Height`, `indexerPopLeft/Top/Width/Height`

Load happens on `XPLM_MSG_PLANE_LOADED`; restore of the indexer geometry is deferred one flight-loop tick (an SDK-context restriction documented in `IndexerWindow.h`). Save happens on a 1 Hz periodic callback when a dirty flag is set.

### Auto-derived AOA setpoints

`src/m5_indexer/AutoSetpoints.cpp` derives all four setpoints from three X-Plane datarefs:

- `sim/aircraft/overflow/acf_stall_warn_alpha` — wing AOA where the stall warner fires
- `sim/aircraft/view/acf_Vs` — clean stall KIAS
- `sim/aircraft/view/acf_Vso` — landing-config stall KIAS

Per-flap stall AOA is interpolated by `(Vso/Vs)²`. NAOA fractions `{ldmax=0.45, onSpeedFast=0.549, onSpeedSlow=0.640, stallWarn=0.92}` map alpha_stall to each setpoint. LDmax at 0.45 assumes Vbg ≈ 1.5·Vs — rule of thumb, not a measured value.

This runs at first aircraft load when no `.prf` exists; afterwards the saved `.prf` wins.

### Existing positioning modes

`src/m5_indexer/IndexerWindow.cpp` supports two:

- **Floating** (`xplm_WindowPositionFree`) — X-Plane window with chrome, draggable, lives in the X-Plane global desktop boxel space.
- **Popped-out** (`xplm_WindowPopOut`) — own OS window, draggable to any monitor.

Both are persisted independently so toggling pop-out and back returns to the prior floating spot.

## Hard finding: Apple Silicon kills true-3D rendering

The X-Plane SDK header (`XPLMDisplay.h` lines 125-135) explicitly states:

> As of XPLM302 the legacy 3D drawing phases (`xplm_Phase_FirstScene` to `xplm_Phase_LastScene`) are deprecated. ... There is a new drawing phase, `xplm_Phase_Modern3D`, which is supported under OpenGL and Vulkan ... **This phase is NOT supported under Metal** and comes with potentially substantial performance overhead.

OnSpeed development happens on Apple Silicon Macs where X-Plane runs Metal. **A textured quad rendered in 3D space with perspective foreshortening is not buildable on Metal.** This rules out the "true 3D billboard" approach.

The path that works on Metal: render in `xplm_Phase_LastCockpit` (a 2D phase, on top of the cockpit view), with the screen position computed by hand-projecting a 3D anchor each frame. The indexer stays the same on-screen pixel size always, but its screen *position* moves as the camera rotates, exactly as if a real instrument were bolted to the glareshield.

## Deliverable A — Verify per-aircraft persistence (~half day, owned by Sam)

### Scope

Empirical smoke-test of the existing `.prf` machinery to either confirm Vac's report or rule it out. No code changes unless a real bug is found. Sam owns this.

### Side-task: legacy-JSON drop for sanity check

The current plugin (master) already imports a legacy-format JSON at:

```
<X-Plane root>/Custom Data/<acf_ui_name>.json
```

on first aircraft load when no `.prf` exists yet. The format is a flat JSON object with five keys (see `aoa_audio.cpp::TryImportLegacyJson` and `buildLegacyJsonPath`):

```json
{
  "Below LDMax":     5.5,
  "Below OnSpeed":   7.0,
  "OnSpeed Max":     8.5,
  "Above OnSpeed":   11.0,
  "IAS Tone Enable": 40
}
```

Map to current plugin globals:

| Legacy key | Current global |
|---|---|
| `Below LDMax` | `fLDMAXAOA` |
| `Below OnSpeed` | `fONSPEEDFASTAOA` |
| `OnSpeed Max` | `fONSPEEDSLOWAOA` |
| `Above OnSpeed` | `fSTALLWARNAOA` |
| `IAS Tone Enable` | `iMuteAudioUnderIAS` |

Order constraint: `Below LDMax < Below OnSpeed < OnSpeed Max < Above OnSpeed`. If violated, the import is skipped and the legacy file is left in place.

A test file has been written to `/Users/sritchie/X-Plane 12/Custom Data/Cessna 172 SP Skyhawk - 180HP.json` with the values shown above. The filename assumes `acf_ui_name` for the stock C172 SP equals `Cessna 172 SP Skyhawk - 180HP` (read from the `.acf` file's `acf/_descrip` field, which X-Plane uses as the UI name when no explicit `_ui_name` is set).

To verify the import:

1. Delete any existing `Output/preferences/AOA-Tone-FlyOnSpeed-Cessna_172SP.prf` (or whatever the .prf basename is for the stock C172 — `sanitizeAcfBasename` strips paths and extension).
2. Load the stock C172 in X-Plane.
3. Check X-Plane's `Log.txt`. On a successful import you'll see lines like:
   ```
   FlyOnSpeed: legacy json-config found at <path>.json — attempting import
   FlyOnSpeed: imported legacy thresholds — ...
   ```
4. The `Custom Data/<name>.json` file is renamed to `<name>.json.imported` on successful import. If it's still there, the import was skipped (parse incomplete, thresholds out of order, or X-Plane reports a different `acf_ui_name` than guessed). The Log.txt will say which.
5. If the path was wrong, copy `<X-Plane>/Custom Data/Cessna 172 SP Skyhawk - 180HP.json` to whatever path the log reports the plugin DID look at.

This verifies both: (a) the legacy-import code path works end-to-end, and (b) `acf_ui_name` resolves to the expected string for the stock C172. Useful even if per-aircraft persistence otherwise turns out to be solid.

### Test matrix

Two stock aircraft (default RV-10 + one other, e.g., Cessna 172). For each pairing:

| Setting | Change in aircraft A | Switch to B | Switch back to A | Expect |
|---|---|---|---|---|
| `fLDMAXAOA` | 6.0° → 7.5° | (loads B's value) | (loads A's saved 7.5°) | 7.5° |
| `audioEnabled` | on → off | (loads B's) | (loads A's off) | off |
| `iMasterVolumePct` | 60 → 90 | (loads B's) | (loads A's 90) | 90 |
| `iMuteAudioUnderIAS` | 40 → 50 | (loads B's) | (loads A's 50) | 50 |
| Indexer mode | 0 → 3 | (loads B's) | (loads A's mode 3) | 3 |
| Indexer position | drag to top-left | (loads B's pos) | (loads A's top-left) | top-left |
| Pop-out state | floating → popped | (loads B's state) | (loads A's popped) | popped |

Restart X-Plane between each "switch back" to test cold-restore in addition to hot-switch.

### Method

- Install dev build via `scripts/install_dev.sh`.
- Open `Output/preferences/` in Finder side-by-side with X-Plane.
- For each test case: change setting → wait 2 s (1 Hz flush) → cat the `.prf` to verify the new value is on disk → switch aircraft → cat the destination `.prf` → verify content → switch back → verify in-sim.
- Document any test that fails as `bug/persistence-<setting>` and file an issue. Fix before B.

### Documentation

Append findings to `docs/site/docs/xplane/settings.md` — confirm the per-aircraft semantics, list every persisted key. Reduces "doesn't stick" reports.

## Deliverable B — Glareshield-mounted indexer (~3 days)

### User-visible behavior

**Menu** (under `Plugins → Fly On Speed`):

```
Indexer position →
   ● Floating window
   ○ Popped-out OS window
   ○ Mounted in 3D cockpit
```

Radio-group bullets reflect the active mode. Click switches modes immediately.

**Mounted mode behavior:**

- Indexer renders as a 320×240 quad in screen space, centered on the projection of a 3D anchor point in aircraft body frame.
- Pan view → indexer slides like a real instrument bolted to the glareshield.
- View pitch/roll change → indexer rotates with the aircraft (since the anchor is in body frame).
- Indexer stays at the same screen-pixel size regardless of viewing angle (no foreshortening). Always readable.
- Switch to external view (chase, runway, tower) → indexer hides; switch back → reappears at the persisted spot.
- Click the indexer body and drag → moves it to a new spot on the glareshield. The new position persists per aircraft.
- Click without drag → cycles indexer mode (same as today's floating-window behavior).

### Architecture

#### Placement mode enum

Replace the existing `isPoppedOut` bool with an enum:

```cpp
enum PlacementMode : int {
    kPlacementFloating  = 0,
    kPlacementPopOut    = 1,
    kPlacementMounted3D = 2,
};
```

`PersistedState` (in `IndexerWindow.h`) grows three fields:

```cpp
PlacementMode placementMode = kPlacementFloating;
float mount3D_X = 0.0f;     // meters, +X right in aircraft body frame
float mount3D_Y = 0.05f;    // meters, +Y up
float mount3D_Z = 0.30f;    // meters, +Z forward (out the nose)
```

Defaults place the indexer 30 cm forward and 5 cm above the pilot's eyepoint — a sensible starting spot.

#### Render path: reuse the per-window callback

Critical constraint from `software/OnSpeed-XPlane-Plugin/CLAUDE.md`:

> Draw INSIDE the per-window callback. NOT a global XPLMRegisterDrawCallback phase. X-Plane's matrices are set up to map window coords → clip space ONLY during the per-window callback.

This rules out `XPLMRegisterDrawCallback(xplm_Phase_LastCockpit)` as the render surface — even though it's a 2D phase, the matrix setup isn't right for plugin draws on Metal, and the existing `RenderTexturedQuadVA` would silently no-op.

The fix: keep using the existing per-window draw callback. In mounted mode, instead of rendering at a static window position, we:

1. Compute the projected screen rect once per flight loop (in `Tick`).
2. Call `XPLMSetWindowGeometry(s_window, left, top, right, bottom)` to move the window to the projected rect.
3. Switch the window's decoration to `xplm_WindowDecorationSelfDecorated` (no chrome — chrome would visually break the "mounted to the cockpit" effect).
4. The existing per-window `DrawWindow` callback then renders the quad at the new geometry on the next frame. No new render path.

In essence, mounted mode is "floating window where geometry is driven by projection math instead of the pilot's drag." The MODE-button click handling carries over verbatim. Drag handling adds a small layer on top (see "Mouse drag" below).

When the placement mode is `kPlacementMounted3D` and the current view is external (not 3D-cockpit), the geometry update is skipped AND `XPLMSetWindowIsVisible(false)` hides the window. When the pilot returns to 3D-cockpit view, visibility is restored and geometry resumes tracking the projection.

The view check uses `sim/graphics/view/view_type` — int dataref. Accept `1023` (forward with panel) and `1026` (forward 3D cockpit). Reject everything else.

If the anchor projects behind the camera (camera-frame Z >= 0), the geometry update is skipped AND visibility is hidden. The visibility flip is the on/off switch; geometry move is the position update.

#### Projection math

A pure function in a new file `Indexer3DPlacement.cpp/h`, unit-testable without linking XPLM.

**Signature:**

```cpp
struct Anchor3D       { float xMeters, yMeters, zMeters; };
struct AircraftState  { float xWorld, yWorld, zWorld; float pitch, roll, heading; };
struct CameraState    { float xWorld, yWorld, zWorld; float pitch, roll, heading; float fovDeg; };
struct ScreenDim      { int wPx, hPx; };

struct ProjectedQuad {
    float centerX, centerY;   // screen pixels, origin bottom-left
    bool  visible;            // false when behind camera or fully off-screen
};

ProjectedQuad ProjectAnchor(const Anchor3D& a,
                            const AircraftState& ac,
                            const CameraState& cam,
                            const ScreenDim& sd);
```

**Algorithm:**

1. `p_local = (xMeters, yMeters, zMeters)` — anchor in aircraft body frame. Convention: +X right, +Y up, +Z forward.
2. `p_world_offset = R_aircraft(heading, pitch, roll) × p_local` — rotate by aircraft attitude.
3. `p_world = (xWorld, yWorld, zWorld) + p_world_offset` — translate by aircraft world position.
4. `p_cam_offset = p_world - (cam.xWorld, cam.yWorld, cam.zWorld)` — translate to camera origin.
5. `p_cam = R_cam_inv(cam.heading, cam.pitch, cam.roll) × p_cam_offset` — rotate to camera frame.
6. If `p_cam.z >= 0` (behind camera, OpenGL convention: −Z is forward): `visible = false`, return.
7. `focal = sd.hPx / (2 × tan(cam.fovDeg × π / 360))` — pixel focal length.
8. `screen_x = (p_cam.x / -p_cam.z) × focal + sd.wPx / 2`.
9. `screen_y = (p_cam.y / -p_cam.z) × focal + sd.hPx / 2`.
10. If `screen_x` or `screen_y` is more than `quad_half + margin` off-screen: `visible = false`, return.
11. Otherwise: `visible = true`, return `(screen_x, screen_y)`.

Inverse projection (for mouse drag) is the same chain in reverse, fixing the camera-frame Z to the click-time depth:

```cpp
Anchor3D InverseProject(float screenX, float screenY, float depthMeters,
                        const AircraftState& ac, const CameraState& cam,
                        const ScreenDim& sd);
```

#### Mouse drag

The existing `HandleClick` callback in `IndexerWindow.cpp` already runs on the X-Plane window. Since mounted mode reuses that window (just driving its geometry), the click handler keeps working for free — the MODE button still cycles modes.

Key behavioral choice: when the pilot drags, the anchor moves on a plane parallel to the camera image plane at the *click-time* distance. The depth (camera-frame Z) of the anchor is captured on `xplm_MouseDown` and held constant for the duration of the drag. Without this, a small mouse motion at large distance produces a huge anchor jump (the indexer punches through the panel into infinity or shoots back to the horizon). With it, drag feels like sliding the indexer along the glareshield at the depth it was when the pilot grabbed it.

The current `HandleClick` only handles `xplm_MouseDown` and consumes other events with `return 1`. To support drag we extend it to recognize `xplm_MouseDrag` and `xplm_MouseUp` when placement mode is `kPlacementMounted3D`.

**Drag handler addition to `HandleClick`:**

```cpp
int HandleClick(XPLMWindowID, int x, int y, XPLMMouseStatus status, void*) {
    static bool s_dragging = false;
    static int  s_dragOffsetX = 0, s_dragOffsetY = 0;
    static int  s_downX = 0, s_downY = 0;
    static float s_dragDepth = 0.0f;

    if (status == xplm_MouseDown) {
        s_dragging = false;            // resolved on first drag or up
        s_downX = x; s_downY = y;
        s_dragDepth = ComputeClickDepth(/*current anchor*/);
        s_dragOffsetX = x - projectedCenterX;
        s_dragOffsetY = y - projectedCenterY;
        return 1;                      // consume
    }
    if (status == xplm_MouseDrag) {
        if (!s_dragging) {
            if (abs(x - s_downX) + abs(y - s_downY) < kDragThreshold) return 1;
            s_dragging = true;
        }
        const Anchor3D newAnchor = InverseProject(
            x - s_dragOffsetX, y - s_dragOffsetY, s_dragDepth, ...);
        s_persisted.mount3D_X = newAnchor.xMeters;
        s_persisted.mount3D_Y = newAnchor.yMeters;
        s_persisted.mount3D_Z = newAnchor.zMeters;
        s_dirty = true;
        return 1;
    }
    if (status == xplm_MouseUp) {
        if (!s_dragging) {
            CycleMode();                // tap behavior preserved
        }
        s_dragging = false;
        return 1;
    }
    return 0;
}
```

`kDragThreshold = 5 px` — standard UI deadband.

#### .prf schema changes

New keys appended to `SaveSettings` / `LoadSettings`:

```
indexerPlacementMode = 2
indexerMount3DX = 0.000000
indexerMount3DY = 0.050000
indexerMount3DZ = 0.300000
```

**Migration from old `.prf`:**

If `indexerPlacementMode` is missing and `indexerPoppedOut` is present, map:

- `indexerPoppedOut = 1` → `placementMode = kPlacementPopOut`
- `indexerPoppedOut = 0` → `placementMode = kPlacementFloating`

This preserves Vac's existing configurations for both his aircraft.

### Edge cases

- **View becomes external during a drag.** The drag is aborted on the first external-view frame; current anchor is kept. Pilot can resume in 3D cockpit.
- **FOV change mid-session.** Projection updates next frame using the new FOV. The indexer's apparent on-screen size stays the same (since we draw a fixed-pixel quad, not a 3D scaled one).
- **Multi-monitor X-Plane.** The draw callback fires once per monitor that's rendering the cockpit view. The indexer draws on each one it's visible on. Acceptable for v1.
- **Aircraft inverted or in a roll.** Anchor stays in body frame; indexer rolls with the aircraft. Correct behavior.
- **Pilot's head-track / VR.** Out of scope for v1. Future enhancement could use `xplm_WindowVR` in the same .prf schema.
- **Apple Silicon Metal.** `xplm_Phase_LastCockpit` is a 2D phase that works on Metal. The existing `RenderTexturedQuadVA` path is already proven on Apple Silicon. The new draw callback reuses it verbatim.

### Testing

#### Unit tests (`test/test_indexer_3d_projection/`)

| Test | Setup | Expected |
|---|---|---|
| `ProjectAheadCenter` | anchor=(0,0,0.3) body, aircraft level, camera at eyepoint looking forward, FOV=70°, screen=1920×1080 | projection lands within 5 px of screen center, visible=true |
| `ProjectBehindCamera` | anchor=(0,0,−0.3) | visible=false |
| `ProjectPanRight30` | anchor=(0,0,0.3), camera yaw +30° | screen_x shifts left by ≈ 1920 × (30/70) px |
| `ProjectBank45` | anchor=(0.1,0,0.3), aircraft roll +45° | the +X body offset maps to a diagonal world offset; projection lands accordingly |
| `InverseRoundTrip` | project an anchor, run inverse-project at same camera + same z-depth | recover the original anchor within 1 mm |
| `OffScreenSuppressed` | anchor that projects to (−500, −500) | visible=false |

#### Manual / browser tests

| Scenario | Steps | Pass criteria |
|---|---|---|
| Default placement renders | Enable mounted mode in default RV-10 | Indexer appears in front of pilot, ~30 cm ahead, 5 cm above |
| Pan tracks | Pan view right via mouse-pan | Indexer slides smoothly off the left edge without jitter |
| Drag persists | Click+drag to upper-left of glareshield | Quad follows mouse; on release, .prf reflects new mount3D fields within 2 s |
| Cold restore | Drag, then quit X-Plane, restart, load same aircraft | Indexer returns to dragged spot |
| Per-aircraft | Drag in RV-10, switch to C172, drag, switch back | RV-10 returns to its spot, C172 returns to its own |
| External view hides | Press F (chase view) | Indexer disappears; press W back to 3D → reappears |
| Tap cycles mode | Single click without drag motion | Mode cycles, position unchanged |

### Implementation order (three sub-PRs of B)

1. **B-1: Pure math.** `Indexer3DPlacement.cpp/h` with `ProjectAnchor` and `InverseProject` as pure functions. Six unit tests. No X-Plane integration. ~1 day. Reviewable in isolation.
2. **B-2: Integration.** New `PlacementMode` enum, new .prf keys + migration, menu submenu, `xplm_Phase_LastCockpit` draw callback wired to the projection math, X-Plane window geometry tracking the projected rect. No drag yet — anchor stays at the default for B-2. ~1 day.
3. **B-3: Mouse drag.** Add drag handling to `HandleClick`, inverse-project on drag, persist to .prf. ~1 day.

## Scope explicitly out

- **L/D max calibration wizard.** No `acf_Vbg` dataref exists and an in-sim derivation only refines a guess. Pilots can hand-tune `fLDMAXAOA` in the audio control window if the 0.45·alpha_stall default is off for their airframe.
- **VR (`xplm_WindowVR`) placement.** Punted to a v2. The mounted-mode .prf schema is forward-compatible (a future `kPlacementVR` enum + VR-coords block won't break .prf parsing).
- **Stall-mark logic changes.** Already correct — `acf_stall_warn_alpha` is the StallWarn anchor.
- **Per-flap setpoint editing UI.** Tracked in issue #393, separate effort.
- **Cross-platform validation.** macOS arm64 is the primary target; Linux/Windows builds tracked in issue #396.

## Decisions log

| Decision | Why |
|---|---|
| Screen-projected 2D quad, not true 3D | `xplm_Phase_Modern3D` doesn't run on Metal; Apple Silicon is primary dev target |
| Mounted mode hides the X-Plane window but keeps it alive | Reuses proven MODE-button click handling; resizing the window to the projected rect is cheaper than building a parallel click router |
| Anchor in aircraft body frame, not world frame | Indexer rolls with the aircraft, like a real glareshield instrument; correct under unusual attitudes |
| `kDragThreshold = 5 px` | Standard UI deadband; large enough to absorb hand tremor on tap, small enough to feel responsive |
| Default mount3D = (0, 0.05, 0.3) | 30 cm in front, 5 cm up — visible from a typical eyepoint without obscuring anything |
| .prf migration: `indexerPoppedOut → indexerPlacementMode` | Preserves Vac's existing configs across the upgrade |
| Drop deliverable C (L/D wizard) | No aerodynamic-polar dataref to build on; the in-sim measurement only refines a guess. Pilot tuning of `fLDMAXAOA` is the lighter answer. |

## Open questions for the user

None blocking implementation. The user may want to weigh in on:

- Should the menu radio-group be a submenu (as drafted) or three separate top-level menu items? Submenu is less cluttered.
- Default mount3D position — is (0, 0.05, 0.3) sensible, or should the default be aircraft-aware (e.g., read pilot eyepoint + nominal glareshield offset from the .acf)? Default-aware would be marginal effort; current decision is to ship the static default and let the pilot drag it.
