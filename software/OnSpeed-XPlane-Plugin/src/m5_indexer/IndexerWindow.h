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

// Tear down the X-Plane window and GL texture.  Called from XPluginStop.
void Shutdown();

}  // namespace onspeed_xplane::indexer
