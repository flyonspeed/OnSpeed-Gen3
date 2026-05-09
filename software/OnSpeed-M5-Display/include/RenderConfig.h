// software/OnSpeed-M5-Display/include/RenderConfig.h
//
// Cross-cutting render configuration shared by main.cpp and SettingsMenu.cpp.
//
// XPLANE_PLUGIN_DEPTH selects the off-screen sprite color depth. The firmware
// builds use 8-bit palette indexing for memory efficiency; the X-Plane
// plugin's CMake overrides to 16 (RGB565) so its custom Panel_PluginCanvas
// can read the framebuffer directly.
#pragma once

#ifndef XPLANE_PLUGIN_DEPTH
#define XPLANE_PLUGIN_DEPTH 8
#endif
