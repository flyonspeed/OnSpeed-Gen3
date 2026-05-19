# X-Plane Mounted Indexer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let the OnSpeed X-Plane indexer be "mounted" on the 3D-cockpit glareshield. The indexer becomes a screen-projected quad that tracks a per-aircraft 3D anchor point; the pilot drags it to place; position persists per aircraft.

**Architecture:** Reuse the existing X-Plane window (it's the only render surface that draws correctly on Apple Silicon Metal). Add a new placement mode `kPlacementMounted3D`. Each flight loop, project a body-frame anchor through aircraft attitude → world → camera → screen, and drive `XPLMSetWindowGeometry` to land the existing window on the projected rect. Click+drag inverse-projects the new screen position back to body-frame coords. Window is recreated with `xplm_WindowDecorationSelfDecorated` (no chrome) when entering mounted mode, and back to `RoundRectangle` when leaving — decoration is set at creation time and can't be changed in place.

**Tech Stack:** C++17, X-Plane SDK 4.x (XPLM200..XPLM400), CMake, native unit tests (no XPLM linkage), GL 1.x client-side vertex arrays in the existing `RenderTexturedQuadVA`.

**Spec:** `docs/superpowers/specs/2026-05-18-xplane-3d-indexer-design.md`

---

## File Structure

**New files (B-1, pure math):**
- `software/OnSpeed-XPlane-Plugin/src/m5_indexer/Indexer3DPlacement.h` — pure data types (Anchor3D, AircraftState, CameraState, ScreenDim, ProjectedQuad) + function declarations (ProjectAnchor, InverseProject).
- `software/OnSpeed-XPlane-Plugin/src/m5_indexer/Indexer3DPlacement.cpp` — implementation. Pure standard C++ (no XPLM headers). Math only.
- `software/OnSpeed-XPlane-Plugin/tests/indexer_3d_placement.cpp` — six unit tests.

**Modified files (B-2, integration):**
- `software/OnSpeed-XPlane-Plugin/src/m5_indexer/IndexerWindow.h` — extend `PersistedState` with `PlacementMode` enum and `mount3D_X/Y/Z` floats; add `RecreateWindowWithDecoration` declaration.
- `software/OnSpeed-XPlane-Plugin/src/m5_indexer/IndexerWindow.cpp` — implement mode-aware window creation, per-Tick geometry update for mounted mode, view-type check, anchor-behind-camera hide.
- `software/OnSpeed-XPlane-Plugin/src/aoa_audio.cpp` — .prf schema additions + migration (indexerPoppedOut → indexerPlacementMode), menu submenu construction, AudioMenuHandler dispatch.
- `software/OnSpeed-XPlane-Plugin/tests/CMakeLists.txt` — register the new test executable.

**Modified files (B-3, mouse drag):**
- `software/OnSpeed-XPlane-Plugin/src/m5_indexer/IndexerWindow.cpp` — extend `HandleClick` with drag state machine + inverse-projection call.

---

## Sub-PR B-1: Pure Projection Math

### Task 1: Create Indexer3DPlacement.h

**Files:**
- Create: `software/OnSpeed-XPlane-Plugin/src/m5_indexer/Indexer3DPlacement.h`

- [ ] **Step 1: Write the header**

```cpp
// Indexer3DPlacement — pure-function projection of a body-frame anchor
// to screen coordinates, and inverse-projection of a screen point back
// to body frame.  No X-Plane SDK linkage — testable as plain C++.
//
// Conventions:
//   Aircraft body frame:  +X right, +Y up, +Z forward (out the nose).
//   World frame:          OpenGL convention as used by sim/flightmodel/
//                         position/local_{x,y,z}.
//   Camera frame:         +X right, +Y up, -Z forward (OpenGL).
//   Screen pixels:        origin bottom-left, +X right, +Y up.
//
// Angles in degrees on input; converted to radians inside.

#pragma once

namespace onspeed_xplane::indexer {

struct Anchor3D {
    float xMeters = 0.0f;   // +X right of cockpit reference, body frame
    float yMeters = 0.0f;   // +Y up
    float zMeters = 0.0f;   // +Z forward (out the nose)
};

struct AircraftState {
    float xWorld = 0.0f, yWorld = 0.0f, zWorld = 0.0f;
    float pitchDeg = 0.0f;     // theta
    float rollDeg  = 0.0f;     // phi
    float headingDeg = 0.0f;   // psi
};

struct CameraState {
    float xWorld = 0.0f, yWorld = 0.0f, zWorld = 0.0f;
    float pitchDeg = 0.0f;
    float rollDeg  = 0.0f;
    float headingDeg = 0.0f;
    float fovDeg = 70.0f;      // vertical FOV
};

struct ScreenDim {
    int wPx = 1920;
    int hPx = 1080;
};

struct ProjectedQuad {
    float centerX = 0.0f;
    float centerY = 0.0f;
    bool  visible = false;
    float depthMeters = 0.0f;  // camera-frame -Z at the anchor (>0 when in front)
};

// Project a body-frame anchor to screen coordinates.
//
// Returns visible=false when the anchor is behind the camera
// (camera-frame Z >= 0), or projects more than `marginPx` outside
// the screen bounds.  Otherwise returns the screen-pixel center
// of the projection and the depth at which the projection landed
// (used by InverseProject to preserve drag distance).
ProjectedQuad ProjectAnchor(const Anchor3D& anchor,
                            const AircraftState& aircraft,
                            const CameraState& camera,
                            const ScreenDim& screen,
                            float marginPx = 200.0f);

// Inverse-project a screen point at a fixed camera-frame depth back
// to a body-frame anchor.  Used during drag: depth is captured at
// click-time (from ProjectAnchor's depthMeters output) and held
// constant for the drag duration, so a small mouse motion at large
// distance doesn't produce a runaway anchor jump.
//
// depthMeters must be > 0.
Anchor3D InverseProject(float screenX,
                        float screenY,
                        float depthMeters,
                        const AircraftState& aircraft,
                        const CameraState& camera,
                        const ScreenDim& screen);

}  // namespace onspeed_xplane::indexer
```

- [ ] **Step 2: Commit**

```bash
git add software/OnSpeed-XPlane-Plugin/src/m5_indexer/Indexer3DPlacement.h
git commit -m "feat(xplane): add Indexer3DPlacement header for projection math"
```

### Task 2: Create the test file with the first failing test

**Files:**
- Create: `software/OnSpeed-XPlane-Plugin/tests/indexer_3d_placement.cpp`

The test pattern mirrors `tests/auto_setpoints.cpp` — raw `check()` macro, integer failure count, returns nonzero on failure. We add tests one at a time.

- [ ] **Step 1: Write the test scaffold + first test**

```cpp
// indexer_3d_placement — pin the pure-function ProjectAnchor /
// InverseProject behavior used by the mounted-indexer mode.  See
// docs/superpowers/specs/2026-05-18-xplane-3d-indexer-design.md.

#include "../src/m5_indexer/Indexer3DPlacement.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

bool nearly(float a, float b, float tol = 0.5f)
{
    return std::fabs(a - b) <= tol;
}

int failures = 0;

void check(bool cond, const char* what)
{
    if (!cond) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

}  // namespace

int main()
{
    using onspeed_xplane::indexer::Anchor3D;
    using onspeed_xplane::indexer::AircraftState;
    using onspeed_xplane::indexer::CameraState;
    using onspeed_xplane::indexer::InverseProject;
    using onspeed_xplane::indexer::ProjectAnchor;
    using onspeed_xplane::indexer::ProjectedQuad;
    using onspeed_xplane::indexer::ScreenDim;

    // ----------------------------------------------------------------
    // 1. Anchor 30 cm ahead, level aircraft, camera at aircraft origin
    //    looking forward → projects to screen center.
    // ----------------------------------------------------------------
    {
        Anchor3D      anchor{0.0f, 0.0f, 0.30f};
        AircraftState ac{0,0,0, 0,0,0};
        CameraState   cam{0,0,0, 0,0,0, 70.0f};
        ScreenDim     sd{1920, 1080};

        ProjectedQuad pq = ProjectAnchor(anchor, ac, cam, sd);
        check(pq.visible, "ahead-of-camera anchor is visible");
        check(nearly(pq.centerX, 960.0f, 1.0f),
              "ahead-of-camera projects to horizontal screen center");
        check(nearly(pq.centerY, 540.0f, 1.0f),
              "ahead-of-camera projects to vertical screen center");
        check(pq.depthMeters > 0.29f && pq.depthMeters < 0.31f,
              "depth equals the forward offset");
    }

    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("all indexer_3d_placement tests passed\n");
    return EXIT_SUCCESS;
}
```

- [ ] **Step 2: Wire into CMakeLists.txt**

In `software/OnSpeed-XPlane-Plugin/tests/CMakeLists.txt`, after the `auto_setpoints` block (line 85), append:

```cmake
# indexer_3d_placement — pins the projection / inverse-projection math
# used by mounted-mode anchor placement.  Pure C++; no XPLM linkage.
add_executable(indexer_3d_placement
    indexer_3d_placement.cpp
    ../src/m5_indexer/Indexer3DPlacement.cpp)
target_compile_features(indexer_3d_placement PRIVATE cxx_std_17)
target_compile_options(indexer_3d_placement PRIVATE
    -Wall -Wextra -Werror -Wshadow -Wformat=2)
target_include_directories(indexer_3d_placement PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src)

add_test(NAME indexer_3d_placement COMMAND indexer_3d_placement)
```

- [ ] **Step 3: Verify the build fails (Indexer3DPlacement.cpp doesn't exist)**

```bash
cd software/OnSpeed-XPlane-Plugin/tests
cmake -B build -S .
cmake --build build --target indexer_3d_placement 2>&1 | tail -10
```

Expected: linker error or "no rule to make Indexer3DPlacement.cpp" — confirms the test needs the implementation.

### Task 3: Implement ProjectAnchor

**Files:**
- Create: `software/OnSpeed-XPlane-Plugin/src/m5_indexer/Indexer3DPlacement.cpp`

- [ ] **Step 1: Write the implementation**

```cpp
// Indexer3DPlacement.cpp — see Indexer3DPlacement.h.

#include "Indexer3DPlacement.h"

#include <cmath>

namespace onspeed_xplane::indexer {

namespace {

constexpr float kPi = 3.14159265358979323846f;

inline float deg2rad(float d) { return d * (kPi / 180.0f); }

// Rotation matrix in the X-Plane / OpenGL convention.  Aircraft
// attitude is applied as heading (yaw, ψ) → pitch (θ) → roll (φ)
// using right-hand rotations about Y, X, and Z respectively.  We
// avoid building a full 3x3 by inlining the multiplies.
//
// Convention: applies R = Rψ · Rθ · Rφ to (x,y,z) where +Y is up.
struct M3 {
    float r[3][3];
};

M3 BuildAttitudeMatrix(float headingDeg, float pitchDeg, float rollDeg)
{
    const float ch = std::cos(deg2rad(headingDeg));
    const float sh = std::sin(deg2rad(headingDeg));
    const float cp = std::cos(deg2rad(pitchDeg));
    const float sp = std::sin(deg2rad(pitchDeg));
    const float cr = std::cos(deg2rad(rollDeg));
    const float sr = std::sin(deg2rad(rollDeg));

    // R = Ry(heading) * Rx(pitch) * Rz(roll)
    M3 m{};
    m.r[0][0] = ch*cr + sh*sp*sr;
    m.r[0][1] = -ch*sr + sh*sp*cr;
    m.r[0][2] = sh*cp;
    m.r[1][0] = cp*sr;
    m.r[1][1] = cp*cr;
    m.r[1][2] = -sp;
    m.r[2][0] = -sh*cr + ch*sp*sr;
    m.r[2][1] = sh*sr + ch*sp*cr;
    m.r[2][2] = ch*cp;
    return m;
}

inline void Mul(const M3& m, float in[3], float out[3])
{
    out[0] = m.r[0][0]*in[0] + m.r[0][1]*in[1] + m.r[0][2]*in[2];
    out[1] = m.r[1][0]*in[0] + m.r[1][1]*in[1] + m.r[1][2]*in[2];
    out[2] = m.r[2][0]*in[0] + m.r[2][1]*in[1] + m.r[2][2]*in[2];
}

inline M3 Transpose(const M3& m)
{
    M3 t{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            t.r[i][j] = m.r[j][i];
    return t;
}

}  // namespace

ProjectedQuad ProjectAnchor(const Anchor3D& anchor,
                            const AircraftState& aircraft,
                            const CameraState& camera,
                            const ScreenDim& screen,
                            float marginPx)
{
    ProjectedQuad pq;

    // 1. Rotate body-frame anchor into world-aligned frame.
    float pBody[3] = { anchor.xMeters, anchor.yMeters, anchor.zMeters };
    float pWorldRot[3];
    const M3 acR = BuildAttitudeMatrix(aircraft.headingDeg,
                                       aircraft.pitchDeg,
                                       aircraft.rollDeg);
    Mul(acR, pBody, pWorldRot);

    // 2. Translate to world origin.
    const float pWorld[3] = {
        pWorldRot[0] + aircraft.xWorld,
        pWorldRot[1] + aircraft.yWorld,
        pWorldRot[2] + aircraft.zWorld,
    };

    // 3. Translate to camera origin.
    float pCamOffset[3] = {
        pWorld[0] - camera.xWorld,
        pWorld[1] - camera.yWorld,
        pWorld[2] - camera.zWorld,
    };

    // 4. Rotate into camera frame.  Camera attitude rotates the
    //    world's basis vectors INTO camera-frame ones; to rotate a
    //    world-frame vector into the camera frame we apply the inverse
    //    (transpose) of the camera's attitude rotation.
    const M3 camR = BuildAttitudeMatrix(camera.headingDeg,
                                        camera.pitchDeg,
                                        camera.rollDeg);
    const M3 camRT = Transpose(camR);
    float pCam[3];
    Mul(camRT, pCamOffset, pCam);

    // 5. OpenGL camera convention: -Z is forward.  If pCam[2] >= 0,
    //    the anchor is at or behind the camera.
    if (pCam[2] >= 0.0f) {
        pq.visible = false;
        return pq;
    }

    pq.depthMeters = -pCam[2];

    // 6. Pixel focal length from vertical FOV.
    const float fovRad = deg2rad(camera.fovDeg);
    const float focalPx = static_cast<float>(screen.hPx) /
                          (2.0f * std::tan(fovRad * 0.5f));

    // 7. Project to screen.
    pq.centerX = (pCam[0] / -pCam[2]) * focalPx + 0.5f * screen.wPx;
    pq.centerY = (pCam[1] / -pCam[2]) * focalPx + 0.5f * screen.hPx;

    // 8. Off-screen culling.
    if (pq.centerX < -marginPx || pq.centerX > screen.wPx + marginPx ||
        pq.centerY < -marginPx || pq.centerY > screen.hPx + marginPx)
    {
        pq.visible = false;
        return pq;
    }

    pq.visible = true;
    return pq;
}

Anchor3D InverseProject(float screenX,
                        float screenY,
                        float depthMeters,
                        const AircraftState& aircraft,
                        const CameraState& camera,
                        const ScreenDim& screen)
{
    // Reverse of step 7: recover camera-frame X/Y from screen and depth.
    const float fovRad = camera.fovDeg * (kPi / 180.0f);
    const float focalPx = static_cast<float>(screen.hPx) /
                          (2.0f * std::tan(fovRad * 0.5f));

    const float pCam[3] = {
        ((screenX - 0.5f * screen.wPx) / focalPx) * depthMeters,
        ((screenY - 0.5f * screen.hPx) / focalPx) * depthMeters,
        -depthMeters,
    };

    // Reverse of step 4: rotate back to world-aligned frame.
    const M3 camR = BuildAttitudeMatrix(camera.headingDeg,
                                        camera.pitchDeg,
                                        camera.rollDeg);
    float pWorldOffset[3];
    {
        float in[3] = { pCam[0], pCam[1], pCam[2] };
        Mul(camR, in, pWorldOffset);
    }

    // Reverse of step 3: translate to world frame.
    const float pWorld[3] = {
        pWorldOffset[0] + camera.xWorld,
        pWorldOffset[1] + camera.yWorld,
        pWorldOffset[2] + camera.zWorld,
    };

    // Reverse of step 2: translate to aircraft frame.
    const float pAcOffset[3] = {
        pWorld[0] - aircraft.xWorld,
        pWorld[1] - aircraft.yWorld,
        pWorld[2] - aircraft.zWorld,
    };

    // Reverse of step 1: rotate back to body frame using transpose of
    // aircraft attitude.
    const M3 acR = BuildAttitudeMatrix(aircraft.headingDeg,
                                       aircraft.pitchDeg,
                                       aircraft.rollDeg);
    const M3 acRT = Transpose(acR);
    float pBody[3];
    {
        float in[3] = { pAcOffset[0], pAcOffset[1], pAcOffset[2] };
        Mul(acRT, in, pBody);
    }

    return Anchor3D{ pBody[0], pBody[1], pBody[2] };
}

}  // namespace onspeed_xplane::indexer
```

- [ ] **Step 2: Build and run the test**

```bash
cd software/OnSpeed-XPlane-Plugin/tests
cmake --build build --target indexer_3d_placement
./build/indexer_3d_placement
```

Expected output: `all indexer_3d_placement tests passed`

- [ ] **Step 3: Commit**

```bash
git add software/OnSpeed-XPlane-Plugin/src/m5_indexer/Indexer3DPlacement.cpp \
        software/OnSpeed-XPlane-Plugin/tests/indexer_3d_placement.cpp \
        software/OnSpeed-XPlane-Plugin/tests/CMakeLists.txt
git commit -m "feat(xplane): implement ProjectAnchor + InverseProject (1/6 tests)"
```

### Task 4: Add the remaining five tests

Each test is independent; add them in series. Build between each one to catch implementation surprises early.

- [ ] **Step 1: Test 2 — anchor behind camera**

In `indexer_3d_placement.cpp`, after the first test's closing brace, append:

```cpp
    // ----------------------------------------------------------------
    // 2. Anchor behind camera → visible=false.
    // ----------------------------------------------------------------
    {
        Anchor3D      anchor{0.0f, 0.0f, -0.30f};
        AircraftState ac{0,0,0, 0,0,0};
        CameraState   cam{0,0,0, 0,0,0, 70.0f};
        ScreenDim     sd{1920, 1080};

        ProjectedQuad pq = ProjectAnchor(anchor, ac, cam, sd);
        check(!pq.visible, "behind-camera anchor is suppressed");
    }
```

Build and run. Expected: still all-passing.

- [ ] **Step 2: Test 3 — pan view right shifts anchor left**

Append:

```cpp
    // ----------------------------------------------------------------
    // 3. Pan view right 30°: anchor 30cm ahead slides left on screen.
    //    Δscreen_x ≈ screen_w × atan(0.30·sin(30°)/(0.30·cos(30°))) / FOV
    //    Simpler: at small angles the shift is ~ pan_deg / fov_deg ×
    //    screen_w_at_that_depth.  Vertical FOV 70° on a 1920×1080 means
    //    aspect-ratio'd horizontal FOV is ~ 90°.  A 30° pan should move
    //    the projection well off horizontal center; the assertion just
    //    checks direction and bounds.
    // ----------------------------------------------------------------
    {
        Anchor3D      anchor{0.0f, 0.0f, 0.30f};
        AircraftState ac{0,0,0, 0,0,0};
        CameraState   cam{0,0,0, 0,0, 30.0f, 70.0f};
        ScreenDim     sd{1920, 1080};

        ProjectedQuad pq = ProjectAnchor(anchor, ac, cam, sd);
        check(pq.visible, "panned-but-still-in-FOV anchor is visible");
        check(pq.centerX < 960.0f,
              "pan-right shifts the anchor LEFT of screen center");
    }
```

Build and run.

- [ ] **Step 3: Test 4 — bank rotates the anchor**

```cpp
    // ----------------------------------------------------------------
    // 4. Aircraft rolled 45°: an anchor with body-frame +X offset
    //    rotates with the aircraft.  Camera (no roll) sees the anchor
    //    displaced diagonally.
    // ----------------------------------------------------------------
    {
        Anchor3D      anchor{0.10f, 0.0f, 0.30f};
        AircraftState ac{0,0,0, 0, 45.0f, 0};
        CameraState   cam{0,0,0, 0,0,0, 70.0f};
        ScreenDim     sd{1920, 1080};

        ProjectedQuad pq = ProjectAnchor(anchor, ac, cam, sd);
        check(pq.visible, "rolled-aircraft anchor still in FOV");
        // The +X body offset rolls into a mix of +X and +Y world
        // (and thus camera, since no camera roll).  Projection lands
        // right of and above screen center.
        check(pq.centerX > 960.0f, "rolled-aircraft anchor moves right");
        check(pq.centerY > 540.0f, "rolled-aircraft anchor moves up");
    }
```

Build and run.

- [ ] **Step 4: Test 5 — inverse projection round-trip**

```cpp
    // ----------------------------------------------------------------
    // 5. Project then inverse-project: recover the original anchor
    //    within 1 mm.  This is the load-bearing invariant of the
    //    drag math.
    // ----------------------------------------------------------------
    {
        Anchor3D      anchor{0.05f, 0.07f, 0.42f};
        AircraftState ac{100.0f, 50.0f, 200.0f,
                         5.0f, -3.0f, 12.0f};
        CameraState   cam{100.1f, 50.05f, 200.2f,
                          4.5f, -2.5f, 11.0f, 65.0f};
        ScreenDim     sd{2560, 1440};

        ProjectedQuad pq = ProjectAnchor(anchor, ac, cam, sd);
        check(pq.visible, "compound-attitude anchor projects in-frame");

        Anchor3D recovered = InverseProject(pq.centerX, pq.centerY,
                                            pq.depthMeters,
                                            ac, cam, sd);
        check(nearly(recovered.xMeters, anchor.xMeters, 0.001f),
              "round-trip recovers X within 1 mm");
        check(nearly(recovered.yMeters, anchor.yMeters, 0.001f),
              "round-trip recovers Y within 1 mm");
        check(nearly(recovered.zMeters, anchor.zMeters, 0.001f),
              "round-trip recovers Z within 1 mm");
    }
```

Build and run.

- [ ] **Step 5: Test 6 — far-off-screen anchor suppressed**

```cpp
    // ----------------------------------------------------------------
    // 6. Anchor that projects far outside the screen rect (~big +X
    //    body offset, narrow FOV) is suppressed.
    // ----------------------------------------------------------------
    {
        Anchor3D      anchor{5.0f, 0.0f, 0.30f};
        AircraftState ac{0,0,0, 0,0,0};
        CameraState   cam{0,0,0, 0,0,0, 30.0f};  // narrow zoom
        ScreenDim     sd{1920, 1080};

        ProjectedQuad pq = ProjectAnchor(anchor, ac, cam, sd);
        check(!pq.visible, "wildly-off-screen anchor is suppressed");
    }
```

Build and run. Expected: `all indexer_3d_placement tests passed`.

- [ ] **Step 6: Commit**

```bash
git add software/OnSpeed-XPlane-Plugin/tests/indexer_3d_placement.cpp
git commit -m "test(xplane): cover ProjectAnchor edges + InverseProject round-trip"
```

### Task 5: Open PR B-1

- [ ] **Step 1: Push branch**

```bash
git push -u origin spec/xplane-3d-indexer
```

- [ ] **Step 2: Open PR**

```bash
gh pr create --head spec/xplane-3d-indexer --title "feat(xplane): mounted-indexer projection math (1/3)" --body "$(cat <<'EOF'
## Mounted-indexer projection math

Pure-function projection of an aircraft body-frame anchor to screen
coordinates, plus its inverse for drag handling.  No X-Plane SDK
linkage — built and tested as plain C++.

This is the first of three PRs implementing the "Mounted in 3D cockpit"
indexer placement mode (spec: docs/superpowers/specs/2026-05-18-xplane-3d-indexer-design.md).
B-2 adds the integration; B-3 adds mouse drag.

### Changes

- New `Indexer3DPlacement.h/cpp` in `src/m5_indexer/`.
- New `indexer_3d_placement` test exe — six tests covering
  on-axis projection, behind-camera suppression, pan tracking,
  bank tracking, project/inverse-project round-trip within 1 mm,
  and off-screen suppression.
- `tests/CMakeLists.txt` registers the test.

### Testing

```bash
cd software/OnSpeed-XPlane-Plugin/tests
cmake -B build -S .
cmake --build build --target indexer_3d_placement
./build/indexer_3d_placement
```
EOF
)"
```

PR B-1 ends here.

---

## Sub-PR B-2: Integration (placement mode, .prf, menu, geometry tracking)

### Task 6: Extend PersistedState with placement mode and 3D anchor

**Files:**
- Modify: `software/OnSpeed-XPlane-Plugin/src/m5_indexer/IndexerWindow.h`

- [ ] **Step 1: Add the enum and fields to the struct**

Replace lines 49-55 (the `PersistedState` struct) with:

```cpp
// Placement mode.  Persisted as `indexerPlacementMode` integer in the
// .prf.  Old .prfs with `indexerPoppedOut = 1` migrate to PopOut;
// old .prfs with `indexerPoppedOut = 0` migrate to Floating.
enum PlacementMode : int {
    kPlacementFloating  = 0,
    kPlacementPopOut    = 1,
    kPlacementMounted3D = 2,
};

// Persisted indexer state.  Three placement modes are tracked
// independently so toggling between them returns the window to its
// previous position in that mode.  Floating and pop-out coords are
// 2D rects (window geometry); mount3D is a 3D anchor in the aircraft's
// body frame, projected to screen each frame.
struct PersistedState {
    bool visible      = false;
    int  mode         = 0;
    PlacementMode placementMode = kPlacementFloating;
    bool isPoppedOut  = false;     // legacy, kept for migration only
    int  floatLeft = 100, floatTop = 600, floatWidth = 320, floatHeight = 240;
    int  popLeft = 100, popTop = 100, popWidth = 320, popHeight = 240;
    // Aircraft body frame: +X right, +Y up, +Z forward (out the nose).
    // Defaults place the indexer 30 cm forward and 5 cm above the
    // cockpit reference point — visible from a typical eyepoint.
    float mount3D_X = 0.0f;
    float mount3D_Y = 0.05f;
    float mount3D_Z = 0.30f;
};
```

- [ ] **Step 2: Build the plugin to confirm nothing else breaks**

```bash
cd software/OnSpeed-XPlane-Plugin
cmake --build build -j 2>&1 | tail -20
```

Expected: clean build. `IndexerWindow.cpp` doesn't reference `isPoppedOut` anymore — but it does in `ApplyPersistedState`, `GetCurrentState`. Leave those untouched in this step; we update them in Task 8.

- [ ] **Step 3: Commit**

```bash
git add software/OnSpeed-XPlane-Plugin/src/m5_indexer/IndexerWindow.h
git commit -m "feat(xplane): add PlacementMode enum + mount3D to PersistedState"
```

### Task 7: Add .prf schema fields + migration

**Files:**
- Modify: `software/OnSpeed-XPlane-Plugin/src/aoa_audio.cpp`

- [ ] **Step 1: Find the SaveSettings indexer block (around line 534)**

```bash
grep -n "indexerVisible\|indexerPopHeight" software/OnSpeed-XPlane-Plugin/src/aoa_audio.cpp
```

Should show lines ~534-544 emit the existing indexer keys.

- [ ] **Step 2: Add new SaveSettings fields**

After line 544 (the `indexerPopHeight` fprintf), insert:

```cpp
    std::fprintf(fp, "indexerPlacementMode = %d\n",
                 static_cast<int>(indexerSettings.placementMode));
    std::fprintf(fp, "indexerMount3DX = %.6f\n", indexerSettings.mount3D_X);
    std::fprintf(fp, "indexerMount3DY = %.6f\n", indexerSettings.mount3D_Y);
    std::fprintf(fp, "indexerMount3DZ = %.6f\n", indexerSettings.mount3D_Z);
```

- [ ] **Step 3: Find the LoadSettings indexer block (around line 636)**

```bash
grep -n "indexerVisible\")" software/OnSpeed-XPlane-Plugin/src/aoa_audio.cpp
```

- [ ] **Step 4: Add new LoadSettings parser branches**

After line 646 (the `indexerPopHeight` line), insert:

```cpp
        else if (!std::strcmp(key, "indexerPlacementMode")) {
            const int m = std::atoi(val);
            if (m >= 0 && m <= 2) {
                indexerSettings.placementMode =
                    static_cast<onspeed_xplane::indexer::PlacementMode>(m);
            }
        }
        else if (!std::strcmp(key, "indexerMount3DX")) {
            indexerSettings.mount3D_X = static_cast<float>(std::atof(val));
        }
        else if (!std::strcmp(key, "indexerMount3DY")) {
            indexerSettings.mount3D_Y = static_cast<float>(std::atof(val));
        }
        else if (!std::strcmp(key, "indexerMount3DZ")) {
            indexerSettings.mount3D_Z = static_cast<float>(std::atof(val));
        }
```

- [ ] **Step 5: Add migration after the loop**

After LoadSettings finishes reading the file (look for the line just after the parsing while-loop ends — search for `return true;` in LoadSettings, ~line 690 area). Just before the function returns, add:

```cpp
    // Migrate legacy indexerPoppedOut → indexerPlacementMode for
    // .prfs written before the mounted-mode field existed.
    // If the new key was present in the file the loader already set
    // placementMode; this only fires when it wasn't.  We detect that
    // by checking whether placementMode is still its default (Floating).
    // A pop-out user's pre-migration .prf will have isPoppedOut=true
    // but placementMode=Floating; we promote here.
    if (indexerSettings.placementMode ==
            onspeed_xplane::indexer::kPlacementFloating
        && indexerSettings.isPoppedOut)
    {
        indexerSettings.placementMode =
            onspeed_xplane::indexer::kPlacementPopOut;
        XPLMDebugString("FlyOnSpeed: migrated indexerPoppedOut → "
                        "indexerPlacementMode=PopOut\n");
    }
```

- [ ] **Step 6: Build to confirm changes compile**

```bash
cd software/OnSpeed-XPlane-Plugin
cmake --build build -j 2>&1 | tail -20
```

- [ ] **Step 7: Commit**

```bash
git add software/OnSpeed-XPlane-Plugin/src/aoa_audio.cpp
git commit -m "feat(xplane): persist indexerPlacementMode + mount3D in .prf"
```

### Task 8: Add the per-Tick mounted-mode geometry update

**Files:**
- Modify: `software/OnSpeed-XPlane-Plugin/src/m5_indexer/IndexerWindow.cpp`

- [ ] **Step 1: Add includes at the top of IndexerWindow.cpp**

After the existing `#include` block, add:

```cpp
#include "Indexer3DPlacement.h"
```

- [ ] **Step 2: Add datarefs for aircraft/camera state**

Find where the file declares its other static state (around line 101 where `s_window` is declared). Add:

```cpp
// Datarefs for mounted-mode projection.  Looked up lazily on first
// Tick where placementMode==kPlacementMounted3D, then cached.
XPLMDataRef s_drAcLocalX        = nullptr;
XPLMDataRef s_drAcLocalY        = nullptr;
XPLMDataRef s_drAcLocalZ        = nullptr;
XPLMDataRef s_drAcPitch         = nullptr;
XPLMDataRef s_drAcRoll          = nullptr;
XPLMDataRef s_drAcHeading       = nullptr;
XPLMDataRef s_drCamX            = nullptr;
XPLMDataRef s_drCamY            = nullptr;
XPLMDataRef s_drCamZ            = nullptr;
XPLMDataRef s_drCamPitch        = nullptr;
XPLMDataRef s_drCamRoll         = nullptr;
XPLMDataRef s_drCamHeading      = nullptr;
XPLMDataRef s_drFovDeg          = nullptr;
XPLMDataRef s_drViewType        = nullptr;
bool        s_mount3DRefsInit   = false;
```

- [ ] **Step 3: Add the lookup helper**

After the datarefs declaration, add:

```cpp
void EnsureMount3DRefs()
{
    if (s_mount3DRefsInit) return;
    s_drAcLocalX   = XPLMFindDataRef("sim/flightmodel/position/local_x");
    s_drAcLocalY   = XPLMFindDataRef("sim/flightmodel/position/local_y");
    s_drAcLocalZ   = XPLMFindDataRef("sim/flightmodel/position/local_z");
    s_drAcPitch    = XPLMFindDataRef("sim/flightmodel/position/theta");
    s_drAcRoll     = XPLMFindDataRef("sim/flightmodel/position/phi");
    s_drAcHeading  = XPLMFindDataRef("sim/flightmodel/position/psi");
    s_drCamX       = XPLMFindDataRef("sim/graphics/view/pilots_head_x");
    s_drCamY       = XPLMFindDataRef("sim/graphics/view/pilots_head_y");
    s_drCamZ       = XPLMFindDataRef("sim/graphics/view/pilots_head_z");
    s_drCamPitch   = XPLMFindDataRef("sim/graphics/view/pilots_head_the");
    s_drCamRoll    = XPLMFindDataRef("sim/graphics/view/pilots_head_phi");
    s_drCamHeading = XPLMFindDataRef("sim/graphics/view/pilots_head_psi");
    s_drFovDeg     = XPLMFindDataRef("sim/graphics/view/field_of_view_deg");
    s_drViewType   = XPLMFindDataRef("sim/graphics/view/view_type");
    s_mount3DRefsInit = true;
}

bool IsInCockpitView()
{
    if (!s_drViewType) return false;
    const int v = XPLMGetDatai(s_drViewType);
    // 1023 = forward_with_panel (2D cockpit); 1026 = forward 3D cockpit.
    return v == 1023 || v == 1026;
}

// Per-tick projection update for mounted mode.  Sets the existing
// X-Plane window's geometry to the projected screen rect and toggles
// visibility based on view type and whether the anchor is in front
// of the camera.  No-op if the window doesn't exist yet.
void UpdateMounted3DGeometry()
{
    if (!s_window) return;
    EnsureMount3DRefs();

    if (!IsInCockpitView()) {
        XPLMSetWindowIsVisible(s_window, 0);
        return;
    }

    AircraftState ac{};
    ac.xWorld     = s_drAcLocalX   ? XPLMGetDataf(s_drAcLocalX)   : 0.0f;
    ac.yWorld     = s_drAcLocalY   ? XPLMGetDataf(s_drAcLocalY)   : 0.0f;
    ac.zWorld     = s_drAcLocalZ   ? XPLMGetDataf(s_drAcLocalZ)   : 0.0f;
    ac.pitchDeg   = s_drAcPitch    ? XPLMGetDataf(s_drAcPitch)    : 0.0f;
    ac.rollDeg    = s_drAcRoll     ? XPLMGetDataf(s_drAcRoll)     : 0.0f;
    ac.headingDeg = s_drAcHeading  ? XPLMGetDataf(s_drAcHeading)  : 0.0f;

    CameraState cam{};
    cam.xWorld     = s_drCamX       ? XPLMGetDataf(s_drCamX)       : 0.0f;
    cam.yWorld     = s_drCamY       ? XPLMGetDataf(s_drCamY)       : 0.0f;
    cam.zWorld     = s_drCamZ       ? XPLMGetDataf(s_drCamZ)       : 0.0f;
    cam.pitchDeg   = s_drCamPitch   ? XPLMGetDataf(s_drCamPitch)   : 0.0f;
    cam.rollDeg    = s_drCamRoll    ? XPLMGetDataf(s_drCamRoll)    : 0.0f;
    cam.headingDeg = s_drCamHeading ? XPLMGetDataf(s_drCamHeading) : 0.0f;
    cam.fovDeg     = s_drFovDeg     ? XPLMGetDataf(s_drFovDeg)     : 70.0f;

    Anchor3D anchor{s_persisted.mount3D_X,
                    s_persisted.mount3D_Y,
                    s_persisted.mount3D_Z};

    ScreenDim sd{};
    XPLMGetScreenSize(&sd.wPx, &sd.hPx);

    const ProjectedQuad pq = ProjectAnchor(anchor, ac, cam, sd);
    if (!pq.visible) {
        XPLMSetWindowIsVisible(s_window, 0);
        return;
    }

    const int halfW = s_persisted.floatWidth  / 2;
    const int halfH = s_persisted.floatHeight / 2;
    const int cx    = static_cast<int>(pq.centerX);
    const int cy    = static_cast<int>(pq.centerY);

    XPLMSetWindowGeometry(s_window,
                          cx - halfW,         // left
                          cy + halfH,         // top   (Y-up in X-Plane)
                          cx + halfW,         // right
                          cy - halfH);        // bottom
    XPLMSetWindowIsVisible(s_window, 1);
}
```

Inject the `using` clauses near the top of the anonymous namespace so the types resolve:

```cpp
using onspeed_xplane::indexer::Anchor3D;
using onspeed_xplane::indexer::AircraftState;
using onspeed_xplane::indexer::CameraState;
using onspeed_xplane::indexer::ProjectAnchor;
using onspeed_xplane::indexer::ProjectedQuad;
using onspeed_xplane::indexer::ScreenDim;
```

- [ ] **Step 4: Call `UpdateMounted3DGeometry` from Tick**

Find the existing `Tick()` function (look for `void Tick()` in IndexerWindow.cpp). After the M5 firmware's `loop()` is called, before Tick returns, append:

```cpp
    if (s_persisted.placementMode ==
            onspeed_xplane::indexer::kPlacementMounted3D)
    {
        UpdateMounted3DGeometry();
    }
```

- [ ] **Step 5: Build and verify**

```bash
cd software/OnSpeed-XPlane-Plugin
cmake --build build -j 2>&1 | tail -20
```

Expected: clean build. The mounted-mode code path doesn't execute yet (no menu item to switch placement mode), so this is purely a compile check.

- [ ] **Step 6: Commit**

```bash
git add software/OnSpeed-XPlane-Plugin/src/m5_indexer/IndexerWindow.cpp
git commit -m "feat(xplane): per-Tick projection-driven geometry for mounted mode"
```

### Task 9: Add window decoration switch on placement-mode change

The decoration (`xplm_WindowDecorationRoundRectangle` vs `xplm_WindowDecorationSelfDecorated`) is set at `XPLMCreateWindowEx` time and can't be changed afterwards. To switch between modes we destroy and recreate the window.

**Files:**
- Modify: `software/OnSpeed-XPlane-Plugin/src/m5_indexer/IndexerWindow.cpp`

- [ ] **Step 1: Extract window creation into a parameterized helper**

Find `CreateXPlaneWindow` (around line 478). Modify it to take a decoration parameter:

```cpp
bool CreateXPlaneWindow(XPLMWindowDecoration decoration)
{
    // ... existing scratch buffer setup ...

    XPLMCreateWindow_t params = {};
    params.structSize             = sizeof(params);
    params.left                   = s_persisted.floatLeft;
    params.top                    = s_persisted.floatTop;
    params.right                  = s_persisted.floatLeft + s_persisted.floatWidth;
    params.bottom                 = s_persisted.floatTop  - s_persisted.floatHeight;
    params.visible                = 0;
    params.drawWindowFunc         = DrawWindow;
    params.handleMouseClickFunc   = HandleClick;
    params.handleRightClickFunc   = HandleClick;
    params.handleKeyFunc          = HandleKey;
    params.handleCursorFunc       = HandleCursor;
    params.handleMouseWheelFunc   = HandleWheel;
    params.refcon                 = nullptr;
    params.decorateAsFloatingWindow = decoration;
    params.layer                  = xplm_WindowLayerFloatingWindows;
    s_window = XPLMCreateWindowEx(&params);
    if (!s_window) return false;

    XPLMSetWindowTitle(s_window, "OnSpeed Indexer");
    return true;
}
```

And update the existing single caller inside `InstallPanelAndRunSetup` (or wherever it's called — `grep -n "CreateXPlaneWindow" IndexerWindow.cpp`) to pass `xplm_WindowDecorationRoundRectangle` by default.

- [ ] **Step 2: Add a recreate-with-decoration entry point**

After `CreateXPlaneWindow`, add:

```cpp
void RecreateWindowWithDecoration(XPLMWindowDecoration decoration)
{
    if (s_window) {
        XPLMDestroyWindow(s_window);
        s_window = nullptr;
    }
    CreateXPlaneWindow(decoration);
}
```

Add the declaration to `IndexerWindow.h` after `ApplyPersistedState`:

```cpp
// Destroy and recreate the X-Plane window with a different decoration.
// Used when switching between mounted (self-decorated) and floating/
// pop-out (RoundRectangle).  No-op if no window exists yet.  Must be
// called from a flight-loop callback context (same restriction as
// ApplyPersistedState).
void RecreateWindowWithDecoration(int decoration);
```

(Use `int` in the header to avoid leaking the XPLM type into a header that's included from non-XPLM files.)

- [ ] **Step 3: Apply the right decoration in `ApplyPersistedState`**

In `ApplyPersistedState` (around line 836), at the very beginning of the function after the existing clamp block, add a decoration switch:

```cpp
    const XPLMWindowDecoration desiredDecoration =
        (st.placementMode ==
            onspeed_xplane::indexer::kPlacementMounted3D)
        ? xplm_WindowDecorationSelfDecorated
        : xplm_WindowDecorationRoundRectangle;

    // If window already exists with wrong decoration, recreate.  X-Plane
    // SDK provides no in-place decoration setter.
    if (s_window) {
        // We can't query the current decoration from the SDK.  Track
        // it locally via a static.
        static XPLMWindowDecoration s_currentDecoration =
            xplm_WindowDecorationRoundRectangle;
        if (s_currentDecoration != desiredDecoration) {
            RecreateWindowWithDecoration(desiredDecoration);
            s_currentDecoration = desiredDecoration;
        }
    }
```

(Static-local is acceptable here because the function is called from one thread only — the flight-loop callback.)

- [ ] **Step 4: Build and verify**

```bash
cd software/OnSpeed-XPlane-Plugin
cmake --build build -j 2>&1 | tail -20
```

- [ ] **Step 5: Commit**

```bash
git add software/OnSpeed-XPlane-Plugin/src/m5_indexer/IndexerWindow.cpp \
        software/OnSpeed-XPlane-Plugin/src/m5_indexer/IndexerWindow.h
git commit -m "feat(xplane): recreate window with self-decorated chrome for mounted mode"
```

### Task 10: Add the menu submenu and dispatch

**Files:**
- Modify: `software/OnSpeed-XPlane-Plugin/src/aoa_audio.cpp`

- [ ] **Step 1: Add menu submenu under "Fly On Speed"**

Find the menu construction block (around line 2563). After `IndexerToggle` (line 2573), insert:

```cpp
    // Placement mode submenu — three radio-style items.  The
    // AudioMenuHandler dispatches on the tag string.  Bullet markers
    // (● / ○) are added at runtime by RefreshPlacementMenu().
    XPLMAppendMenuSeparator(menuId);
    int placementItem = XPLMAppendMenuItem(menuId, "Indexer position",
                                           nullptr, 1);
    g_PlacementMenuId = XPLMCreateMenu("Indexer position", menuId,
                                       placementItem,
                                       AudioMenuHandler, nullptr);
    XPLMAppendMenuItem(g_PlacementMenuId, "Floating window",
        static_cast<void*>(const_cast<char*>("PlacementFloating")), 1);
    XPLMAppendMenuItem(g_PlacementMenuId, "Popped-out OS window",
        static_cast<void*>(const_cast<char*>("PlacementPopOut")), 1);
    XPLMAppendMenuItem(g_PlacementMenuId, "Mounted in 3D cockpit",
        static_cast<void*>(const_cast<char*>("PlacementMounted3D")), 1);
    RefreshPlacementMenu();
```

- [ ] **Step 2: Add the menu-id static near the top of aoa_audio.cpp**

Find where `g_SerialMenuId` is declared (search `g_SerialMenuId`). Add nearby:

```cpp
static XPLMMenuID g_PlacementMenuId = nullptr;
```

- [ ] **Step 3: Add RefreshPlacementMenu function**

Before `AudioMenuHandler`, add:

```cpp
// Update menu items' checkmarks to reflect the active placement
// mode.  Called whenever placementMode changes.
static void RefreshPlacementMenu()
{
    if (!g_PlacementMenuId) return;
    using onspeed_xplane::indexer::kPlacementFloating;
    using onspeed_xplane::indexer::kPlacementPopOut;
    using onspeed_xplane::indexer::kPlacementMounted3D;
    XPLMCheckMenuItem(g_PlacementMenuId, 0,
        indexerSettings.placementMode == kPlacementFloating
            ? xplm_Menu_Checked : xplm_Menu_Unchecked);
    XPLMCheckMenuItem(g_PlacementMenuId, 1,
        indexerSettings.placementMode == kPlacementPopOut
            ? xplm_Menu_Checked : xplm_Menu_Unchecked);
    XPLMCheckMenuItem(g_PlacementMenuId, 2,
        indexerSettings.placementMode == kPlacementMounted3D
            ? xplm_Menu_Checked : xplm_Menu_Unchecked);
}
```

- [ ] **Step 4: Handle the dispatch tags in AudioMenuHandler**

In `AudioMenuHandler` (around line 1981), after the existing `IndexerToggle` block (line 2014), add:

```cpp
    auto applyMode = [](onspeed_xplane::indexer::PlacementMode mode) {
        indexerSettings.placementMode = mode;
        // ApplyPersistedState handles decoration + visibility/geometry
        // changes.  Schedule it for the next flight-loop tick via the
        // existing s_indexerRestorePending hook.
        s_indexerRestorePending = true;
        RefreshPlacementMenu();
        SaveSettings();
    };

    if (!strcmp(tag, "PlacementFloating")) {
        applyMode(onspeed_xplane::indexer::kPlacementFloating);
        return;
    }
    if (!strcmp(tag, "PlacementPopOut")) {
        applyMode(onspeed_xplane::indexer::kPlacementPopOut);
        return;
    }
    if (!strcmp(tag, "PlacementMounted3D")) {
        applyMode(onspeed_xplane::indexer::kPlacementMounted3D);
        return;
    }
```

- [ ] **Step 5: Wire decoration + positioning-mode through ApplyPersistedState**

In `IndexerWindow.cpp`, in `ApplyPersistedState` (around line 875 where the existing `if (st.isPoppedOut)` branch runs), replace the `isPoppedOut` branch with a 3-way switch on `placementMode`:

```cpp
    // Window exists.  Apply positioning mode + geometry per the
    // active placement mode.
    using onspeed_xplane::indexer::kPlacementFloating;
    using onspeed_xplane::indexer::kPlacementPopOut;
    using onspeed_xplane::indexer::kPlacementMounted3D;

    if (st.placementMode == kPlacementPopOut) {
        XPLMSetWindowPositioningMode(s_window, xplm_WindowPopOut, -1);
        XPLMSetWindowGeometryOS(s_window,
                                st.popLeft,
                                st.popTop,
                                st.popLeft + st.popWidth,
                                st.popTop  - st.popHeight);
    } else if (st.placementMode == kPlacementFloating) {
        XPLMSetWindowPositioningMode(s_window, xplm_WindowPositionFree, -1);
        XPLMSetWindowGeometry(s_window,
                              st.floatLeft,
                              st.floatTop,
                              st.floatLeft + st.floatWidth,
                              st.floatTop  - st.floatHeight);
    } else {
        // Mounted3D: per-Tick projection sets geometry.  Just clear
        // any pop-out state so it lives in the floating coord space.
        XPLMSetWindowPositioningMode(s_window, xplm_WindowPositionFree, -1);
    }
```

- [ ] **Step 6: Build**

```bash
cd software/OnSpeed-XPlane-Plugin
cmake --build build -j 2>&1 | tail -20
```

- [ ] **Step 7: Install and smoke-test**

```bash
./scripts/install_dev.sh "/Users/sritchie/X-Plane 12"
```

Then start X-Plane manually, load the stock C172 SP, and:

1. Plugins → Fly On Speed → Indexer position → see the three radio items, "Floating window" checked by default.
2. Click "Mounted in 3D cockpit". Expected: window decoration disappears; window jumps to the projected position (initially in front of the cockpit eyepoint, but may not be visible until you find the right view). Switch view modes to find it.
3. Click "Floating window" again. Window returns with chrome.

If the window vanishes entirely in mounted mode, check `Log.txt` for `FlyOnSpeed: ApplyPersistedState` lines — visibility may be 0 due to the view-type check or projection-behind-camera path.

- [ ] **Step 8: Commit**

```bash
git add software/OnSpeed-XPlane-Plugin/src/aoa_audio.cpp \
        software/OnSpeed-XPlane-Plugin/src/m5_indexer/IndexerWindow.cpp
git commit -m "feat(xplane): menu submenu + 3-way placement-mode dispatch"
```

### Task 11: Open PR B-2

- [ ] **Step 1: Push and open PR**

```bash
git push
gh pr create --head spec/xplane-3d-indexer --title "feat(xplane): mounted-indexer integration (2/3)" --body "$(cat <<'EOF'
## Mounted-indexer integration

Wires the projection math from B-1 into the X-Plane plugin: new
`PlacementMode` enum, per-aircraft .prf persistence with backward
compatibility for old `indexerPoppedOut` keys, menu submenu, and
per-Tick window-geometry update for mounted mode.

This is the second of three PRs (spec: docs/superpowers/specs/2026-05-18-xplane-3d-indexer-design.md).
B-3 adds mouse drag — for now, the anchor is fixed at the default
(0, 0.05, 0.3) until B-3 lands.

### Changes

- `PersistedState` grows a `PlacementMode` enum + three `mount3D_X/Y/Z`
  floats.  Default placement is Floating (unchanged behavior).
- `.prf` gains `indexerPlacementMode`, `indexerMount3DX/Y/Z` keys.
  Old `.prf`s with `indexerPoppedOut=1` migrate to PopOut.
- Menu: new "Indexer position" submenu with three radio items.
- Each Tick, in mounted mode: read aircraft + camera datarefs, project
  the 3D anchor to screen, `XPLMSetWindowGeometry` to land the window
  at the projection.  View-type check (3D cockpit only) and
  behind-camera check hide the window otherwise.
- Window decoration changes (RoundRectangle ↔ SelfDecorated) require
  destroy+recreate — done in `ApplyPersistedState`.

### Testing

Manual smoke test against the stock C172 SP.  The default mounted
anchor (0, 0.05, 0.3 in body frame) puts the indexer 30 cm forward
and 5 cm up from the pilot eyepoint — visible from the default 3D
cockpit camera position.
EOF
)"
```

PR B-2 ends here.

---

## Sub-PR B-3: Mouse Drag

### Task 12: Extend HandleClick with drag state machine

**Files:**
- Modify: `software/OnSpeed-XPlane-Plugin/src/m5_indexer/IndexerWindow.cpp`

- [ ] **Step 1: Add drag state to the file's anonymous namespace**

Near `s_persisted` (around line 183), add:

```cpp
struct DragState {
    bool  active        = false;     // currently dragging?
    int   downX         = 0;         // mouse pos at MouseDown
    int   downY         = 0;
    int   offsetX       = 0;         // (downX - projectedCenterX)
    int   offsetY       = 0;
    float depthMeters   = 0.0f;      // camera-frame depth at click time
    AircraftState ac{};              // captured at click time
    CameraState   cam{};
    ScreenDim     sd{};
};
DragState s_drag;

constexpr int kDragThreshold = 5;    // pixels of motion to enter drag
```

- [ ] **Step 2: Extend HandleClick**

Replace the existing `HandleClick` (around line 341) with:

```cpp
int HandleClick(XPLMWindowID, int x, int y,
                XPLMMouseStatus status, void* /*refcon*/)
{
    if (!s_window) return 1;

    // Only intercept drag for mounted mode.  Floating/pop-out keep
    // legacy behavior: any MouseDown cycles modes if inside the
    // MODE button rect.
    const bool mounted = (s_persisted.placementMode ==
        onspeed_xplane::indexer::kPlacementMounted3D);

    if (!mounted) {
        if (status != xplm_MouseDown) return 1;
        int wL, wT, wR, wB;
        XPLMGetWindowGeometry(s_window, &wL, &wT, &wR, &wB);
        int bL, bT, bR, bB;
        if (!ComputeButtonRect(wL, wT, wR, wB, &bL, &bT, &bR, &bB))
            return 1;
        if (x >= bL && x <= bR && y >= bB && y <= bT) {
            const int next = (static_cast<int>(displayType) + 1) % 5;
            displayType = static_cast<int16_t>(next);
        }
        return 1;
    }

    // Mounted-mode click handling.
    if (status == xplm_MouseDown) {
        s_drag.active = false;
        s_drag.downX = x;
        s_drag.downY = y;

        // Capture aircraft + camera state at click time; reused for
        // drag inverse-projection so the math is consistent across
        // the drag duration even if the pilot's view jitters slightly.
        EnsureMount3DRefs();
        s_drag.ac.xWorld     = s_drAcLocalX   ? XPLMGetDataf(s_drAcLocalX)   : 0;
        s_drag.ac.yWorld     = s_drAcLocalY   ? XPLMGetDataf(s_drAcLocalY)   : 0;
        s_drag.ac.zWorld     = s_drAcLocalZ   ? XPLMGetDataf(s_drAcLocalZ)   : 0;
        s_drag.ac.pitchDeg   = s_drAcPitch    ? XPLMGetDataf(s_drAcPitch)    : 0;
        s_drag.ac.rollDeg    = s_drAcRoll     ? XPLMGetDataf(s_drAcRoll)     : 0;
        s_drag.ac.headingDeg = s_drAcHeading  ? XPLMGetDataf(s_drAcHeading)  : 0;
        s_drag.cam.xWorld     = s_drCamX       ? XPLMGetDataf(s_drCamX)       : 0;
        s_drag.cam.yWorld     = s_drCamY       ? XPLMGetDataf(s_drCamY)       : 0;
        s_drag.cam.zWorld     = s_drCamZ       ? XPLMGetDataf(s_drCamZ)       : 0;
        s_drag.cam.pitchDeg   = s_drCamPitch   ? XPLMGetDataf(s_drCamPitch)   : 0;
        s_drag.cam.rollDeg    = s_drCamRoll    ? XPLMGetDataf(s_drCamRoll)    : 0;
        s_drag.cam.headingDeg = s_drCamHeading ? XPLMGetDataf(s_drCamHeading) : 0;
        s_drag.cam.fovDeg     = s_drFovDeg     ? XPLMGetDataf(s_drFovDeg)     : 70.0f;
        XPLMGetScreenSize(&s_drag.sd.wPx, &s_drag.sd.hPx);

        // Project the current anchor to find depth + screen center.
        const Anchor3D a{s_persisted.mount3D_X,
                         s_persisted.mount3D_Y,
                         s_persisted.mount3D_Z};
        const ProjectedQuad pq = ProjectAnchor(a, s_drag.ac, s_drag.cam,
                                                s_drag.sd);
        s_drag.depthMeters = pq.depthMeters;
        s_drag.offsetX = x - static_cast<int>(pq.centerX);
        s_drag.offsetY = y - static_cast<int>(pq.centerY);
        return 1;
    }

    if (status == xplm_MouseDrag) {
        if (!s_drag.active) {
            if (std::abs(x - s_drag.downX) + std::abs(y - s_drag.downY) <
                    kDragThreshold) {
                return 1;
            }
            s_drag.active = true;
        }
        const float newCx = static_cast<float>(x - s_drag.offsetX);
        const float newCy = static_cast<float>(y - s_drag.offsetY);
        const Anchor3D newAnchor =
            onspeed_xplane::indexer::InverseProject(
                newCx, newCy, s_drag.depthMeters,
                s_drag.ac, s_drag.cam, s_drag.sd);
        s_persisted.mount3D_X = newAnchor.xMeters;
        s_persisted.mount3D_Y = newAnchor.yMeters;
        s_persisted.mount3D_Z = newAnchor.zMeters;
        s_dirty = true;
        return 1;
    }

    if (status == xplm_MouseUp) {
        if (!s_drag.active) {
            // Tap, not drag.  Same MODE-cycle behavior as floating mode.
            int wL, wT, wR, wB;
            XPLMGetWindowGeometry(s_window, &wL, &wT, &wR, &wB);
            int bL, bT, bR, bB;
            if (ComputeButtonRect(wL, wT, wR, wB, &bL, &bT, &bR, &bB)
                && x >= bL && x <= bR && y >= bB && y <= bT)
            {
                const int next = (static_cast<int>(displayType) + 1) % 5;
                displayType = static_cast<int16_t>(next);
            }
        }
        s_drag.active = false;
        return 1;
    }

    return 1;
}
```

- [ ] **Step 3: Build**

```bash
cd software/OnSpeed-XPlane-Plugin
cmake --build build -j 2>&1 | tail -20
```

- [ ] **Step 4: Install and smoke-test**

```bash
./scripts/install_dev.sh "/Users/sritchie/X-Plane 12"
```

Start X-Plane, load the C172 SP, switch to 3D cockpit view, set Indexer position to "Mounted in 3D cockpit", then:

1. **Tap test** — click the indexer body once. MODE should cycle (0 → 1 → 2 → ...).
2. **Drag test** — click-and-drag the indexer to a different spot on the glareshield. It should follow the mouse. Release; it should stay.
3. **Persistence test** — after the drag, wait ~2 s for the 1 Hz save flush, then check the `.prf` file:

   ```bash
   grep "indexerMount3D" "/Users/sritchie/X-Plane 12/Output/preferences/AOA-Tone-FlyOnSpeed-Cessna_172SP*.prf"
   ```

   The three `indexerMount3DX/Y/Z` values should reflect the new position.

4. **View-pan test** — pan view left-right with mouse. The indexer should track its anchor (move opposite to pan direction, like a real instrument fixed in the cockpit).

5. **Cold-restore test** — quit X-Plane, restart, load the same aircraft. Indexer should return to the dragged position.

If any test fails, check `Log.txt` for `FlyOnSpeed:` lines and the actual mount3D values being saved/loaded.

- [ ] **Step 5: Commit**

```bash
git add software/OnSpeed-XPlane-Plugin/src/m5_indexer/IndexerWindow.cpp
git commit -m "feat(xplane): mouse drag in mounted mode places anchor"
```

### Task 13: Update end-user docs

**Files:**
- Modify: `docs/site/docs/xplane/indexer.md`

- [ ] **Step 1: Find the "Window appearance and behavior" section**

```bash
grep -n "appearance and behavior\|Pop-out mode\|Sticky position" docs/site/docs/xplane/indexer.md
```

- [ ] **Step 2: Add the new section after pop-out**

After the "Pop-out mode (multi-monitor)" section, add:

```markdown
### Mounted in 3D cockpit

For sim setups where you fly in 3D-cockpit view, you can mount the
indexer to a fixed point in the cockpit instead of leaving it
floating on the screen.  When you pan the view, the indexer slides
like a real instrument bolted to the glareshield.

To enable:

1. **Plugins → Fly On Speed → Indexer position → Mounted in 3D cockpit**.
2. The window loses its chrome (no titlebar, no close button) and
   jumps to a default spot 30 cm forward and 5 cm above the pilot
   eyepoint — visible from the default 3D cockpit camera.
3. **Click and drag** the indexer to move it to a different spot on
   the glareshield.  A short tap (no drag) still cycles the indexer
   mode.

The position is stored per aircraft in the same `.prf` file as the
audio settings — the indexer remembers where you placed it for each
airframe.

Mounted mode hides the indexer in external views (chase, runway,
tower, etc.) since the anchor is a cockpit-frame point.  Switch
back to 3D cockpit and the indexer reappears at the saved spot.

To return to a floating window, pick **Floating window** or
**Popped-out OS window** in the same submenu.
```

- [ ] **Step 3: Build the docs site locally and verify**

```bash
cd docs/site
uv run --with "mkdocs>=1.6,<2" --with mkdocs-material mkdocs serve
```

Open `http://127.0.0.1:8000/xplane/indexer/`. Verify the new section renders.

- [ ] **Step 4: Commit**

```bash
git add docs/site/docs/xplane/indexer.md
git commit -m "docs(xplane): document mounted-in-3D-cockpit placement"
```

### Task 14: Open PR B-3

- [ ] **Step 1: Push and open PR**

```bash
git push
gh pr create --head spec/xplane-3d-indexer --title "feat(xplane): mounted-indexer mouse drag + docs (3/3)" --body "$(cat <<'EOF'
## Mounted-indexer mouse drag

Extends `HandleClick` with a drag state machine: in mounted mode,
click+drag inverse-projects the new screen position back to a
body-frame anchor and persists it.  Short taps still cycle the
indexer mode.

Closes the three-PR mounted-indexer series:

- B-1: pure projection math (PR #...).
- B-2: integration — placement mode, .prf, menu, geometry tracking
  (PR #...).
- B-3: mouse drag + end-user docs (this PR).

### Changes

- `HandleClick` recognizes `xplm_MouseDown` / `xplm_MouseDrag` /
  `xplm_MouseUp` in mounted mode; falls back to the legacy MODE-cycle
  in floating/pop-out modes.
- 5 px deadband distinguishes tap from drag.
- Aircraft + camera state are captured at MouseDown and held through
  the drag — anchor stays at the click-time depth so a small mouse
  motion doesn't fling the anchor to the horizon.
- `docs/site/docs/xplane/indexer.md` documents the new mode.

### Testing

Manual: tap MODE, drag to reposition, pan view to verify tracking,
quit + restart for cold-restore.  Detailed steps in the spec.
EOF
)"
```

---

## Self-Review

**1. Spec coverage:**

- ✓ Deliverable B — mounted indexer.  Covered by Tasks 1-13.
- ✓ Per-aircraft persistence — extended in Task 7 with mount3D fields.
- ✓ Migration of `indexerPoppedOut` → `indexerPlacementMode` — Task 7 Step 5.
- ✓ Menu submenu — Task 10.
- ✓ View-type check (cockpit only) — Task 8 `IsInCockpitView()`.
- ✓ Behind-camera suppression — Task 8 `UpdateMounted3DGeometry`.
- ✓ Drag deadband — Task 12 `kDragThreshold = 5`.
- ✓ Drag depth pinning — Task 12 captures `depthMeters` at MouseDown.
- ✓ Decoration switch — Task 9 destroy+recreate path.
- ✓ Unit tests for projection — Tasks 2-4.
- ✓ End-user docs — Task 13.
- ✗ Spec deliverable A (verification) — owned by user, not in this plan.  Side-task (legacy JSON drop) already done in the prior conversation.
- ✗ Spec deliverable C (L/D wizard) — dropped, per user.

**2. Placeholder scan:** No "TBD", "TODO", or "fill in" markers.

**3. Type consistency:**

- `PlacementMode` enum used consistently (Tasks 6, 7, 8, 10, 12).
- `Anchor3D`, `AircraftState`, `CameraState`, `ScreenDim`, `ProjectedQuad` types match between header (Task 1) and uses (Tasks 4, 8, 12).
- `RecreateWindowWithDecoration` declared with `int` parameter in header (avoids leaking XPLM type) but called with `XPLMWindowDecoration` enum in .cpp — implicit conversion handles this.
- `s_dirty` is the existing dirty flag (in IndexerWindow.cpp, line ~187); Task 12 sets it on drag.  Verified by grep against the existing file.
