// m5modes/index.js — barrel for the five M5-accurate mode renderers.
//
// PR 2 of Project B2. Each mode is a Preact component that takes
// `{ state, stale }` where `state` is the frozen object returned by
// `tools/web/lib/replay/m5sim/m5sim.js::M5Sim.read()`.
//
// The components are pure renderers — they do not subscribe, fetch,
// or own state. The page is responsible for advancing the M5 sim and
// passing in the latest state.

export { EnergyMode }   from './EnergyMode.js';
export { AttitudeMode } from './AttitudeMode.js';
export { IndexerMode }  from './IndexerMode.js';
export { DecelMode }    from './DecelMode.js';
export { HistoricGMode } from './HistoricGMode.js';
