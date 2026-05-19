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
