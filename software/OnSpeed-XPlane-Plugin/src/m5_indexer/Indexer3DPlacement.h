// Indexer3DPlacement — pure-function projection of a body-frame anchor
// to screen coordinates, and inverse-projection of a screen point back
// to body frame.  No X-Plane SDK linkage — testable as plain C++.
//
// Conventions:
//   Aircraft body frame:  +X right, +Y up, +Z BACK (-Z forward / out
//                         the nose).  This matches X-Plane's structural
//                         reference frame and the camera convention
//                         (OpenGL -Z forward).  At zero attitudes,
//                         body frame aligns with the world frame.
//   World frame:          X-Plane OpenGL local: +X east, +Y up,
//                         +Z south.  Same axes as
//                         sim/flightmodel/position/local_{x,y,z}.
//   Camera frame:         +X right, +Y up, -Z forward (OpenGL).
//                         Same as body frame.
//   Screen pixels:        origin bottom-left, +X right, +Y up.
//
// X-Plane attitude conventions (same for aircraft and camera):
//   psi   (heading): clockwise from north positive.
//   theta (pitch):   nose-up positive.
//   phi   (roll):    right-wing-down positive.
//
// BuildAttitudeMatrix translates these into a standard right-hand
// rotation matrix by negating psi and phi inside the trig functions.
//
// Angles in degrees on input; converted to radians inside.

#pragma once

namespace onspeed_xplane::indexer {

struct Anchor3D {
    float xMeters = 0.0f;   // +X right of cockpit reference, body frame
    float yMeters = 0.0f;   // +Y up
    float zMeters = 0.0f;   // +Z BACK (-Z out the nose)
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
    float fovDeg = 70.0f;      // vertical FOV, expected in (0, 180);
                               // values outside this range cause
                               // ProjectAnchor to return visible=false
                               // and InverseProject to return zero.
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
