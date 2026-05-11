// IndexerMode.js — Mode 2 renderer driven by M5 firmware sim state.
//
// PR 2 of Project B2. Mode 2 is the AOA-only page: same indexer/ball/
// percent-number/edge-tape as Mode 0 but no corners, no flap circle,
// no datamark. Mirrors main.cpp:842-851 where Mode 2 calls displayAOA()
// with `numericDisplay = false`.
//
// Composes EnergyMode with the gate flipped — same renderer, fewer
// elements painted. Matches the C++ pattern.

import { html } from '../../../vendor/preact-standalone.js';
import { EnergyMode } from './EnergyMode.js';

export const IndexerMode = ({ state, stale = false }) => html`
  <${EnergyMode} state=${state} stale=${stale} numericDisplay=${false} />`;
