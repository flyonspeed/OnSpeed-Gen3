// hud-test-entry.js — DEV ONLY synthetic HUD renderer for visual iteration.
//
// Mounts HudOverlay with a controllable synthetic M5State so the HUD can
// be tweaked without loading a real flight log. Not shipped to the
// firmware bundle and not referenced from ReplayPage. The static
// hud-test.html sibling embeds this as a `<script type="module">`,
// mkdocs serves both as-is.
//
// Import paths follow the 2-up convention documented in replay-entry.js:
// from /data-and-logs/replay/, ../../packages/ui-core/... resolves to
// /packages/ui-core/... (via the docs/site/docs/packages -> ../../../packages
// symlink), and the same path works under mike's /latest/ prefix in
// production.

import { html, render, useState } from '../../packages/ui-core/vendor/preact-standalone.js';
import { HudOverlay } from '../../packages/ui-core/components/svg/HudOverlay.js';

// HudOnSpeedLogo resolves the FlyOnSpeed PNG via the `__replayBundleBase`
// global the bundle preamble sets. The unbundled test page has no such
// preamble, so we set it here on `window` before render() so the
// component picks it up at first paint. Points at the directory that
// holds lib/components/hud/assets/FlyOnSpeed_Logo.png.
window.__replayBundleBase = new URL('./', import.meta.url).href;

function Row({ label, suffix, value, min, max, step, onInput }) {
  return html`
    <label>${label}: ${value}</label>
    <input type="range" min=${min} max=${max} step=${step} value=${value}
           oninput=${e => onInput(parseFloat(e.target.value))} />
    <span>${suffix}</span>`;
}

function App() {
  const [iasKt, setIasKt] = useState(138);
  const [altFt, setAltFt] = useState(4080);
  const [pitchDeg, setPitchDeg] = useState(5);
  const [rollDeg, setRollDeg] = useState(0);
  const [vsiFpm, setVsiFpm] = useState(480);
  const [pitchOffsetDeg, setPitchOffsetDeg] = useState(0);

  // Synthetic M5State. Only the fields HudOverlay's subtree reads need
  // to be present; defaults inside the components handle the rest.
  const state = {
    IAS: iasKt,
    Palt: altFt,
    Pitch: pitchDeg,
    Roll: rollDeg,
    iVSI: vsiFpm,
    FlightPath: pitchDeg, // FPM sits on the horizon at zero pitch
    LateralG: 0,
    VerticalG: 1.0,
    PercentLift: 50,
    StallWarnPctLift: 80,
    IasIsValid: true,
  };

  return html`
    <div class="hud-test-stage">
      <${HudOverlay} state=${state} pitchOffsetDeg=${pitchOffsetDeg} />
    </div>
    <div class="hud-test-controls">
      <h3>Synthetic HUD state</h3>
      <div class="hud-test-grid">
        <${Row} label="IAS"        suffix="kt"  value=${iasKt}          min=${0}    max=${200}  step=${1}   onInput=${setIasKt} />
        <${Row} label="Palt"       suffix="ft"  value=${altFt}          min=${-500} max=${15000} step=${10}  onInput=${setAltFt} />
        <${Row} label="Pitch"      suffix="°"   value=${pitchDeg}       min=${-30}  max=${30}    step=${0.5} onInput=${setPitchDeg} />
        <${Row} label="Roll"       suffix="°"   value=${rollDeg}        min=${-60}  max=${60}    step=${1}   onInput=${setRollDeg} />
        <${Row} label="VSI"        suffix="fpm" value=${vsiFpm}         min=${-2500} max=${2500} step=${10}  onInput=${setVsiFpm} />
        <${Row} label="Pitch off"  suffix="°"   value=${pitchOffsetDeg} min=${-20}  max=${20}    step=${0.1} onInput=${setPitchOffsetDeg} />
      </div>
    </div>`;
}

const root = document.getElementById('hud-test-root');
if (!root) {
  console.error('hud-test: #hud-test-root mount point not found');
} else {
  render(html`<${App} />`, root);
}
