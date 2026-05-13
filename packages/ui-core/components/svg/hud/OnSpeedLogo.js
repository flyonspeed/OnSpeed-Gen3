// OnSpeedLogo.js — OnSpeed logo PNG image for the HUD top-left.
//
// Renders the FlyOnSpeed logo (mark + wordmark) as an SVG <image>
// element. The PNG ships alongside the replay bundle under
// lib/components/hud/assets/, served at the same directory URL as
// replay-bundle.js. __replayBundleBase is set by the bundle preamble
// at load time from document.currentScript.src; we resolve the PNG
// URL relative to that so the image works regardless of whether
// the bundle is served by mkdocs (port 8000), the dev server
// (port 9001), or the firmware AP.

import { html } from '../../../vendor/preact-standalone.js';
import * as H from '../../../core/hudGeometry.js';

const LOGO_REL_PATH = 'lib/components/hud/assets/FlyOnSpeed_Logo.png';

export const HudOnSpeedLogo = () => {
  // __replayBundleBase is the directory URL of the replay bundle (set
  // by the bundle preamble). Fall back to a relative path if the
  // global is missing (e.g. unbundled smoke tests in Node).
  const baseUrl = (typeof __replayBundleBase !== 'undefined' && __replayBundleBase)
    || './';
  const href = baseUrl + LOGO_REL_PATH;
  return html`
    <image data-widget="hud-onspeed-logo"
           href=${href}
           xlink:href=${href}
           x=${H.HUD_LOGO_X}
           y=${H.HUD_LOGO_Y}
           width=${H.HUD_LOGO_W}
           height=${H.HUD_LOGO_H}
           preserveAspectRatio="xMidYMid meet" />`;
};
