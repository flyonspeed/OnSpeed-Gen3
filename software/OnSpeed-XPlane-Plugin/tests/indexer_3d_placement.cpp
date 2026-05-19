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

    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("all indexer_3d_placement tests passed\n");
    return EXIT_SUCCESS;
}
