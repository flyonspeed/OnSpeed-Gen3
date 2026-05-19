// IndexerWindow — host the OnSpeed M5 indexer renderer inside an
// X-Plane window.  See docs/superpowers/specs/2026-05-02-xplane-
// embedded-m5-indexer-design.md for the full architecture.
//
// Lifecycle:
//   Init()  — initialize SDL (timer only), install our software
//             panel, run the M5 firmware's setup(), create the X-Plane
//             window + GL texture.
//   Tick()  — called per X-Plane flight loop.  Builds a display-serial
//             frame from datarefs, feeds it through InjectSerialByte,
//             calls the M5's loop().  Renderer writes RGB565 into our
//             panel's framebuffer.
//   draw()  — XPLM window draw callback.  Reads the framebuffer,
//             converts to RGBA8888, uploads to GL texture, draws a
//             quad covering the window.
//   Shutdown()  — destroy the X-Plane window + texture, release SDL.

#pragma once

#include <string>

namespace onspeed_xplane::indexer {

// Idempotent: safe to call multiple times.  Returns false if SDL or
// the M5 firmware setup failed.
bool Init();

// Per-flight-loop tick.  Reads X-Plane datarefs, builds a display-
// serial frame, feeds it through the M5 parser, calls the M5
// renderer's loop().  No-op if Init() failed.
void Tick();

// Returns true iff the indexer window is currently visible.
bool IsVisible();

// Show/hide the indexer window.  Used by the menu toggle.
void Show();
void Hide();

// Set the M5 display mode (0..4).  Writes through to the M5 firmware's
// global displayType.  Used by the menu mode-selector items.
void SetMode(int mode);
int  GetMode();

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
    // Aircraft body frame: +X right, +Y up, +Z BACK (-Z forward / out
    // the nose).  Matches Indexer3DPlacement.h's convention.
    //
    // Sentinel: mount3DSeeded=false → on first ApplyPersistedState in
    // mounted mode, seed from pilots_head_x/y/z + 30 cm forward.  After
    // the seed, the bool is set true and the actual mount3D_* are used.
    // This lets the indexer land in front of the pilot for any aircraft
    // without per-aircraft hand-tuning of defaults.
    bool  mount3DSeeded = false;
    float mount3D_X = 0.0f;
    float mount3D_Y = 0.0f;
    float mount3D_Z = 0.0f;
};

// Apply persisted state.  MUST be called from a flight-loop callback,
// not a message handler — calling from XPLM_MSG_AIRPORT_LOADED
// crashed X-Plane on plugin reload because Show() triggers
// SDL/M5GFX/panel-framebuffer lazy init that the M5Unified singleton
// + SDL timer subsystem don't tolerate from arbitrary SDK message
// dispatch contexts.  aoa_audio.cpp's periodic save callback
// observes a one-shot flag set in the AIRPORT_LOADED handler and
// invokes ApplyPersistedState on the next flight-loop tick.
void ApplyPersistedState(const PersistedState& st);

// Destroy and recreate the X-Plane window with a different decoration.
// Used when switching between mounted (self-decorated) and floating/
// pop-out (RoundRectangle).  No-op if no window exists yet.  Must be
// called from a flight-loop callback context (same restriction as
// ApplyPersistedState).
void RecreateWindowWithDecoration(int decoration);

// Re-seed the mount3D anchor from the current pilot eyepoint.  Used
// by the "Reset indexer position to default" menu item and triggered
// automatically the first time a pilot enters mounted mode for an
// aircraft that has no saved anchor.
void ResetMount3DAnchor();

// Snapshot current window state for saving.  Reads geometry from the
// active positioning mode (floating vs popped-out) via the matching
// SDK getter; the other mode's coords are held at their last persisted
// values so a pop-out → drag → un-pop session preserves both spots.
// Filters out obviously-bad reads (multi-monitor transition glitches
// that previously stranded the window 67k boxels off-screen).
void GetCurrentState(PersistedState* out);

// Compare current geometry against the last-persisted snapshot.
// Sets a dirty flag if anything changed and the new state is
// well-formed.  Polled by the periodic save callback in
// aoa_audio.cpp; flushes via SaveSettings if dirty.
void MarkDirtyIfChanged();
bool IsDirty();
void ClearDirty();

// Tear down the X-Plane window and GL texture.  Called from XPluginStop.
void Shutdown();

// USB-serial output to a physical M5Stack.  When configured, every
// display-serial frame the indexer builds in Tick() is also pushed
// out the named port at 115200 8N1 — same wire format as a real
// OnSpeed display, so the same M5 firmware that runs embedded in
// the X-Plane plugin will run on a USB-tethered Core2 against the
// sim.
//
// Pass an empty path to close any currently-open port.  Returns true
// on successful open (or successful close); false if the path could
// not be opened.
bool OpenSerialOut(const std::string& portPath);
void CloseSerialOut();
bool IsSerialOutOpen();
const std::string& SerialOutPath();

}  // namespace onspeed_xplane::indexer
