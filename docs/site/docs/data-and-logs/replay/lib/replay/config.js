// config.js — thin WASM wrapper for OnSpeed config XML parsing.
//
// Pre-PLAN_WASM_CORE.md Step 1 this file contained a JS port of the C++
// config parser including the V1/V2 detection logic and the <3DAUDIO>
// regex rewrite needed because browsers' DOMParser rejects digit-prefixed
// tags.  It now delegates entirely to the WASM build of onspeed_core,
// so the Replay tool and the firmware parse configs with the exact same
// compiled code.
//
// The <3DAUDIO> → <_3DAUDIO> XML preprocessing trick is handled by
// ConfigV1Parse.cpp's string-search parser, which locates tags by text
// scan and therefore accepts the digit-prefixed tag names that are invalid
// XML.  The JS-side regex rewrite is no longer needed.
//
// Public API (unchanged from the hand-port — callers don't need to change):
//
//   parseConfigXml(xmlText) -> Promise<object>
//
// The returned object has the same field names as host_main's parse_config
// JSON output:
//   { aoaSmoothing, pressureSmoothing, muteUnderIas, dataSource,
//     volumeControl, defaultVolume, audio3D, overGWarning, efisType,
//     serialOutFormat, pitchBias, rollBias, gxBias, gyBias, gzBias,
//     pstaticBias, loadLimitPositive, loadLimitNegative, iAhrsAlgorithm,
//     sdLogging, boomConvertData, logRate, vno, acGrossWeight,
//     acBestGlideIAS, acVfe,
//     flaps: [{ degrees, potPosition, ldmaxAoa, onSpeedFastAoa,
//               onSpeedSlowAoa, stallWarnAoa, stallAoa, manAoa,
//               alpha0, alphaStall, kFit,
//               aoaCurve: { type, coeff: [a3, a2, a1, a0] } }, ...]
//   }
//
// aoaCurve is required for non-zero engine AOA. SuFlaps's default ctor
// leaves AoaCurve all-zero, so a hand-rolled cfg that omits aoaCurve
// will pin engine AOA to 0 — see bindings.cpp ConfigFromVal.
//
// On parse error, parseConfigXml throws ConfigParseError.

import { getWasmCore } from './wasm_core.js';

/**
 * Thrown when parse_config returns an error object rather than a valid config.
 * Callers must handle this — do not silently render empty UI on bad XML.
 */
export class ConfigParseError extends Error {
    /**
     * @param {string} message - Human-readable error description.
     * @param {string} xmlSnippet - First 200 characters of the input for diagnostics.
     */
    constructor(message, xmlSnippet) {
        super(message);
        this.name = 'ConfigParseError';
        this.xmlSnippet = xmlSnippet;
    }
}

/**
 * Parse a V1 or V2 OnSpeed config XML string.
 *
 * Both V1 (<CONFIG> root) and V2 (<CONFIG2> root) formats are supported.
 * The C++ parser auto-detects the format via IsV1Format().
 *
 * @param {string} xmlText - Raw XML text from a .cfg file.
 * @returns {Promise<object>} Parsed config object with flaps array,
 *   alpha values, etc.
 * @throws {ConfigParseError} If the XML cannot be parsed.
 *
 * (Declared as `async function` then exported separately because
 * build_web_bundle.py's regex grammar does not recognize the inline
 * form `export async function`.)
 */
async function parseConfigXml(xmlText) {
    const w = await getWasmCore();
    const result = w.parse_config(xmlText);
    if (result && typeof result === 'object' && result.error) {
        throw new ConfigParseError(
            `Failed to parse OnSpeed config XML: ${result.error}`,
            xmlText.slice(0, 200)
        );
    }
    return result;
}

export { parseConfigXml };
