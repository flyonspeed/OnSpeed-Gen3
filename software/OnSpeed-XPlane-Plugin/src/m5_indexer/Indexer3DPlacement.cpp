// Indexer3DPlacement.cpp — see Indexer3DPlacement.h.

#include "Indexer3DPlacement.h"

#include <cmath>

namespace onspeed_xplane::indexer {

namespace {

constexpr float kPi = 3.14159265358979323846f;

inline float deg2rad(float d) { return d * (kPi / 180.0f); }

// Rotation matrix for X-Plane attitude conventions.
//
// X-Plane: psi clockwise-from-north, theta nose-up, phi right-wing-down.
// We translate into a standard right-hand rotation matrix:
//   M = Ry(-psi) · Rx(theta) · Rz(-phi)
// applied to body-frame vectors to produce world-frame vectors.
//
// Body and world frames coincide at identity attitudes (both are
// +X east, +Y up, +Z south in world / +X right, +Y up, +Z back in body).
struct M3 {
    float r[3][3];
};

M3 BuildAttitudeMatrix(float headingDeg, float pitchDeg, float rollDeg)
{
    // X-Plane attitudes:
    //   psi (heading) is clockwise-from-north positive → negate for
    //     right-hand Y-rotation.
    //   theta (pitch) is nose-up positive → matches right-hand
    //     X-rotation directly.
    //   phi (roll) is right-wing-down positive → negate for right-hand
    //     Z-rotation.
    const float ch = std::cos(deg2rad(-headingDeg));
    const float sh = std::sin(deg2rad(-headingDeg));
    const float cp = std::cos(deg2rad(pitchDeg));
    const float sp = std::sin(deg2rad(pitchDeg));
    const float cr = std::cos(deg2rad(-rollDeg));
    const float sr = std::sin(deg2rad(-rollDeg));

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
