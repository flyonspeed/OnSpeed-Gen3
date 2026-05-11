// m5modes/index.js — barrel for the five M5-accurate mode renderers.
//
// Each mode is a Preact component that takes `{ state, stale }` where
// `state` is the canonical M5State (see packages/ui-core/state-shape.js).
// Both the docs-site replay tool (M5Sim.read() → state) and the
// firmware-served /indexer page (wsRecordToState(r) → state) consume
// the same renderers.
//
// The components are pure renderers — they do not subscribe, fetch,
// or own state. The host page is responsible for producing the state.
//
// Implementation note: explicit import-then-export is used here
// instead of the shorter re-export form (re-export-from-module-X)
// because the firmware bundler doesn't support re-exports. Same
// content either way; just a syntax constraint.

import { EnergyMode }    from './EnergyMode.js';
import { AttitudeMode }  from './AttitudeMode.js';
import { IndexerMode }   from './IndexerMode.js';
import { DecelMode }     from './DecelMode.js';
import { HistoricGMode } from './HistoricGMode.js';

export { EnergyMode, AttitudeMode, IndexerMode, DecelMode, HistoricGMode };
