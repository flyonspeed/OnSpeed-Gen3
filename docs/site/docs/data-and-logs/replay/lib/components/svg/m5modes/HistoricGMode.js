// HistoricGMode.js — Mode 4 renderer driven by M5 firmware sim state.
//
// PR 2 of Project B2. Mode 4 is a 60-second scrolling vertical-G trace.
// The M5 firmware owns the 300-element ring buffer at 5 Hz (one sample
// per 200 ms — see main.cpp's `gHistoryTime` cadence). We read the
// firmware's `gHistory` Float32Array + `gHistoryIndex` and pass them
// straight to the existing GHistory SVG component.
//
// Why we don't maintain the ring buffer JS-side (as IndexerPage does
// for the live `/indexer` view): the firmware already has it. Re-
// implementing the ring in JS would re-introduce the kind of
// state-machine drift the WASM-firmware approach is meant to eliminate.
// PR 3 will probably clean up the IndexerPage path the same way.
//
// `gHistory` arriving from m5sim.read() is a Float32Array copy of the
// WASM heap (M5Sim.read() slices on each call so the array is safe to
// hold across re-renders). `gHistoryIndex` is the firmware's write
// index — the next slot it will overwrite, equivalently the oldest
// sample in the visualization.

import { html } from '../../../../../../../../../packages/ui-core/vendor/preact-standalone.js';
import * as G from '../../../../../../../../../packages/ui-core/core/geometry.js';
import { colors } from '../../../../../../../../../packages/ui-core/core/colors.js';
import { GHistory } from '../../../../../../../../../packages/ui-core/components/svg/index.js';

// "Has samples" gate matches the live IndexerPage's logic — the M5
// firmware initializes its ring with 1.0 G in setup(), so the buffer
// is always populated. The "show nothing until first sample" gate is
// for when JS hasn't received any frames yet, which doesn't apply
// here: by the time HistoricGMode renders, replay_init() has run.
//
// We still wire `hasSamples` through so a future change can flip it
// off (e.g. a panel of the very first frame post-init when the buffer
// is all 1.0 G defaults — visually misleading).
export const HistoricGMode = ({ state, stale = false, hasSamples = true }) => html`
  <svg viewBox="0 0 ${G.M5_PANEL_W} ${G.M5_PANEL_H}"
       xmlns="http://www.w3.org/2000/svg"
       style="background: ${colors.TFT_BLACK}; width: 100%; height: 100%;">
    <${GHistory} buf=${state.gHistory} writeIdx=${state.gHistoryIndex}
                 hasSamples=${hasSamples} />
  </svg>`;
