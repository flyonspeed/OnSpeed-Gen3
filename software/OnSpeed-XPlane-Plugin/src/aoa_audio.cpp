/*
 * Originally: OnSpeed X-Plane plugin
 *   https://github.com/flyonspeed/OnSpeed-XPlane
 *   Commit fff96202fc82a9e8e0e2fda7d62ee92237c18a05, imported 2026-04-23.
 *
 *   Authored by:
 *     Topher Timemachine <https://github.com/TopherTimeMachine>
 *     Mrcoole7890       <https://github.com/Mrcoole7890>
 *
 * Ported into the OnSpeed-Gen3 tree (PR #256) and adapted to:
 *   - route audio decisions through onspeed_core::audio::ToneCalc
 *     (PR #261)
 *   - link against the vendored X-Plane SDK 4.3.0 (PR #259)
 *   - build on Linux and Windows via GitHub Actions matrix (PR #263)
 *
 * Licensed MIT — see LICENSE at repo root.
 */

// Version is sourced from <buildinfo.h> (BuildInfo::version, ::buildDate)
// so the plugin reports the same string as firmware/M5 builds. Wired at
// CMake configure time via scripts/generate_buildinfo.py — see
// CMakeLists.txt and software/Libraries/version/.
#include <buildinfo.h>

// Platform flags (APL / IBM / LIN) and XPLM_64 are defined by the CMake
// build. XPLM_API / PLUGIN_API are defined by the SDK's XPLMDefs.h based
// on those flags — don't pre-define them here or -Werror catches the
// redefinition on Windows.

// OpenAL's header layout differs per platform: macOS ships it as a
// system framework under <OpenAL/>, Linux + Windows (OpenAL Soft) use
// <AL/>.
#if defined(__APPLE__)
    #include <OpenAL/al.h>
    #include <OpenAL/alc.h>
#else
    #include <AL/al.h>
    #include <AL/alc.h>
#endif

#include "XPLMDisplay.h"
#include "XPLMGraphics.h"
#include "XPLMProcessing.h"
#include "XPLMDataAccess.h"
#include "XPLMPlanes.h"
#include "XPLMPlugin.h"
#include "XPLMUtilities.h"
#include "XPWidgets.h"
#include "XPStandardWidgets.h"
#include "XPWidgetUtils.h"
#include "XPLMMenus.h"

#include <cctype>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <thread>
#include <mutex>
#include <atomic>
#include <optional>
#include <memory>
#include <string>
#include <vector>

// Plugin shares its audio engine with the firmware via onspeed_core —
// envelope shape, orchestration decision, mixing, and synthesis are
// all the same code that drives the panel-mounted Gen3.  This file
// owns the OpenAL streaming I/O and the X-Plane SDK glue.
#include <audio/AudioMixer.h>
#include <audio/AudioOrchestrator.h>
#include <audio/Envelope.h>
#include <audio/Panning.h>
#include <audio/ToneCalc.h>
#include <audio/ToneSynth.h>
#include <config/OnSpeedConfig.h>
#include <filters/RunningMean.h>

#ifdef ENABLE_M5_INDEXER
#include "m5_indexer/IndexerWindow.h"
#include "serial_port.h"
#endif

// DataRefAdapter.h declares both indexer-side helpers
// (BuildInputsFromDatarefs, FillPercentLift) AND the audio-path
// helper MaybeSynthesizeAoaFromVSquared.  The header is pure
// declarations (no XPLM, no M5GFX includes), so it's safe to include
// unconditionally.  In audio-only builds we link only the
// MaybeSynthesizeAoaFromVSquared definition (PercentLiftFill.cpp);
// BuildInputsFromDatarefs / FillPercentLift remain unlinked because
// nothing in the audio path calls them.
#include "m5_indexer/DataRefAdapter.h"

// AutoSetpoints helper is platform-independent and lives under
// m5_indexer/ for namespace cohesion, but the auto-derivation runs in
// the audio flight loop too — keep the include unconditional so an
// audio-only build still benefits from per-aircraft setpoint defaults.
#include "m5_indexer/AutoSetpoints.h"
#include <filters/RunningMedian.h>
#include <util/OnSpeedTypes.h>

// Function declarations
void cleanupAudio();
void AudioRenderThread();
static void UpdateAOATextFields();
static void CreateAudioControlWindow(int x, int y, int w, int h);

// Per-aircraft AOA setpoints, named to match the firmware's
// per-flap config fields (Config.h::SuFlaps).  The four boundary
// AOAs partition the tone map:
//
//   below LDmax              → silence
//   LDmax  .. OnSpeedFast    → low pulsed (1.5..8.2 PPS)
//   OnSpeedFast .. OnSpeedSlow → low solid (the "OnSpeed" band)
//   OnSpeedSlow .. StallWarn → high pulsed (1.5..6.2 PPS)
//   above StallWarn          → high pulsed at stall PPS (20)
//
// Compiled-in defaults are generic and only used when no .prf exists
// AND the per-aircraft auto-seed (see SeedSetpointsFromDatarefs) can't
// derive values from X-Plane's stall datarefs.  After first load,
// pilots tune these via the audio control window and the values
// persist to the per-aircraft .prf.
float fLDMAXAOA       = 6.0f;
float fONSPEEDFASTAOA = 7.3f;
float fONSPEEDSLOWAOA = 9.6f;
float fSTALLWARNAOA   = 12.5f;

// Wing AOA at literal stall — the 100% point on the indexer's
// percent-lift scale.  Pilot-editable; defaults to a generic stall
// AOA that's well above the StallWarn anchor so an unseeded pilot
// sees plausible scaling out of the box.  Seeded per-aircraft from
// acf_stall_warn_alpha / 0.92 in SeedSetpointsFromDatarefs.
float fALPHASTALL    = 13.6f;   // 12.5 / 0.92, matches the canonical fraction

// Shared debounced onground_any value.  Written by CheckAOAAndPlayTone
// (audio path) once per flight-loop tick after running the raw
// onground_any reading through DebounceOnGround.  Read by the
// indexer's BuildInputsFromDatarefs so audio and indicator can never
// disagree on whether we're in the V² (on-ground) regime — a single-
// frame raw flicker is absorbed once for both consumers instead of
// each running its own debouncer.  False at static init so the very
// first flight-loop tick starts in the safe "in flight, use alpha"
// regime; the next tick latches the actual ground state.
bool g_DebouncedOnGround = false;

// USB-serial output to a physical M5Stack.  Empty string = disabled.
// On macOS path looks like "/dev/cu.usbmodem11201", on Linux
// "/dev/ttyACM0", on Windows "COM5".  Persisted per-aircraft in the
// .prf file alongside the AOA setpoints.
std::string sSerialPortPath;

// IAS gate: tone path is silenced below this airspeed.  Matches the
// firmware's `iMuteAudioUnderIAS` config field with hysteresis —
// audio unmutes at iMuteAudioUnderIAS + kIasMuteHysteresisKt and
// re-mutes back at iMuteAudioUnderIAS, so a touchdown bouncing the
// airspeed across the threshold doesn't chatter the tone on/off.
// Sentinel: 0 means "never mute on IAS" (matches firmware).
int   iMuteAudioUnderIAS         = 25;
constexpr int kIasMuteHysteresisKt = 5;

// Plugin-only smoothing.  X-Plane's sim/flightmodel/position/alpha
// can be jaggy at low frame rates, so we run the dataref through a
// median-despike + running-mean filter before handing it to ToneCalc.
// The firmware doesn't smooth AOA in the audio path — its AOA is
// already derived from heavily-smoothed pressure samples upstream
// in SensorIO — so these knobs have no firmware analog.
//
// Total latency ≈ (medianWindow + meanWindow) / 2 frames at the
// X-Plane flight-loop rate (typ. 30-60 Hz).
int iAoaMedianWindow = 5;     // 1 disables median (passthrough)
int iAoaMeanWindow   = 10;    // 1 disables mean (passthrough)

// 1G clean stall speed (KIAS) used by the on-ground percent-lift
// formula.  When the aircraft is on the ground, body-angle AOA is
// not aerodynamically meaningful (gear is loading the airframe);
// we instead derive percent-of-stall from V² (IAS²) so the indexer
// reflects "how loaded the wing is at this airspeed" — which is
// what the pilot wants on the takeoff roll.  See FillPercentLift.
//
// Seeded from `sim/aircraft/view/acf_Vs` (KIAS) per X-Plane
// aircraft on first load; pilot can override via the audio control
// window.  Sentinels:
//    0  = "auto" — re-seed from acf_Vs on every aircraft load
//   -1  = "disabled" — pilot has explicitly opted out; do not seed
//                      and fall back to the alpha-based formula
//   >0  = explicit Vs in KIAS (seeded or pilot-set)
// Both 0 and -1 disable the V² path; the live percent reading then
// comes from the alpha-based formula (gated on iasValid) for
// aircraft where neither acf_Vs nor the pilot has supplied a value.
int iVs1G = 0;

// Master volume (0-100 %).  Mirrors the firmware's pot-driven
// fVolume scaling — the per-PPS fVolumeMult ramp from STALL_VOL_MIN
// (cruise/OnSpeed) to STALL_VOL_MAX (stall warning) is multiplied by
// this before the per-channel pan splits.  100 = full output, 0 =
// silent (the IAS gate / Sound:Off toggle stays the safer mute path).
int iMasterVolumePct = 100;

#define DEFAULT_VOLUME          1.0f    // OpenAL gain (0..1)

// OpenAL device, context, source.  Rendering uses streaming queued
// buffers (alSourceQueueBuffers) rather than a fixed precomputed
// loop; carrier + envelope + mixer run sample-accurate per-chunk
// and feed PCM into a rotating bank of buffers.
ALCdevice* device = nullptr;
ALCcontext* context = nullptr;
ALuint audioSource = 0;

// Streaming buffer pool.  Four 10ms chunks @ 16kHz ≈ 40ms of
// in-flight audio — keeps OpenAL fed without starving while staying
// short enough that an envelope state change takes effect within
// one chunk window.  More buffers => more latency on transitions.
constexpr int   kAudioSampleRateHz = onspeed::audio::kSampleRateHz;  // 16000
constexpr int   kFramesPerChunk    = kAudioSampleRateHz / 100;       // 160 frames = 10ms
constexpr int   kStreamBufferCount = 4;
ALuint streamBuffers[kStreamBufferCount] = {0, 0, 0, 0};

// X-Plane datarefs read each flight loop.
XPLMDataRef aoaDataRef       = nullptr;     // sim/flightmodel/position/alpha
XPLMDataRef iasDataRef       = nullptr;     // sim/flightmodel/position/indicated_airspeed
XPLMDataRef lateralGDataRef  = nullptr;     // sim/flightmodel/forces/g_side
XPLMDataRef pausedDataRef    = nullptr;     // sim/time/paused (int, 1 when paused)
XPLMDataRef crashedDataRef   = nullptr;     // sim/flightmodel2/misc/has_crashed (int, 1 after crash)
// sim/aircraft/view/acf_has_stallwarn — int, writable.  Reapplied each
// flight loop when the user has chosen to mute the sim's stall horn,
// because X-Plane restores the .acf-defined value on aircraft load.
XPLMDataRef acfHasStallwarnDataRef = nullptr;
XPLMDataRef acfVsDataRef     = nullptr;     // sim/aircraft/view/acf_Vs (float, KIAS)
XPLMDataRef onGroundAnyDataRef = nullptr;   // sim/flightmodel/failures/onground_any (int, 1 = any gear touching)

// Seed-on-first-load datarefs.  Read once per aircraft load when no
// .prf is present yet; their values seed the four f*AOA globals
// before the audio control window opens.  After seeding, the pilot
// owns the values; nothing in this plugin overwrites them.
//
// acf_stall_warn_alpha is the per-aircraft wing AOA at which the
// stall warner fires (RV-10 = 10°, C172 = 12°, F-14 = 20°).  acf_Vso
// + acf_Vs supply per-flap reduction at seed time so the seeded
// values reflect a representative mid-flap config rather than
// clean-only.  flap_handle_deploy_ratio = 0 at seed time gives the
// clean-config seed; pilots flying full-flap approaches will likely
// adjust the seeded values down to match their use.
XPLMDataRef acfStallWarnAlphaRef  = nullptr;
XPLMDataRef acfVsoDataRef         = nullptr;  // sim/aircraft/view/acf_Vso (KIAS, full flap)

// Control-window widget handles.
static XPWidgetID audioControlWidget  = nullptr;
static XPWidgetID audioToggleCheckbox = nullptr;
static XPWidgetID widgetAOAValue      = nullptr;
static XPWidgetID widgetAudioStatus   = nullptr;
static XPWidgetID widgetButtonStallHorn = nullptr;

// Editable-field handles.  Declared here (rather than next to the
// CreateAudioControlWindow that builds them) because the validator
// below has to address them by name.
static XPWidgetID widgetLDMaxAOA              = nullptr;
static XPWidgetID widgetOnSpeedFastAOA        = nullptr;
static XPWidgetID widgetOnSpeedSlowAOA        = nullptr;
static XPWidgetID widgetStallWarnAOA          = nullptr;
static XPWidgetID widgetAlphaStall            = nullptr;
static XPWidgetID widgetMuteAudioUnderIAS     = nullptr;
static XPWidgetID widgetVs1G                  = nullptr;
static XPWidgetID widgetMasterVolumePct       = nullptr;
static XPWidgetID widgetAoaMedianWindow       = nullptr;
static XPWidgetID widgetAoaMeanWindow         = nullptr;
static XPWidgetID widgetButtonSave            = nullptr;
static XPWidgetID widgetButtonRestoreDefaults = nullptr;

static bool audioEnabled = false;

// User-controlled override for X-Plane's built-in stall horn.  When true,
// CheckAOAAndPlayTone clears acf_has_stallwarn each flight loop so the
// sim doesn't play its own stall warning over OnSpeed's audio cues.
// Persisted per-aircraft.  Default off (sim's horn behaves normally).
static bool bMuteStallHorn = false;

// Original value of acf_has_stallwarn captured the first time we mute
// the horn for the current aircraft.  Toggling the mute back off
// restores this value rather than blindly writing 1, so an aircraft
// whose .acf legitimately has no stall horn doesn't end up with one
// after a mute / unmute cycle.  -1 means "not yet captured."
// X-Plane resets the dataref to the .acf default on aircraft load, so
// OnAircraftLoaded() resets this back to -1 — the next mute for the
// new aircraft re-captures.
static int g_iAcfHasStallwarnOriginal = -1;

static XPLMMenuID menuId;

// AOA smoothing pipeline.  Window sizes are runtime-configurable via
// iAoaMedianWindow / iAoaMeanWindow; rebuildAoaSmoothers() swaps in
// fresh filter instances when the user updates them.
std::unique_ptr<onspeed::RunningMedian> aoaMedian;
std::unique_ptr<onspeed::RunningMean>   aoaMean;

static void rebuildAoaSmoothers() {
    if (iAoaMedianWindow < 1) iAoaMedianWindow = 1;
    if (iAoaMeanWindow   < 1) iAoaMeanWindow   = 1;
    aoaMedian = std::make_unique<onspeed::RunningMedian>(iAoaMedianWindow);
    aoaMean   = std::make_unique<onspeed::RunningMean>(iAoaMeanWindow);
}

// --------------------------------------------------------------------------
// Per-aircraft settings persistence.
//
// Settings live at <X-Plane>/Output/preferences/AOA-Tone-FlyOnSpeed-<acf>.prf
// where <acf> is the aircraft .acf basename (no extension, slashes
// flattened).  Format is plain `key = value\n` lines; missing keys keep
// their compiled-in defaults so a partial file is fine.
//
// Calibration is per-airframe (RV-10 ≠ Cessna 172) so every editable
// field gets persisted in the same per-aircraft file — no global vs.
// per-aircraft split.  audioEnabled (Sound:On/Off) is persisted too:
// the pilot's choice for one airframe survives a sim restart.

constexpr const char* kSettingsDirRel = "Output/preferences/";
constexpr const char* kSettingsPrefix = "AOA-Tone-FlyOnSpeed-";
constexpr const char* kSettingsSuffix = ".prf";

// Path the most recently loaded/saved settings file lives at.  Tracked
// so Save can write back to the right file even after an aircraft
// change races with a button press.
static std::string s_SettingsPath;

// Race-guard: an indexer click or menu toggle can fire before the
// deferred OnAircraftLoaded → LoadSettings completes.  SaveSettings
// must skip those writes — the in-memory AOA setpoints / volume etc.
// are still compile defaults, and writing now would clobber the
// pilot's saved calibration.  Cleared at the bottom of LoadSettings.
static bool s_settingsLoaded = false;

#ifdef ENABLE_M5_INDEXER
// Indexer window state, persisted per-aircraft alongside the audio
// settings.  Mirrors PersistedState in IndexerWindow.h; SaveSettings
// emits / LoadSettings parses these as separate .prf keys.
static onspeed_xplane::indexer::PersistedState indexerSettings;

// One-shot flag set in XPLM_MSG_AIRPORT_LOADED.  The periodic save
// callback (which runs from a flight-loop callback context, where
// Show()'s lazy-init is safe) observes this on its next tick and
// invokes ApplyPersistedState exactly once.  Doing the apply from
// the message handler itself crashed X-Plane on plugin reload —
// SDL/M5GFX/M5Unified singleton + panel-framebuffer init don't
// tolerate execution from arbitrary SDK message dispatch contexts.
static bool s_indexerRestorePending = false;
#endif

// Audio control window (Plugins → Fly On Speed → Show) geometry +
// visibility, persisted per-aircraft.  Defaults match the historical
// CreateAudioControlWindow(300, 690, 280, 470) literals.  Visibility
// defaults to false so a fresh aircraft doesn't auto-pop the panel
// the first time it loads — only an aircraft that previously had the
// panel open at save time will have it reopen.
//
// Sized to fit the current widget count (5 setpoint floats + 5
// scalar ints + 3 buttons + 3 status captions ≈ 16 rows) at 25 px
// pitch plus chrome.  A larger persisted height (e.g. 820 from the
// auto-mode experiment) is clamped DOWN to this on load — the prior
// value would leave the bottom of the window way past the buttons.
static constexpr int kAudioWindowHeight = 470;

struct AudioWindowState {
    int  left   = 300;
    int  top    = 690;
    int  width  = 280;
    int  height = kAudioWindowHeight;
    bool visible = false;
};
static AudioWindowState s_audioWindow;
// Track the last-persisted geometry so the periodic save callback
// only writes the .prf when the user actually moves or resizes the
// window — avoids hammering disk on every tick.
static AudioWindowState s_audioWindowLastSaved;

// Minimum boxels of the audio control window kept on-screen after a
// monitor swap.  Matches the floating-indexer kMinVisible (80 in
// IndexerWindow.cpp) so both windows treat "still grabbable" the
// same way.
constexpr int kAudioWindowMinVisible = 80;

// Clamp s_audioWindow.left/top so the title bar stays grabbable on
// the current X-Plane screen rect.  Without this, a per-aircraft
// .prf saved when X-Plane spanned a tall external monitor (e.g.
// audioWindowTop = 1307) restores fully off-screen on a 1080-tall
// monitor, with no in-game way to recover.
//
// X-Plane "global" coords cover the union of all monitors X-Plane
// is rendering to, so this clamp respects multi-monitor setups: a
// position legitimately on a second X-Plane monitor stays put as
// long as that monitor is still in the bounds.  When the user
// disconnects a monitor, the bounds shrink and the window snaps to
// the remaining visible area.
//
// Skips clamping if XPLMGetScreenBoundsGlobal returns a degenerate
// rect (e.g. during early plugin init before the screen is up).
static void ClampAudioWindowOnScreen() {
    int sLeft = 0, sTop = 0, sRight = 0, sBottom = 0;
    XPLMGetScreenBoundsGlobal(&sLeft, &sTop, &sRight, &sBottom);
    if (sRight <= sLeft || sTop <= sBottom) return;

    const int maxLeft = sRight - kAudioWindowMinVisible;
    const int minLeft = sLeft  - (s_audioWindow.width - kAudioWindowMinVisible);
    const int maxTop  = sTop;
    const int minTop  = sBottom + kAudioWindowMinVisible;

    if (s_audioWindow.left > maxLeft) s_audioWindow.left = maxLeft;
    if (s_audioWindow.left < minLeft) s_audioWindow.left = minLeft;
    if (s_audioWindow.top  > maxTop)  s_audioWindow.top  = maxTop;
    if (s_audioWindow.top  < minTop)  s_audioWindow.top  = minTop;
}

static std::string sanitizeAcfBasename(const char* acfFileName) {
    std::string out;
    out.reserve(strlen(acfFileName));
    for (const char* p = acfFileName; *p != '\0'; ++p) {
        char c = *p;
        if (c == '/' || c == '\\' || c == ':') c = '_';
        out += c;
    }
    // Strip the .acf extension if present.
    const std::string acfExt = ".acf";
    if (out.size() >= acfExt.size() &&
        out.compare(out.size() - acfExt.size(), acfExt.size(), acfExt) == 0) {
        out.resize(out.size() - acfExt.size());
    }
    return out;
}

// Build the absolute path to the per-aircraft settings file for the
// currently-loaded user aircraft.  Returns empty string if X-Plane
// reports no user aircraft (e.g., during early startup).
static std::string buildSettingsPath() {
    char xpRoot[1024] = {0};
    XPLMGetSystemPath(xpRoot);

    char acfFile[256] = {0};
    char acfPath[1024] = {0};
    XPLMGetNthAircraftModel(0, acfFile, acfPath);
    if (acfFile[0] == '\0') return {};

    std::string path = xpRoot;
    path += kSettingsDirRel;
    path += kSettingsPrefix;
    path += sanitizeAcfBasename(acfFile);
    path += kSettingsSuffix;
    return path;
}

// Forward decls so visibility-changing handlers (close button, Show
// menu) can capture the latest geometry before persisting.
static void RefreshAudioWindowState();
static bool AudioWindowChanged();

// One-shot seed: read X-Plane's stall datarefs and overwrite the four
// f*AOA setpoint globals with sensible per-aircraft defaults.  Called
// from OnAircraftLoaded only when no per-aircraft .prf exists yet —
// once a pilot has saved their tuning, those values persist and this
// function isn't called again for that aircraft.
//
// Uses flapRatio=0 (clean config) so the seeded values are the clean
// setpoints; pilots can adjust from there.  When acf_stall_warn_alpha
// is missing or zero, leaves the compiled-in defaults in place and
// logs a breadcrumb.
static void SeedSetpointsFromDatarefs()
{
    const float stallWarnAoa = acfStallWarnAlphaRef
                                ? XPLMGetDataf(acfStallWarnAlphaRef) : 0.0f;
    const float vsKt         = acfVsDataRef
                                ? XPLMGetDataf(acfVsDataRef)         : 0.0f;
    const float vsoKt        = acfVsoDataRef
                                ? XPLMGetDataf(acfVsoDataRef)        : 0.0f;

    const auto d = onspeed_xplane::indexer::DeriveSetpointsFromDatarefs(
        stallWarnAoa, vsKt, vsoKt, /*flapRatio=*/0.0f,
        onspeed_xplane::indexer::kCanonicalNaoa);
    if (!d.applied) {
        XPLMDebugString("FlyOnSpeed: seed setpoints declined "
                        "(acf_stall_warn_alpha missing or 0); "
                        "compiled-in defaults stand\n");
        return;
    }
    fLDMAXAOA       = d.ldmax;
    fONSPEEDFASTAOA = d.onSpeedFast;
    fONSPEEDSLOWAOA = d.onSpeedSlow;
    fSTALLWARNAOA   = d.stallWarn;
    // alpha_stall = StallWarn / 0.92 by canonical fraction.  The
    // indexer's 100% point lands at this value on the wing-AOA scale.
    fALPHASTALL     = d.stallWarn /
                      onspeed_xplane::indexer::kCanonicalNaoa.stallWarn;
}

// Returns true only when the .prf was opened, all fields written, and
// fclose reported no buffered-write error.  Callers that gate
// follow-on side effects on a confirmed write (e.g. renaming a legacy
// file after a successful import) must check this return value.
// Fire-and-forget callers can ignore it.
static bool SaveSettings() {
    if (s_SettingsPath.empty()) return false;
    if (!s_settingsLoaded) {
        // Race-guard: SaveSettings called before LoadSettings finished
        // for the current aircraft.  In-memory AOA setpoints / volume /
        // smoothing are still compile defaults — writing them now
        // would clobber the pilot's saved calibration.  Skip; the next
        // user-driven change after load completes will persist.
        XPLMDebugString("FlyOnSpeed: SaveSettings skipped — settings "
                        "not yet loaded for this aircraft\n");
        return false;
    }
    FILE* fp = std::fopen(s_SettingsPath.c_str(), "w");
    if (!fp) {
        XPLMDebugString(("FlyOnSpeed: SaveSettings: could not open "
                         + s_SettingsPath + " for writing\n").c_str());
        return false;
    }
    std::fprintf(fp, "fLDMAXAOA = %.3f\n",       fLDMAXAOA);
    std::fprintf(fp, "fONSPEEDFASTAOA = %.3f\n", fONSPEEDFASTAOA);
    std::fprintf(fp, "fONSPEEDSLOWAOA = %.3f\n", fONSPEEDSLOWAOA);
    std::fprintf(fp, "fSTALLWARNAOA = %.3f\n",   fSTALLWARNAOA);
    std::fprintf(fp, "fALPHASTALL = %.3f\n",     fALPHASTALL);
    std::fprintf(fp, "iMuteAudioUnderIAS = %d\n", iMuteAudioUnderIAS);
    std::fprintf(fp, "iVs1G = %d\n",              iVs1G);
    std::fprintf(fp, "iMasterVolumePct = %d\n",   iMasterVolumePct);
    std::fprintf(fp, "iAoaMedianWindow = %d\n",   iAoaMedianWindow);
    std::fprintf(fp, "iAoaMeanWindow = %d\n",     iAoaMeanWindow);
    std::fprintf(fp, "audioEnabled = %d\n",       audioEnabled ? 1 : 0);
    std::fprintf(fp, "bMuteStallHorn = %d\n",     bMuteStallHorn ? 1 : 0);
    std::fprintf(fp, "serialPortPath = %s\n",     sSerialPortPath.c_str());
    std::fprintf(fp, "audioWindowVisible = %d\n", s_audioWindow.visible ? 1 : 0);
    std::fprintf(fp, "audioWindowLeft = %d\n",    s_audioWindow.left);
    std::fprintf(fp, "audioWindowTop = %d\n",     s_audioWindow.top);
    std::fprintf(fp, "audioWindowWidth = %d\n",   s_audioWindow.width);
    std::fprintf(fp, "audioWindowHeight = %d\n",  s_audioWindow.height);
#ifdef ENABLE_M5_INDEXER
    std::fprintf(fp, "indexerVisible = %d\n",     indexerSettings.visible ? 1 : 0);
    std::fprintf(fp, "indexerMode = %d\n",        indexerSettings.mode);
    std::fprintf(fp, "indexerPoppedOut = %d\n",   indexerSettings.isPoppedOut ? 1 : 0);
    std::fprintf(fp, "indexerFloatLeft = %d\n",   indexerSettings.floatLeft);
    std::fprintf(fp, "indexerFloatTop = %d\n",    indexerSettings.floatTop);
    std::fprintf(fp, "indexerFloatWidth = %d\n",  indexerSettings.floatWidth);
    std::fprintf(fp, "indexerFloatHeight = %d\n", indexerSettings.floatHeight);
    std::fprintf(fp, "indexerPopLeft = %d\n",     indexerSettings.popLeft);
    std::fprintf(fp, "indexerPopTop = %d\n",      indexerSettings.popTop);
    std::fprintf(fp, "indexerPopWidth = %d\n",    indexerSettings.popWidth);
    std::fprintf(fp, "indexerPopHeight = %d\n",   indexerSettings.popHeight);
#endif
    // fclose can return EOF if a buffered fprintf write hit an error
    // that fprintf itself didn't surface (full disk, broken NAS mount).
    // Treat it as a write failure so callers don't act on a corrupt or
    // truncated .prf.
    if (std::fclose(fp) != 0) {
        XPLMDebugString(("FlyOnSpeed: SaveSettings: fclose reported error "
                         "for " + s_SettingsPath +
                         " — .prf may be incomplete\n").c_str());
        return false;
    }
    // Periodic-save baseline only updates on a confirmed-clean
    // write.  If fclose failed above, leave s_audioWindowLastSaved
    // untouched so AudioWindowChanged() returns true next tick and
    // the periodic-save callback retries.
    s_audioWindowLastSaved = s_audioWindow;
    return true;
}

#ifdef ENABLE_M5_INDEXER
// Snapshot indexer state and persist.  Called by the periodic save
// callback when MarkDirtyIfChanged fires, by menu Show/Hide/SetMode
// handlers for an immediate flush, by XPLM_MSG_WILL_WRITE_PREFS as a
// last chance before X-Plane writes its own prefs, and by XPluginStop
// on clean shutdown.
void SaveIndexerWindowState()
{
    onspeed_xplane::indexer::GetCurrentState(&indexerSettings);
    SaveSettings();
    onspeed_xplane::indexer::ClearDirty();
}
#endif

// Best-effort key=value parser.  Unknown keys, malformed lines, and
// missing files are all silently ignored — defaults stand in.
//
// Returns true if a .prf file was found and read for the current
// aircraft; false if the file didn't exist (first run for this
// aircraft).  Caller uses this to decide whether to attempt the
// one-time import from the legacy json-config plugin's format and
// whether to seed setpoints from X-Plane's stall datarefs.
static bool LoadSettings() {
    if (s_SettingsPath.empty()) return false;
    FILE* fp = std::fopen(s_SettingsPath.c_str(), "r");
    if (!fp) {
        // First run for this aircraft; compiled-in defaults stand
        // until the seed-from-datarefs path overwrites them.  Mark
        // settings loaded anyway so subsequent user-driven changes
        // can persist.
        s_settingsLoaded = true;
        return false;
    }

    // Track whether the .prf carried an explicit fALPHASTALL key.  Old
    // .prf files (pre-#445) don't have it; loading those would leave
    // fALPHASTALL at the compiled default (13.6) regardless of whatever
    // fSTALLWARNAOA the pilot tuned.  If the pilot's fSTALLWARNAOA
    // exceeds 13.6, the V² synthesis ends up with a non-positive
    // (alphaStall - alpha0) span and produces garbage AOA.  After parse,
    // re-derive fALPHASTALL = fSTALLWARNAOA / 0.92 when the key was
    // missing — same canonical fraction the seed path uses.
    bool sawAlphaStallKey = false;

    char line[256];
    while (std::fgets(line, sizeof(line), fp)) {
        char key[64];
        char val[128];
        // " key = value " — tolerant of surrounding whitespace.
        if (std::sscanf(line, " %63[^= \t] = %127[^\n\r]", key, val) != 2)
            continue;

        if      (!std::strcmp(key, "fLDMAXAOA"))          fLDMAXAOA          = std::atof(val);
        else if (!std::strcmp(key, "fONSPEEDFASTAOA"))    fONSPEEDFASTAOA    = std::atof(val);
        else if (!std::strcmp(key, "fONSPEEDSLOWAOA"))    fONSPEEDSLOWAOA    = std::atof(val);
        else if (!std::strcmp(key, "fSTALLWARNAOA"))      fSTALLWARNAOA      = std::atof(val);
        else if (!std::strcmp(key, "fALPHASTALL"))      { fALPHASTALL        = std::atof(val); sawAlphaStallKey = true; }
        // setpoint_mode / naoa_* keys (from the brief auto-mode
        // experiment) are silently ignored if present in an old .prf.
        else if (!std::strcmp(key, "iMuteAudioUnderIAS")) iMuteAudioUnderIAS = std::atoi(val);
        else if (!std::strcmp(key, "iVs1G"))              iVs1G              = std::atoi(val);
        else if (!std::strcmp(key, "iMasterVolumePct"))   iMasterVolumePct   = std::atoi(val);
        else if (!std::strcmp(key, "iAoaMedianWindow"))   iAoaMedianWindow   = std::atoi(val);
        else if (!std::strcmp(key, "iAoaMeanWindow"))     iAoaMeanWindow     = std::atoi(val);
        else if (!std::strcmp(key, "audioEnabled"))       audioEnabled       = std::atoi(val) != 0;
        else if (!std::strcmp(key, "audioWindowVisible")) s_audioWindow.visible = std::atoi(val) != 0;
        else if (!std::strcmp(key, "audioWindowLeft"))    s_audioWindow.left    = std::atoi(val);
        else if (!std::strcmp(key, "audioWindowTop"))     s_audioWindow.top     = std::atoi(val);
        else if (!std::strcmp(key, "audioWindowWidth"))   s_audioWindow.width   = std::atoi(val);
        else if (!std::strcmp(key, "audioWindowHeight"))  s_audioWindow.height  = std::atoi(val);
        else if (!std::strcmp(key, "bMuteStallHorn"))     bMuteStallHorn     = std::atoi(val) != 0;
#ifdef ENABLE_M5_INDEXER
        else if (!std::strcmp(key, "indexerVisible"))      indexerSettings.visible      = std::atoi(val) != 0;
        else if (!std::strcmp(key, "indexerMode"))         indexerSettings.mode         = std::atoi(val);
        else if (!std::strcmp(key, "indexerPoppedOut"))    indexerSettings.isPoppedOut  = std::atoi(val) != 0;
        else if (!std::strcmp(key, "indexerFloatLeft"))    indexerSettings.floatLeft    = std::atoi(val);
        else if (!std::strcmp(key, "indexerFloatTop"))     indexerSettings.floatTop     = std::atoi(val);
        else if (!std::strcmp(key, "indexerFloatWidth"))   indexerSettings.floatWidth   = std::atoi(val);
        else if (!std::strcmp(key, "indexerFloatHeight"))  indexerSettings.floatHeight  = std::atoi(val);
        else if (!std::strcmp(key, "indexerPopLeft"))      indexerSettings.popLeft      = std::atoi(val);
        else if (!std::strcmp(key, "indexerPopTop"))       indexerSettings.popTop       = std::atoi(val);
        else if (!std::strcmp(key, "indexerPopWidth"))     indexerSettings.popWidth     = std::atoi(val);
        else if (!std::strcmp(key, "indexerPopHeight"))    indexerSettings.popHeight    = std::atoi(val);
#endif
        else if (!std::strcmp(key, "serialPortPath")) {
            // Defensive trim: sscanf %127[^\n\r] captures any trailing
            // spaces on the line, and the auto-retry loop would silently
            // spin on /dev/cu.foo<space> if the file was hand-edited
            // with extra whitespace.
            std::string v = val;
            while (!v.empty() && (v.back() == ' ' || v.back() == '\t'))
                v.pop_back();
            sSerialPortPath = v;
        }
    }
    std::fclose(fp);

    // Old-.prf forward-compat: when fALPHASTALL was absent from the
    // file (pre-#445 .prfs), re-derive it from the loaded fSTALLWARNAOA
    // using the canonical NAOA stallWarn fraction (0.92).  Without this,
    // a hand-tuned fSTALLWARNAOA above the compiled default of 13.6
    // would silently violate StallWarn < alpha_stall and corrupt the
    // V² synthesis until the pilot's next Save (which the validator
    // would catch).  Doing it here means the first flight-loop tick
    // after load already has a coherent (alphaStall - alpha0) span.
    if (!sawAlphaStallKey) {
        fALPHASTALL = fSTALLWARNAOA /
                      onspeed_xplane::indexer::kCanonicalNaoa.stallWarn;
        XPLMDebugString("FlyOnSpeed: .prf had no fALPHASTALL key; "
                        "re-derived from fSTALLWARNAOA / 0.92\n");
    }

    s_settingsLoaded = true;
    // Always snap window height to the canonical value.  Past
    // versions persisted larger heights (e.g. 820 px from the
    // auto-mode experiment) that leave a wide empty band below the
    // buttons; smaller heights cut off the bottom buttons.  Width
    // and position remain pilot-controlled via window drag.
    s_audioWindow.height = kAudioWindowHeight;
    // Pull the window up if growing the height would push the bottom
    // edge off-screen.  X-Plane window coords are bottom-left origin
    // (positive y = up); top - height is the window's bottom edge.
    // 50 is a small safety margin from the absolute bottom of the
    // screen so the close-box / status bar stays usable.
    if (s_audioWindow.top - s_audioWindow.height < 50) {
        s_audioWindow.top = s_audioWindow.height + 50;
    }
    // The .prf we just read is, by definition, the last on-disk
    // state.  Seed the periodic save's diff baseline so a no-op tick
    // doesn't immediately rewrite the file with the same values.
    s_audioWindowLastSaved = s_audioWindow;
    return true;
}

// --------------------------------------------------------------------------
// Legacy json-config import (one-time per aircraft).
//
// On aircraft load, when no .prf yet exists, look for a flat JSON
// file at `<X-Plane root>/Custom Data/<acf_ui_name>.json` and, if
// present, parse these five keys into the live AOA threshold globals:
//
//   "Below LDMax"      → fLDMAXAOA
//   "Below OnSpeed"    → fONSPEEDFASTAOA
//   "OnSpeed Max"      → fONSPEEDSLOWAOA
//   "Above OnSpeed"    → fSTALLWARNAOA
//   "IAS Tone Enable"  → iMuteAudioUnderIAS  (1.0 → 25, 0.0 → 0)
//
// On a successful import, persist via SaveSettings and rename the
// legacy file to `<name>.json.imported`.  The rename runs only when
// SaveSettings reports the .prf was actually written (see the gate
// in TryImportLegacyJson) so a write failure leaves the legacy file
// in place for retry on the next load.
//
// No-op when:
//   * No legacy file exists.
//   * Any of the four threshold keys is missing or unparseable.
//   * Threshold ordering is invalid (LDmax < Fast < Slow < Warn required).
//
// JSON parsing is hand-rolled (extractJsonFloat) since the input is
// a flat object with five string-keyed float values; a JSON library
// dependency for one one-shot path is overkill.  See
// tests/legacy_json_parse.cpp for the parser contract.
// --------------------------------------------------------------------------

// Extract a float value for `key` from a flat JSON object string.
// Returns true if the key was found and parsed; false otherwise.
//
// Accepts the input format `{"key": 6.0}` and any whitespace-tolerant
// variant.  Robust against extra whitespace, trailing comma, and minor
// JSON style choices.  Not a general JSON parser — does not handle
// nested objects, arrays, escaped strings, or non-ASCII keys.  None
// of those occur in the legacy format this parser accepts.
static bool extractJsonFloat(const std::string& body,
                             const char* key,
                             float& out)
{
    // Find `"key"` literally.
    const std::string needle = std::string("\"") + key + "\"";
    size_t k = body.find(needle);
    if (k == std::string::npos) return false;
    k += needle.size();
    // Skip whitespace + colon.
    while (k < body.size() && std::isspace(static_cast<unsigned char>(body[k]))) ++k;
    if (k >= body.size() || body[k] != ':') return false;
    ++k;
    while (k < body.size() && std::isspace(static_cast<unsigned char>(body[k]))) ++k;
    // Read float up to next non-numeric/sign/dot/exponent char.
    size_t end = k;
    while (end < body.size()) {
        char c = body[end];
        if ((c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+'
            || c == 'e' || c == 'E') {
            ++end;
        } else {
            break;
        }
    }
    if (end == k) return false;
    const std::string numStr = body.substr(k, end - k);
    char* parseEnd = nullptr;
    const float v = std::strtof(numStr.c_str(), &parseEnd);
    if (parseEnd != numStr.c_str() + numStr.size()) return false;
    if (!std::isfinite(v)) return false;
    out = v;
    return true;
}

// Build the absolute path to the legacy json-config file for the
// currently-loaded user aircraft.  Returns empty string if X-Plane
// reports no user aircraft or has no `acf_ui_name` available.
//
// Note: acf_ui_name is used raw (no path-separator sanitization,
// unlike buildSettingsPath which routes through sanitizeAcfBasename).
// The legacy plugin also used raw acf_ui_name in its sprintf, so an
// aircraft with `/` or `\` in its UI name had the legacy file
// written at the corresponding subdirectory path.  Reading raw
// matches what was written — anything else would miss the file.
// fopen returns NULL silently when the subdirectory doesn't exist,
// and the import is skipped in that case.
static std::string buildLegacyJsonPath() {
    char xpRoot[1024] = {0};
    XPLMGetSystemPath(xpRoot);

    XPLMDataRef uiNameRef = XPLMFindDataRef("sim/aircraft/view/acf_ui_name");
    if (!uiNameRef) {
        XPLMDebugString("FlyOnSpeed: legacy-json: acf_ui_name dataref not found\n");
        return {};
    }
    char uiName[256] = {0};
    XPLMGetDatab(uiNameRef, uiName, 0, sizeof(uiName) - 1);
    if (uiName[0] == '\0') {
        XPLMDebugString("FlyOnSpeed: legacy-json: acf_ui_name is empty\n");
        return {};
    }

    std::string path = xpRoot;
    path += "Custom Data/";
    path += uiName;
    path += ".json";

    XPLMDebugString(("FlyOnSpeed: legacy-json: looking for " + path + "\n").c_str());
    return path;
}

// Try to import a legacy json-config file for the active aircraft.
// Called from OnAircraftLoaded only when no .prf exists yet for this
// aircraft.  Returns true when the four threshold globals were
// overwritten from the legacy file (caller skips the dataref-seed
// path in that case).  Returns false on no-op (file missing, parse
// incomplete, thresholds out of order); caller's seed path runs.
//
// On success: writes the parsed values into the live globals and
// calls SaveSettings.  Only when SaveSettings confirms the .prf was
// written does the legacy file get renamed to `<name>.json.imported`
// — a write failure leaves the legacy file untouched so the import
// retries on the next aircraft load.
static bool TryImportLegacyJson() {
    const std::string legacyPath = buildLegacyJsonPath();
    if (legacyPath.empty()) return false;

    FILE* fp = std::fopen(legacyPath.c_str(), "r");
    if (!fp) return false;   // No legacy file; nothing to do.

    // Read the entire file (at most a few hundred bytes for the
    // legacy format) into memory for parsing.
    std::string body;
    char buf[1024];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), fp)) > 0) {
        body.append(buf, n);
        if (body.size() > 65536) break;   // Sanity cap on file size.
    }
    std::fclose(fp);

    XPLMDebugString(("FlyOnSpeed: legacy json-config found at "
                     + legacyPath + " — attempting import\n").c_str());

    float ldmax = 0.0f, fast = 0.0f, slow = 0.0f, warn = 0.0f, iasEnable = 0.0f;
    const bool gotLdmax = extractJsonFloat(body, "Below LDMax",     ldmax);
    const bool gotFast  = extractJsonFloat(body, "Below OnSpeed",   fast);
    const bool gotSlow  = extractJsonFloat(body, "OnSpeed Max",     slow);
    const bool gotWarn  = extractJsonFloat(body, "Above OnSpeed",   warn);
    // IAS Tone Enable was a float-typed boolean in the legacy format;
    // missing key shouldn't fail the import, just keep current default.
    const bool gotIas   = extractJsonFloat(body, "IAS Tone Enable", iasEnable);

    // The four threshold keys are required; without all four the
    // import is meaningless.  Skip on partial parse — pilot's .prf
    // stays at compiled-in defaults and the legacy file stays in
    // place for them to inspect.
    if (!(gotLdmax && gotFast && gotSlow && gotWarn)) {
        XPLMDebugString("FlyOnSpeed: legacy json-config parse incomplete — "
                        "skipping import (file left in place)\n");
        return false;
    }

    // Order check: LDmax < Fast < Slow < Warn.  A scrambled legacy
    // file shouldn't produce a corrupt new .prf.  Falls back to
    // defaults on a bad file.
    if (!(ldmax < fast && fast < slow && slow < warn)) {
        XPLMDebugString("FlyOnSpeed: legacy json-config thresholds out of "
                        "order — skipping import (file left in place)\n");
        return false;
    }

    fLDMAXAOA       = ldmax;
    fONSPEEDFASTAOA = fast;
    fONSPEEDSLOWAOA = slow;
    fSTALLWARNAOA   = warn;
    // Legacy json had no alpha_stall field; recover it from StallWarn
    // via the canonical 0.92 fraction (same recovery formula the
    // indexer uses if alpha_stall isn't editable).
    fALPHASTALL     = warn /
                      onspeed_xplane::indexer::kCanonicalNaoa.stallWarn;
    if (gotIas) {
        // Legacy "IAS Tone Enable" was a bool meaning "audio gates on
        // IAS."  Map true → the new plugin's default mute-floor of 25
        // kt; false → 0 (no mute).  Pilots can fine-tune via the
        // audio control window after import.
        iMuteAudioUnderIAS = (iasEnable >= 0.5f) ? 25 : 0;
    }
    XPLMDebugString(("FlyOnSpeed: imported legacy thresholds — "
                     "LDmax=" + std::to_string(ldmax)
                     + " Fast=" + std::to_string(fast)
                     + " Slow=" + std::to_string(slow)
                     + " Warn=" + std::to_string(warn)
                     + "\n").c_str());

    // Persist to the new .prf so the import is one-shot.  Gate the
    // legacy-file rename on a confirmed write: if SaveSettings can't
    // open the file or fclose reports a buffered-write error, the
    // .prf may not exist on disk — renaming the legacy file in that
    // state would leave the pilot with neither file on the next
    // load.  Leave the legacy file in place; the next aircraft load
    // retries the import.
    if (!SaveSettings()) {
        XPLMDebugString("FlyOnSpeed: legacy json values applied to live "
                        "state but .prf write failed — legacy file left "
                        "in place, import will retry on next aircraft "
                        "load\n");
        UpdateAOATextFields();
        // Live globals were overwritten with the legacy values; the
        // .prf write failed but the in-memory state IS the imported
        // calibration.  Tell the caller not to reseed.
        return true;
    }

    // Rename the legacy file to <name>.json.imported so this branch
    // doesn't fire again if the pilot wipes their .prf, and so the
    // pilot can revert by renaming back.  Best-effort: a rename
    // failure is logged but doesn't undo the import (the .prf is
    // already on disk).
    const std::string archivedPath = legacyPath + ".imported";
    if (std::rename(legacyPath.c_str(), archivedPath.c_str()) != 0) {
        XPLMDebugString(("FlyOnSpeed: failed to rename legacy json to "
                         + archivedPath
                         + " (import succeeded, manual rename advised)\n")
                        .c_str());
    } else {
        XPLMDebugString(("FlyOnSpeed: archived legacy json to "
                         + archivedPath + "\n").c_str());
    }

    // Refresh widget text fields if the audio control window is open
    // so the pilot sees the imported values immediately.
    UpdateAOATextFields();
    return true;
}

// --------------------------------------------------------------------------
// Validation.
//
// Two layers:
//   1. Per-field range + parse check.  AOA setpoints share the universal
//      [AOA_MIN_VALUE, AOA_MAX_VALUE] from onspeed_core/util/OnSpeedTypes
//      so the plugin and firmware agree on what counts as a sane AOA
//      value at all.  Plugin-only fields (IAS gate, master volume,
//      smoothing) get their own ranges.
//   2. Cross-field ordering: LDmax < OnSpeedFast < OnSpeedSlow <
//      StallWarn.  Delegated to OnSpeedConfig::SuFlaps::SetpointOrderError
//      so the plugin reports the same error wording the firmware uses.

struct ValidatedSettings {
    float fLDMAXAOA;
    float fONSPEEDFASTAOA;
    float fONSPEEDSLOWAOA;
    float fSTALLWARNAOA;
    float fALPHASTALL;
    int   iMuteAudioUnderIAS;
    int   iVs1G;
    int   iMasterVolumePct;
    int   iAoaMedianWindow;
    int   iAoaMeanWindow;
};

// Compiled-in defaults — what "Restore Defaults" reverts to.  These
// are generic GA values; real airframes need to override them via
// Save.  On a fresh aircraft (no .prf yet), OnAircraftLoaded seeds
// the four f*AOA globals from X-Plane's stall datarefs before the
// audio control window opens, so the pilot sees per-airframe values
// instead of these generic defaults — but if the seed declines (no
// stall_warn_alpha), these stand.
constexpr ValidatedSettings kDefaultSettings{
    /*fLDMAXAOA=*/        6.0f,
    /*fONSPEEDFASTAOA=*/  7.3f,
    /*fONSPEEDSLOWAOA=*/  9.6f,
    /*fSTALLWARNAOA=*/   12.5f,
    /*fALPHASTALL=*/     13.6f,  // 12.5 / 0.92, generic
    /*iMuteAudioUnderIAS=*/25,
    /*iVs1G=*/             0,    // 0 = will be re-seeded from acf_Vs on
                                  // aircraft load; see OnAircraftLoaded.
    /*iMasterVolumePct=*/ 100,
    /*iAoaMedianWindow=*/   5,
    /*iAoaMeanWindow=*/    10,
};

// Strict float parse: returns false on anything other than "all of the
// string was consumed by strtof and produced a finite number."  Empty
// strings, garbage, +inf, NaN all fail.
static bool parseFloatStrict(const char* s, float& out) {
    if (s == nullptr || s[0] == '\0') return false;
    char* end = nullptr;
    const float v = std::strtof(s, &end);
    if (end == s || *end != '\0') return false;   // no digits or trailing junk
    if (!std::isfinite(v))       return false;
    out = v;
    return true;
}

static bool parseIntStrict(const char* s, int& out) {
    if (s == nullptr || s[0] == '\0') return false;
    char* end = nullptr;
    const long v = std::strtol(s, &end, 10);
    if (end == s || *end != '\0') return false;
    if (v < INT_MIN || v > INT_MAX) return false;
    out = static_cast<int>(v);
    return true;
}

// In-place visual marker for invalid fields.  Prefix the field's
// descriptor with this so the row stands out without adding extra
// widgets.  Stripped automatically when the user edits the field
// (xpMsg_TextFieldChanged) or on a successful Save.
constexpr const char* kInvalidMarker = "!! ";

static void markFieldInvalid(XPWidgetID w) {
    char buf[64];
    XPGetWidgetDescriptor(w, buf, sizeof(buf));
    if (std::strncmp(buf, kInvalidMarker, std::strlen(kInvalidMarker)) == 0) {
        return;   // already marked
    }
    std::string marked = kInvalidMarker;
    marked += buf;
    XPSetWidgetDescriptor(w, marked.c_str());
}

static void unmarkField(XPWidgetID w) {
    char buf[64];
    XPGetWidgetDescriptor(w, buf, sizeof(buf));
    const size_t prefixLen = std::strlen(kInvalidMarker);
    if (std::strncmp(buf, kInvalidMarker, prefixLen) == 0) {
        XPSetWidgetDescriptor(w, buf + prefixLen);
    }
}

// Read every text field, validate, mark every failing field in-place,
// and return the validated bundle if and only if every field passed.
// On failure, outErr holds the first error (most useful for the
// status line) and outBadCount holds how many fields failed.
//
// All-or-nothing commit: any single failure means none of the live
// variables change.  The user fixes the marked rows and clicks Save
// again.
static std::optional<ValidatedSettings> readAndValidateFields(
    std::string& outErr, int& outBadCount)
{
    ValidatedSettings v{};
    outBadCount = 0;
    char buf[32];

    auto setError = [&](const std::string& msg) {
        if (outErr.empty()) outErr = msg;   // remember first error
        ++outBadCount;
    };

    auto readFloat = [&](XPWidgetID w, const char* label,
                         float lo, float hi, float& out) -> bool {
        XPGetWidgetDescriptor(w, buf, sizeof(buf));
        // Tolerate (but ignore) an existing marker — the user may
        // re-Save without first clearing it.
        const char* effective = buf;
        if (std::strncmp(buf, kInvalidMarker,
                         std::strlen(kInvalidMarker)) == 0) {
            effective = buf + std::strlen(kInvalidMarker);
        }
        if (!parseFloatStrict(effective, out)) {
            setError(std::string(label) + " is not a number");
            markFieldInvalid(w);
            return false;
        }
        if (out < lo || out > hi) {
            char msg[128];
            std::snprintf(msg, sizeof(msg),
                          "%s out of range [%.1f, %.1f]", label, lo, hi);
            setError(msg);
            markFieldInvalid(w);
            return false;
        }
        unmarkField(w);
        return true;
    };
    auto readInt = [&](XPWidgetID w, const char* label,
                       int lo, int hi, int& out) -> bool {
        XPGetWidgetDescriptor(w, buf, sizeof(buf));
        const char* effective = buf;
        if (std::strncmp(buf, kInvalidMarker,
                         std::strlen(kInvalidMarker)) == 0) {
            effective = buf + std::strlen(kInvalidMarker);
        }
        if (!parseIntStrict(effective, out)) {
            setError(std::string(label) + " is not an integer");
            markFieldInvalid(w);
            return false;
        }
        if (out < lo || out > hi) {
            char msg[128];
            std::snprintf(msg, sizeof(msg),
                          "%s out of range [%d, %d]", label, lo, hi);
            setError(msg);
            markFieldInvalid(w);
            return false;
        }
        unmarkField(w);
        return true;
    };

    bool ok = true;

    // AOA setpoints are bounded by the universal AOA range.  Setpoints
    // above zero only — LDmax can't be 0 in any normal airframe.  AOA
    // *measurements* can be negative (the plugin's AOA dataref reads
    // wing AOA, which goes negative in pushovers / inverted flight),
    // but the four setpoint thresholds all sit on the lifting side
    // and must be positive.  Upper bound matches onspeed_core's
    // universal AOA_MAX_VALUE so plugin + firmware agree on the
    // notion of "valid AOA value at all."
    ok &= readFloat(widgetLDMaxAOA,       "LDmax AOA",        0.0f,
                    onspeed::AOA_MAX_VALUE, v.fLDMAXAOA);
    ok &= readFloat(widgetOnSpeedFastAOA, "OnSpeed Fast AOA", 0.0f,
                    onspeed::AOA_MAX_VALUE, v.fONSPEEDFASTAOA);
    ok &= readFloat(widgetOnSpeedSlowAOA, "OnSpeed Slow AOA", 0.0f,
                    onspeed::AOA_MAX_VALUE, v.fONSPEEDSLOWAOA);
    ok &= readFloat(widgetStallWarnAOA,   "Stall Warn AOA",   0.0f,
                    onspeed::AOA_MAX_VALUE, v.fSTALLWARNAOA);
    ok &= readFloat(widgetAlphaStall,     "Alpha Stall",      0.0f,
                    onspeed::AOA_MAX_VALUE, v.fALPHASTALL);
    ok &= readInt(widgetMuteAudioUnderIAS, "Mute Under IAS",
                  0, 250, v.iMuteAudioUnderIAS);
    // Vs (1G) sentinels:
    //    0 = "auto"     — re-seed from acf_Vs on aircraft load
    //   -1 = "disabled" — pilot opted out, never auto-seed
    //   >0 = explicit Vs in KIAS
    // Both 0 and -1 fall back to the alpha path; the difference is
    // whether OnAircraftLoaded re-seeds.  Upper bound 250 matches
    // Mute Under IAS for consistency.
    ok &= readInt(widgetVs1G,              "Vs (1G)",
                  -1, 250, v.iVs1G);
    ok &= readInt(widgetMasterVolumePct,   "Master Volume",
                  0, 100, v.iMasterVolumePct);
    ok &= readInt(widgetAoaMedianWindow,   "AOA Median Window",
                  1, 100, v.iAoaMedianWindow);
    ok &= readInt(widgetAoaMeanWindow,     "AOA Mean Window",
                  1, 100, v.iAoaMeanWindow);

    if (!ok) return std::nullopt;

    // Cross-field: ordering check via the firmware's own validator so
    // the plugin and firmware report the same error for the same
    // mistake (e.g., "OnSpeedFast must be less than OnSpeedSlow").
    // Mark every field that participates in an ordering violation —
    // we don't know which value the user mis-typed, but at least all
    // four are visually flagged.
    onspeed::config::OnSpeedConfig::SuFlaps flap;
    flap.fLDMAXAOA       = v.fLDMAXAOA;
    flap.fONSPEEDFASTAOA = v.fONSPEEDFASTAOA;
    flap.fONSPEEDSLOWAOA = v.fONSPEEDSLOWAOA;
    flap.fSTALLWARNAOA   = v.fSTALLWARNAOA;
    const std::string orderErr = flap.SetpointOrderError();
    if (!orderErr.empty()) {
        setError(orderErr);
        markFieldInvalid(widgetLDMaxAOA);
        markFieldInvalid(widgetOnSpeedFastAOA);
        markFieldInvalid(widgetOnSpeedSlowAOA);
        markFieldInvalid(widgetStallWarnAOA);
        return std::nullopt;
    }
    // alpha_stall must sit above StallWarn — it's the literal-stall
    // anchor; the four setpoints are scaled fractions of it.  StallWarn
    // at or above alpha_stall would put the StallWarn pip at or beyond
    // 100% on the indexer.
    if (v.fALPHASTALL <= v.fSTALLWARNAOA) {
        setError("Alpha Stall must be greater than Stall Warn AOA");
        markFieldInvalid(widgetAlphaStall);
        markFieldInvalid(widgetStallWarnAOA);
        return std::nullopt;
    }

    return v;
}

// Apply a validated settings bundle to the live state, rebuilding
// dependent state (smoothers).  Refreshes widget text so the user
// sees the canonical formatting.  Doesn't save — caller decides.
static void ApplyValidatedSettings(const ValidatedSettings& v) {
    fLDMAXAOA          = v.fLDMAXAOA;
    fONSPEEDFASTAOA    = v.fONSPEEDFASTAOA;
    fONSPEEDSLOWAOA    = v.fONSPEEDSLOWAOA;
    fSTALLWARNAOA      = v.fSTALLWARNAOA;
    fALPHASTALL        = v.fALPHASTALL;
    iMuteAudioUnderIAS = v.iMuteAudioUnderIAS;
    iVs1G              = v.iVs1G;
    iMasterVolumePct   = v.iMasterVolumePct;
    if (v.iAoaMedianWindow != iAoaMedianWindow ||
        v.iAoaMeanWindow   != iAoaMeanWindow) {
        iAoaMedianWindow = v.iAoaMedianWindow;
        iAoaMeanWindow   = v.iAoaMeanWindow;
        rebuildAoaSmoothers();
    }
    UpdateAOATextFields();
}

// Refresh the settings path for the currently-loaded aircraft, load
// any saved file, rebuild dependent state (smoothers, widget text).
// Called from init_sound (the deferred flight-loop callback that also
// brings up OpenAL) so XPLMGetNthAircraftModel is guaranteed to return
// a real aircraft name, and from XPLM_MSG_PLANE_LOADED so a mid-sim
// aircraft change re-points at the new aircraft's .prf file.
static void OnAircraftLoaded() {
    const std::string previous = s_SettingsPath;
    s_SettingsPath = buildSettingsPath();

    if (s_SettingsPath.empty()) {
        XPLMDebugString("FlyOnSpeed: OnAircraftLoaded: no aircraft yet "
                        "(XPLMGetNthAircraftModel returned empty)\n");
    } else if (s_SettingsPath != previous) {
        XPLMDebugString(("FlyOnSpeed: settings path = "
                         + s_SettingsPath + "\n").c_str());
        // Park the load-gate while we point at the new aircraft's
        // .prf and read it.  Any stray menu click during this window
        // hits SaveSettings's gate and is skipped.  LoadSettings
        // flips the flag back on exit.
        s_settingsLoaded = false;
        // Reset iVs1G to its compiled default before reading the new
        // aircraft's .prf.  Without this, the prior aircraft's value
        // bleeds through when the new .prf doesn't have an `iVs1G`
        // key — which silently disables auto-seeding when the prior
        // aircraft had iVs1G == -1 ("user disabled V²"), and silently
        // skips the seed when iVs1G > 0 too.  LoadSettings's tolerant
        // parser leaves any global untouched if its key isn't present
        // in the file, so we have to explicitly clear iVs1G ourselves.
        iVs1G = kDefaultSettings.iVs1G;
        // Reset f*AOA to compiled defaults so a previous aircraft's
        // values don't bleed through to a fresh-aircraft seed when
        // the seed-from-datarefs path declines (acf_stall_warn_alpha
        // missing).
        fLDMAXAOA       = kDefaultSettings.fLDMAXAOA;
        fONSPEEDFASTAOA = kDefaultSettings.fONSPEEDFASTAOA;
        fONSPEEDSLOWAOA = kDefaultSettings.fONSPEEDSLOWAOA;
        fSTALLWARNAOA   = kDefaultSettings.fSTALLWARNAOA;
        fALPHASTALL     = kDefaultSettings.fALPHASTALL;

        // Reset window state to compile-time defaults so a previous
        // aircraft's state doesn't bleed through when the new .prf
        // is absent or partial.  Without this, a fresh aircraft load
        // inherits the prior aircraft's audio-window position +
        // visibility + indexer state, and the first SaveSettings
        // bakes those into the new .prf — looking like "settings
        // carried across planes" rather than per-aircraft.
        s_audioWindow = AudioWindowState{};
#ifdef ENABLE_M5_INDEXER
        indexerSettings = onspeed_xplane::indexer::PersistedState{};
#endif

        const bool prfFound = LoadSettings();

        // No .prf yet for this aircraft: try importing settings from
        // a legacy json-config file in `Custom Data/<acf>.json` if
        // one exists.  TryImportLegacyJson is a no-op when no legacy
        // file is present, when parsing is incomplete, or when the
        // thresholds are out of order.
        //
        // If neither a .prf nor a legacy json yields setpoints, seed
        // the four f*AOA globals from X-Plane's stall datarefs
        // (acf_stall_warn_alpha + acf_Vs + acf_Vso) so a fresh
        // aircraft has plausible per-airframe defaults instead of
        // generic compiled-in values.  The pilot's first Save in the
        // audio control window persists those values; from then on
        // the .prf wins and seed-from-datarefs isn't called again
        // for this aircraft.
        if (!prfFound) {
            XPLMDebugString("FlyOnSpeed: no .prf for this aircraft; "
                            "trying legacy json import\n");
            const bool importedJson = TryImportLegacyJson();
            if (!importedJson) {
                XPLMDebugString("FlyOnSpeed: legacy json import did "
                                "not apply; falling back to dataref seed\n");
                SeedSetpointsFromDatarefs();
            }
        } else {
            XPLMDebugString("FlyOnSpeed: .prf found for this aircraft; "
                            "skipping legacy json import\n");
        }
    }

    // Seed iVs1G from acf_Vs if the .prf hasn't supplied a value
    // (iVs1G == 0 sentinel — "auto").  Plane Maker stores it in
    // KIAS as a per-aircraft constant; non-zero only for aircraft
    // whose author populated the field.  Pilot's manual override
    // (set via the audio control window and persisted) wins because
    // LoadSettings would have set iVs1G > 0 (or -1 for "disabled")
    // already, gating this branch off.  We round here rather than
    // truncate so an acf value of e.g. 62.7 becomes 63 instead of 62.
    //
    // Unit (KIAS) verified against three independent open-source
    // X-Plane plugins, all of which treat acf_Vs identically to its
    // KIAS-annotated siblings (acf_Vne / acf_Vno / acf_Vfe / acf_Vso):
    //   - vranki/ExtPlane simulator returns 100 for acf_Vs (knots,
    //     since 100 m/s ≈ 194 kt is implausible for any GA stall).
    //   - vranki/ExtPlane-Panel AirspeedIndicator.qml feeds acf_Vs
    //     through the same uVelocityKnots scaleFactor as acf_Vne
    //     and acf_Vno (which DataRefs.txt confirms are kias).
    //   - blauret/pyG5 (pyG5Network.py) explicitly tags acf_Vs
    //     with the unit string "kt".
    // X-Plane's DataRefs.txt leaves the unit column blank for
    // acf_Vs (a documentation gap, not a unit difference) — every
    // other V-speed in that file annotated "kias" sits next to a
    // blank-or-kias acf_Vs row treated as kias in practice.
    //
    // Defensive: if acf_Vs comes through < 25, refuse to seed.  A
    // KIAS Vs below 25 kt is implausible for any real aircraft
    // (the slowest powered ultralights are around 28-30), but a
    // m/s Vs of 20-25 m/s is a perfectly plausible C172 (~40 kt =
    // 20.6 m/s).  If a future X-Plane version starts returning m/s
    // here we want to skip the seed and let the pilot enter Vs
    // by hand rather than auto-seed wildly wrong KIAS.
    if (iVs1G == 0 && acfVsDataRef) {
        const float acfVs = XPLMGetDataf(acfVsDataRef);
        if (acfVs > 0.0f && acfVs < 25.0f) {
            char buf[200];
            std::snprintf(buf, sizeof(buf),
                "FlyOnSpeed: acf_Vs=%.1f looks like m/s (KIAS expected); "
                "skipping auto-seed — set Vs manually via the audio "
                "control window\n", acfVs);
            XPLMDebugString(buf);
        } else if (acfVs >= 25.0f && acfVs < 250.0f) {
            iVs1G = static_cast<int>(std::round(acfVs));
            XPLMDebugString(("FlyOnSpeed: seeded iVs1G="
                             + std::to_string(iVs1G)
                             + " from acf_Vs\n").c_str());
            // Persist immediately so a future plugin reload doesn't
            // re-seed against a different acf the pilot hadn't yet
            // calibrated by hand.
            SaveSettings();
        }
    }

    rebuildAoaSmoothers();
    UpdateAOATextFields();

    // Drop the prior aircraft's captured acf_has_stallwarn value; the
    // next mute-on tick in CheckAOAAndPlayTone re-captures from whatever
    // is live on the new aircraft.  Assumes XPLM_MSG_PLANE_LOADED fires
    // after the new .acf has been applied to the dataref — likely true
    // and consistent with the indexer's deferred-restore pattern, but
    // the SDK doesn't pin this down explicitly.  If a future X-Plane
    // version were to fire PLANE_LOADED before the .acf-driven dataref
    // values land, the next capture would lock in our previously-written
    // 0 as the "original," and toggle-off would leave the new aircraft
    // muted until next reload.  Defer-to-AIRPORT_LOADED (the existing
    // s_indexerRestorePending pattern) is the fix if this ever bites.
    // Reset the captured original-stall-horn-state so a new aircraft's
    // stall-horn setting gets re-captured the first time the in-panel
    // toggle runs (handled by CheckAOAAndPlayTone's first write).
    g_iAcfHasStallwarnOriginal = -1;
#ifdef ENABLE_M5_INDEXER
    // Open the configured USB-serial port if one is set.  Failures
    // are logged inside OpenSerialOut and just leave the port closed
    // (e.g. user changed aircraft and the saved port path no longer
    // exists, or the M5 isn't currently plugged in).
    if (!sSerialPortPath.empty()) {
        onspeed_xplane::indexer::OpenSerialOut(sSerialPortPath);
    } else {
        onspeed_xplane::indexer::CloseSerialOut();
    }

    // Set mode immediately (cheap; just an int the M5 firmware reads).
    // Geometry + pop-out + visibility restore is deferred to a
    // flight-loop callback after AIRPORT_LOADED.  XPluginReceiveMessage
    // sets s_indexerRestorePending; the periodic save callback
    // observes it and calls ApplyPersistedState exactly once on its
    // next tick.
    onspeed_xplane::indexer::SetMode(indexerSettings.mode);
#endif

    // Audio control window: if the .prf had it open at last save,
    // reopen it now at its saved geometry.  Widgets are lighter than
    // the indexer's lazy-init path, so creating one from this flight-
    // loop callback is safe (no SDL / M5GFX involved).
    if (s_audioWindow.visible && !audioControlWidget) {
        ClampAudioWindowOnScreen();
        CreateAudioControlWindow(s_audioWindow.left,  s_audioWindow.top,
                                 s_audioWindow.width, s_audioWindow.height);
        UpdateAOATextFields();
        // Flush any clamp-induced position change to the .prf
        // immediately, instead of waiting on the periodic save tick.
        // Without this, a quit-within-1s-of-aircraft-load loses the
        // corrected position and re-clamps next boot from the same
        // off-screen .prf value.  Mirrors the "Show" menu handler's
        // flush at line ~1759.
        RefreshAudioWindowState();
        if (AudioWindowChanged()) {
            SaveSettings();
        }
    } else if (s_audioWindow.visible && audioControlWidget) {
        // New aircraft's .prf says the panel was open.  The widget
        // already exists from the prior aircraft's session — show it
        // in case it was hidden during that session.
        XPShowWidget(audioControlWidget);
        UpdateAOATextFields();
    } else if (!s_audioWindow.visible && audioControlWidget) {
        // New aircraft's .prf says the panel was closed.  Hide the
        // existing widget so it matches the saved state (the previous
        // aircraft's session had it open).
        XPHideWidget(audioControlWidget);
    }
}

// Audio render thread.  Owns the OpenAL queue pump; reads g_Engine
// state under g_EngineMutex once per chunk, then renders sample-
// accurate PCM via Synthesize + Envelope + Mix.
std::optional<std::thread> audioThread;
std::atomic<bool>          threadRunning{false};

// Engine state shared between the X-Plane flight loop (writer) and
// the audio render thread (reader).  Guarded by g_EngineMutex; the
// crit-sec is microseconds — just an Envelope spec swap and a couple
// of float assignments — so contention is irrelevant.
struct AudioEngine
{
    onspeed::audio::Envelope    envelope;
    onspeed::audio::MixerState  mixerState;
    float                       carrierPhase = 0.0f;       // [-pi, pi]

    // Two carrier identities so a Low → High switch (or vice versa)
    // doesn't bleed audio onto the wrong frequency during the prior
    // tone's release ramp:
    //   - activeCarrier  = the cosine table the render thread is
    //                      currently synthesizing.  Held stable while
    //                      the envelope is non-idle so the release
    //                      tail decays on the same carrier it started.
    //   - pendingCarrier = the most recent caller request from
    //                      PlayAOATone.  Promoted to activeCarrier
    //                      only when the envelope reaches Idle (release
    //                      has fully drained), at which point we also
    //                      reset carrierPhase to start the new carrier
    //                      from a known sample so any frequency
    //                      mismatch between the two stays inaudible.
    // Mirrors the firmware's s_LastEnvTone / enTone split.
    onspeed::EnToneType         activeCarrier  = onspeed::EnToneType::None;
    onspeed::EnToneType         pendingCarrier = onspeed::EnToneType::None;
    float                       volumeMult = onspeed::STALL_VOL_MIN;
    bool                        muted = true;              // IAS/audio-toggle gate

    // Stereo pan from lateral G (slip-skid centripetal cue).  Computed
    // each flight loop from sim/flightmodel/forces/g_side via
    // onspeed::audio::Apply3DPan; consumed in the render thread as the
    // L/R scale into AudioMixer.  Headphone-stereo only, never OpenAL
    // 3D positioning — the firmware uses the same approach.
    float                       leftPanGain  = 1.0f;
    float                       rightPanGain = 1.0f;
};
AudioEngine g_Engine;
std::mutex  g_EngineMutex;

// Persistent state for the panning IIR.  Lives outside g_Engine because
// it's only touched from the X-Plane flight-loop thread (writer) and
// never read from the audio thread — the smoothed result is pushed
// into g_Engine.{leftPanGain,rightPanGain} under the mutex.
onspeed::audio::PanState  g_PanState;
const onspeed::audio::PanConfig g_PanCfg;   // defaults match firmware

// Orchestrator config: same defaults as the firmware (Gen2 timings),
// resampled into the plugin's audio rate.
const onspeed::audio::OrchestratorConfig g_OrchCfg = []() {
    onspeed::audio::OrchestratorConfig cfg;
    cfg.sampleRateHz = kAudioSampleRateHz;
    return cfg;
}();

// Widget layout constants for the control window.
static int       g_NextWidgetTop = 0;     // running cursor for stacking
static const int kWidgetHeight   = 20;
static const int kWidgetMargin   = 5;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Initialize OpenAL device/context and the streaming buffer pool.
// Called via XPLMRegisterFlightLoopCallback so OpenAL init happens
// after X-Plane's audio subsystem is up.
static float init_sound([[maybe_unused]] float elapsed,
                        [[maybe_unused]] float elapsed_sim,
                        [[maybe_unused]] int counter,
                        [[maybe_unused]] void * ref)
{
    device = alcOpenDevice(nullptr);
    XPLMDebugString("FlyOnSpeed: Initializing audio device\n");
    if (!device) {
        XPLMDebugString("FlyOnSpeed: Failed to open device\n");
        return 0.0f;
    }

    context = alcCreateContext(device, nullptr);
    XPLMDebugString("FlyOnSpeed: Creating audio context\n");
    if (!context) {
        XPLMDebugString("FlyOnSpeed: Failed to create context\n");
        alcCloseDevice(device);
        device = nullptr;
        return 0.0f;
    }

    alcMakeContextCurrent(context);

    alGenSources(1, &audioSource);
    alGenBuffers(kStreamBufferCount, streamBuffers);
    alSourcef(audioSource, AL_GAIN, DEFAULT_VOLUME);

    // Pre-fill all buffers with silence and queue them so the source
    // has something to play from first AL_PLAYING.  The render thread
    // unqueues + refills as the source consumes them.
    int16_t silence[kFramesPerChunk * 2] = {0};   // stereo
    for (int i = 0; i < kStreamBufferCount; ++i) {
        alBufferData(streamBuffers[i], AL_FORMAT_STEREO16, silence,
                     sizeof(silence), kAudioSampleRateHz);
    }
    alSourceQueueBuffers(audioSource, kStreamBufferCount, streamBuffers);
    alSourcePlay(audioSource);

    // Spawn the audio render thread *after* the OpenAL source and
    // buffers exist — the thread immediately calls alGetSourcei /
    // alSourceUnqueueBuffers and would error against an invalid source
    // ID if started in XPluginStart (which runs before X-Plane fires
    // this flight-loop callback).
    threadRunning = true;
    audioThread.emplace(AudioRenderThread);

    // Settings load is also deferred to here: XPLMGetNthAircraftModel
    // returns an empty filename if called from XPluginStart before
    // X-Plane has finished bringing the user aircraft up.  The first
    // flight-loop tick is the earliest moment we can trust it.
    OnAircraftLoaded();

#ifdef ENABLE_M5_INDEXER
    // M5 indexer is also deferred so the dataref lookups in the
    // adapter find their refs.  Init creates the X-Plane window but
    // leaves it hidden until the user toggles it via the menu.
    onspeed_xplane::indexer::InitDataRefs();
    onspeed_xplane::indexer::Init();
#endif

    return 0.0f;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Status line write priority.
//
// PlayAOATone writes the status line every flight loop (~30-60 Hz),
// describing the current audio state.  Save / Restore / validation
// errors also want to write the status line, but if PlayAOATone runs
// in the next millisecond it overwrites them — the message vanishes
// before the pilot sees it.
//
// Resolution: setStatusSticky() stamps a deadline; any plain status
// write from PlayAOATone before that deadline is dropped.  Three
// seconds is long enough to read but short enough to not strand the
// pilot looking at a stale "Saved." after they've changed something.
constexpr float kStickyStatusSeconds = 3.0f;
static float s_StickyStatusDeadline = 0.0f;

static void setStatusSticky(const char* text) {
    XPSetWidgetDescriptor(widgetAudioStatus, text);
    s_StickyStatusDeadline = XPLMGetElapsedTime() + kStickyStatusSeconds;
}

static void setStatusOrIfSticky(const char* text) {
    if (XPLMGetElapsedTime() < s_StickyStatusDeadline) return;
    XPSetWidgetDescriptor(widgetAudioStatus, text);
}

// Strip an in-place "!! " invalid marker from a field as soon as the
// user starts editing it, so the visual flag clears the moment the
// pilot reacts to it.  Called from xpMsg_TextFieldChanged.
static void clearMarkerOnEdit(XPWidgetID w) {
    char buf[64];
    XPGetWidgetDescriptor(w, buf, sizeof(buf));
    const size_t prefixLen = std::strlen(kInvalidMarker);
    if (std::strncmp(buf, kInvalidMarker, prefixLen) != 0) return;
    // The user just typed a character.  Don't strip the marker if the
    // remaining text is still the marker itself or empty — wait until
    // there's something past it (means they actually edited the value).
    if (buf[prefixLen] == '\0') return;
    XPSetWidgetDescriptor(w, buf + prefixLen);
}

// Control-window widget message handler.
static int AudioControlHandler(
    XPWidgetMessage inMessage,
    [[maybe_unused]] XPWidgetID inWidget,
    intptr_t inParam1,
    [[maybe_unused]] intptr_t inParam2)
{
    if (inMessage == xpMessage_CloseButtonPushed) {
        XPHideWidget(audioControlWidget);
        // Refresh after hiding so s_audioWindow.visible reflects the
        // close *and* any unflushed drag/resize the periodic poll
        // hadn't picked up yet.  Persist if anything changed so a
        // reboot reopens at the right place — or stays closed.
        RefreshAudioWindowState();
        if (AudioWindowChanged()) {
            SaveSettings();
        }
        return 1;
    }

    // Clear an invalid marker as soon as the user edits the field.
    if (inMessage == xpMsg_TextFieldChanged) {
        clearMarkerOnEdit(reinterpret_cast<XPWidgetID>(inParam1));
        return 0;   // let widget continue normal processing
    }

    if (inMessage == xpMsg_PushButtonPressed) {
        if (inParam1 == reinterpret_cast<intptr_t>(audioToggleCheckbox)) {
            audioEnabled = !audioEnabled;
            XPSetWidgetDescriptor(audioToggleCheckbox,
                                  audioEnabled ? "OnSpeed Tones: On" : "OnSpeed Tones: Off");
            SaveSettings();
            return 1;
        }
        // X-Plane stall horn toggle: flips bMuteStallHorn and refreshes
        // the button label.  Label reads "on" when the sim's horn plays
        // and "off" when this plugin is muting it.  When toggling back
        // to "on" we restore the .acf's original acf_has_stallwarn
        // value (captured the first time we muted) — without that, the
        // sim would treat the horn as permanently disabled until the
        // next aircraft reload, since CheckAOAAndPlayTone only
        // overwrites the dataref while bMuteStallHorn is true.
        else if (inParam1 == reinterpret_cast<intptr_t>(widgetButtonStallHorn)) {
            bMuteStallHorn = !bMuteStallHorn;
            XPSetWidgetDescriptor(widgetButtonStallHorn,
                bMuteStallHorn ? "X-Plane Stall Horn: Off"
                              : "X-Plane Stall Horn: On");
            if (!bMuteStallHorn && acfHasStallwarnDataRef &&
                g_iAcfHasStallwarnOriginal >= 0) {
                XPLMSetDatai(acfHasStallwarnDataRef, g_iAcfHasStallwarnOriginal);
            }
            SaveSettings();
            return 1;
        }
        // Save: validate every field, commit to live state, persist.
        // Failures mark the offending fields in-place and leave the
        // live state untouched (all-or-nothing commit).
        else if (inParam1 == reinterpret_cast<intptr_t>(widgetButtonSave)) {
            std::string err;
            int badCount = 0;
            auto v = readAndValidateFields(err, badCount);
            if (!v) {
                char status[160];
                std::snprintf(status, sizeof(status),
                              "Invalid (%d): %s", badCount, err.c_str());
                setStatusSticky(status);
                return 1;
            }
            ApplyValidatedSettings(*v);
            SaveSettings();
            setStatusSticky("Saved.");
            return 1;
        }
        // Restore Defaults: revert the form to compiled-in defaults
        // for this aircraft.  Doesn't auto-save — pilot can preview,
        // tweak, then click Save.
        else if (inParam1 ==
                 reinterpret_cast<intptr_t>(widgetButtonRestoreDefaults)) {
            ApplyValidatedSettings(kDefaultSettings);
            setStatusSticky("Defaults restored — click Save to persist.");
            return 1;
        }
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Stack a single-row widget below the previously-added one.
static XPWidgetID createWidget(int widgetClass, const char* description, int leftOffset = 20, int width = 140) {
    if (audioControlWidget == nullptr) {
        return nullptr;
    }
    
    // Get the main window dimensions
    int left, top, right, bottom;
    XPGetWidgetGeometry(audioControlWidget, &left, &top, &right, &bottom);
    
    // If this is the first widget, start from the top
    if (g_NextWidgetTop == 0) {
        g_NextWidgetTop = top - 15;  // Initial offset from top
    } else {
        g_NextWidgetTop -= (kWidgetHeight + kWidgetMargin);  // Space between widgets
    }
    
    XPWidgetID newWidget = XPCreateWidget(
        left + leftOffset,                    // left
        g_NextWidgetTop,                     // top
        left + leftOffset + width,            // right
        g_NextWidgetTop - kWidgetHeight,     // bottom
        1,                                    // visible
        description,                          // descriptor
        0,                                    // not root
        audioControlWidget,                   // container
        widgetClass                           // class
    );

    return newWidget;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Layout constants for labeled-field rows.  Label gets enough room
// for the longest caption ("OnSpeed Slow AOA:", "Mute Under IAS (kt):");
// field is sized for ~5-char numeric values.
constexpr int kLabelWidth      = 150;
constexpr int kLabelFieldGap   = 8;
constexpr int kFieldWidth      = 60;

// Add a label + text-field pair as one stacked row.  initialText is
// caller-formatted (use snprintf "%.1f" for floats, "%d" for ints)
// so each field controls its own display format.  When outLabel is
// non-null, the caller receives the label-caption handle too — used
// for mode-aware show/hide so both halves of the row toggle together.
static XPWidgetID createLabeledTextField(const char* label,
                                         const char* initialText,
                                         int leftOffset = 20,
                                         XPWidgetID* outLabel = nullptr) {
    if (audioControlWidget == nullptr) {
        return nullptr;
    }

    int left, top, right, bottom;
    XPGetWidgetGeometry(audioControlWidget, &left, &top, &right, &bottom);

    g_NextWidgetTop -= (kWidgetHeight + kWidgetMargin);

    // Caption: passive label to the left of the field.  When the
    // caller didn't ask for the handle, the parent window still owns
    // the caption and tears it down on close.
    XPWidgetID labelCaption = XPCreateWidget(
        left + leftOffset,                              // left
        g_NextWidgetTop,                                // top
        left + leftOffset + kLabelWidth,                // right
        g_NextWidgetTop - kWidgetHeight,                // bottom
        1, label, 0, audioControlWidget,
        xpWidgetClass_Caption);
    if (outLabel) *outLabel = labelCaption;

    XPWidgetID textField = XPCreateWidget(
        left + leftOffset + kLabelWidth + kLabelFieldGap,                // left
        g_NextWidgetTop,                                                  // top
        left + leftOffset + kLabelWidth + kLabelFieldGap + kFieldWidth,   // right
        g_NextWidgetTop - kWidgetHeight,                                  // bottom
        1, "", 0, audioControlWidget,
        xpWidgetClass_TextField);

    XPSetWidgetProperty(textField, xpProperty_TextFieldType, xpTextEntryField);
    XPSetWidgetProperty(textField, xpProperty_Enabled, 1);
    XPSetWidgetDescriptor(textField, initialText);

    return textField;
}

// Convenience wrappers around createLabeledTextField.  Floats render
// to one decimal; ints render with no decimal.  outLabel mirrors the
// underlying helper for mode-aware visibility tracking.
static XPWidgetID createLabeledFloatField(const char* label, float value,
                                          XPWidgetID* outLabel = nullptr) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f", value);
    return createLabeledTextField(label, buf, /*leftOffset=*/20, outLabel);
}

static XPWidgetID createLabeledIntField(const char* label, int value,
                                        XPWidgetID* outLabel = nullptr) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", value);
    return createLabeledTextField(label, buf, /*leftOffset=*/20, outLabel);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Build the control window and lay out every widget.
static void CreateAudioControlWindow(int x, int y, int w, int h) {
    int x2 = x + w;
    int y2 = y - h;

    // Reset the g_NextWidgetTop for new window
    g_NextWidgetTop = 0;

    // Create main window
    audioControlWidget = XPCreateWidget(x, y, x2, y2,
        1,  // Visible
        "Fly On Speed", // desc
        1,  // root
        nullptr, // no container
        xpWidgetClass_MainWindow);

    XPSetWidgetProperty(audioControlWidget, xpProperty_MainWindowHasCloseBoxes, 1);

    // Create version label
    char versionText[50];
    snprintf(versionText, sizeof(versionText), "OnSpeed %s (%s)",
             BuildInfo::version, BuildInfo::gitShortSha);
    createWidget(
        xpWidgetClass_Caption,
        versionText
    );

    widgetAOAValue = createWidget(
        xpWidgetClass_Caption,
        ""  // Empty descriptor - will be updated with AOA value
    );

    widgetAudioStatus = createWidget(
        xpWidgetClass_Caption,
        ""
    );

    // Master volume (0-100 %).  Mirrors the firmware's pot.  First
    // editable field — the most-touched setting goes at the top so
    // pilots don't have to scroll past calibration to reach it.
    widgetMasterVolumePct   = createLabeledIntField(
        "Master Volume (%):", iMasterVolumePct);

    // AOA setpoints (firmware terminology: see Config.h::SuFlaps).
    // Pilot-set and fixed across all flaps.  Seeded from X-Plane's
    // stall datarefs on first aircraft load (see SeedSetpointsFromDatarefs);
    // saved values overwrite the seed on subsequent loads.
    XPWidgetID manualLabel = nullptr;
    widgetLDMaxAOA       = createLabeledFloatField("LDmax AOA:",
                                                   fLDMAXAOA,
                                                   &manualLabel);
    widgetOnSpeedFastAOA = createLabeledFloatField("OnSpeed Fast AOA:",
                                                   fONSPEEDFASTAOA,
                                                   &manualLabel);
    widgetOnSpeedSlowAOA = createLabeledFloatField("OnSpeed Slow AOA:",
                                                   fONSPEEDSLOWAOA,
                                                   &manualLabel);
    widgetStallWarnAOA   = createLabeledFloatField("Stall Warn AOA:",
                                                   fSTALLWARNAOA,
                                                   &manualLabel);
    // alpha_stall: the wing AOA at literal stall (100% on the indexer
    // scale).  Seeded from acf_stall_warn_alpha / 0.92 — pilot can
    // tune to match a hand-flown stall test.
    widgetAlphaStall     = createLabeledFloatField("Alpha Stall:",
                                                   fALPHASTALL,
                                                   &manualLabel);

    // ---- Common settings (always visible) -----------------------
    // IAS gate (firmware: iMuteAudioUnderIAS; 0 = never mute).
    widgetMuteAudioUnderIAS = createLabeledIntField(
        "Mute Under IAS (kt):", iMuteAudioUnderIAS);

    // Vs at 1G (KIAS).  Used by the on-ground percent-lift formula
    // to derive percent-of-stall from V² when the gear is loaded.
    // Sentinels: 0 = auto-seed from acf_Vs on aircraft load,
    // -1 = explicitly disabled (pilot opted out), >0 = explicit
    // value.  Both 0 and -1 fall back to alpha; -1 is sticky across
    // aircraft loads, 0 is not.
    widgetVs1G              = createLabeledIntField(
        "Vs at 1G (kt):", iVs1G);

    // Plugin-only AOA smoothing (no firmware analog — the firmware
    // smooths upstream in pressure-space).  1 disables.
    widgetAoaMedianWindow   = createLabeledIntField(
        "AOA Median Window:", iAoaMedianWindow);
    widgetAoaMeanWindow     = createLabeledIntField(
        "AOA Mean Window:",   iAoaMeanWindow);

    // Save: validate every field, commit to live state, persist to
    // disk for the current aircraft.  Invalid fields get a "!! "
    // prefix marker; the status line shows the first error.
    widgetButtonSave = createWidget(xpWidgetClass_Button, "Save");

    // Restore Defaults: revert every field to the compiled-in defaults
    // (does not save automatically — the user can preview, tweak, then
    // Save).  Useful escape after experimentation goes wrong.
    widgetButtonRestoreDefaults = createWidget(
        xpWidgetClass_Button, "Restore Defaults");

    audioToggleCheckbox = createWidget(
        xpWidgetClass_Button,
        audioEnabled ? "OnSpeed Tones: On" : "OnSpeed Tones: Off"
    );

    // X-Plane stall horn toggle.  Label reads "on" when the sim's
    // built-in horn is allowed to play, "off" when this plugin is
    // muting it (the more-common pilot preference once OnSpeed audio
    // takes over the role).  Inverts bMuteStallHorn for display.
    widgetButtonStallHorn = createWidget(
        xpWidgetClass_Button,
        bMuteStallHorn ? "X-Plane Stall Horn: Off" : "X-Plane Stall Horn: On"
    );

    XPAddWidgetCallback(audioControlWidget, AudioControlHandler);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Refresh every editable field's text to its current variable value.
// Called after the Update Values button commits, and any time the
// window becomes visible.
static void UpdateAOATextFields() {
    if (!audioControlWidget || !XPIsWidgetVisible(audioControlWidget)) {
        return;
    }

    char buffer[64];

    snprintf(buffer, sizeof(buffer), "%.1f", fLDMAXAOA);
    XPSetWidgetDescriptor(widgetLDMaxAOA, buffer);

    snprintf(buffer, sizeof(buffer), "%.1f", fONSPEEDFASTAOA);
    XPSetWidgetDescriptor(widgetOnSpeedFastAOA, buffer);

    snprintf(buffer, sizeof(buffer), "%.1f", fONSPEEDSLOWAOA);
    XPSetWidgetDescriptor(widgetOnSpeedSlowAOA, buffer);

    snprintf(buffer, sizeof(buffer), "%.1f", fSTALLWARNAOA);
    XPSetWidgetDescriptor(widgetStallWarnAOA, buffer);

    snprintf(buffer, sizeof(buffer), "%.1f", fALPHASTALL);
    XPSetWidgetDescriptor(widgetAlphaStall, buffer);

    snprintf(buffer, sizeof(buffer), "%d", iMuteAudioUnderIAS);
    XPSetWidgetDescriptor(widgetMuteAudioUnderIAS, buffer);

    snprintf(buffer, sizeof(buffer), "%d", iVs1G);
    XPSetWidgetDescriptor(widgetVs1G, buffer);

    snprintf(buffer, sizeof(buffer), "%d", iMasterVolumePct);
    XPSetWidgetDescriptor(widgetMasterVolumePct, buffer);

    snprintf(buffer, sizeof(buffer), "%d", iAoaMedianWindow);
    XPSetWidgetDescriptor(widgetAoaMedianWindow, buffer);

    snprintf(buffer, sizeof(buffer), "%d", iAoaMeanWindow);
    XPSetWidgetDescriptor(widgetAoaMeanWindow, buffer);

    if (audioToggleCheckbox) {
        XPSetWidgetDescriptor(audioToggleCheckbox,
                              audioEnabled ? "OnSpeed Tones: On" : "OnSpeed Tones: Off");
    }

    if (widgetButtonStallHorn) {
        XPSetWidgetDescriptor(widgetButtonStallHorn,
                              bMuteStallHorn ? "X-Plane Stall Horn: Off"
                                             : "X-Plane Stall Horn: On");
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#ifdef ENABLE_M5_INDEXER
// Storage that backs the serial-submenu refcons.  Menu items pass a
// raw pointer back to the handler; that pointer must outlive the
// menu, so we own the strings here.  Index 0 is always the literal
// "SerialOff" sentinel; subsequent entries hold detected port paths.
static std::vector<std::string> g_SerialMenuRefcons;
static XPLMMenuID g_SerialMenuId = nullptr;

// Rebuild the serial submenu from the OS's current port enumeration.
// Called from XPluginStart and from the "Refresh ports" menu item.
//
// Refcon ownership: each menu entry passes a void* refcon that's
// captured by X-Plane and handed back to the menu callback unchanged.
// We pack each entry's discriminator string into g_SerialMenuRefcons
// and pass `g_SerialMenuRefcons[i].data()` as the refcon.  CRITICAL:
// that pointer must outlive the menu, so the vector must NOT
// reallocate after the .data() pointer is captured.  Reserve up-front
// for the exact number of entries we'll push so no growth happens
// mid-build.
static void RebuildSerialMenu()
{
    if (!g_SerialMenuId) return;
    XPLMClearAllMenuItems(g_SerialMenuId);
    g_SerialMenuRefcons.clear();

    auto ports = onspeed_xplane::serial::ListPorts();
    // Reserve for: 1 "SerialOff" sentinel + N port paths.  Anything
    // less guarantees a reallocation if ports.size() >= 7, which
    // dangles the .data() pointer for "SerialOff".
    g_SerialMenuRefcons.reserve(ports.size() + 1);

    g_SerialMenuRefcons.emplace_back("SerialOff");
    XPLMAppendMenuItem(g_SerialMenuId, "Off (no serial output)",
        static_cast<void*>(g_SerialMenuRefcons.back().data()), 1);
    XPLMAppendMenuItem(g_SerialMenuId, "Refresh ports",
        static_cast<void*>(const_cast<char*>("SerialRefresh")), 1);
    XPLMAppendMenuSeparator(g_SerialMenuId);

    if (ports.empty()) {
        XPLMAppendMenuItem(g_SerialMenuId, "(no USB serial devices found)",
            static_cast<void*>(const_cast<char*>("SerialNone")), 1);
    } else {
        for (const auto& p : ports) {
            g_SerialMenuRefcons.push_back(p);
            XPLMAppendMenuItem(g_SerialMenuId, p.c_str(),
                static_cast<void*>(g_SerialMenuRefcons.back().data()), 1);
        }
    }
}

// Set the serial output to a specific port path (or close if empty),
// persisting the choice to the per-aircraft .prf.
static void SetSerialPort(const std::string& path)
{
    sSerialPortPath = path;
    if (path.empty()) {
        onspeed_xplane::indexer::CloseSerialOut();
    } else {
        onspeed_xplane::indexer::OpenSerialOut(path);
    }
    SaveSettings();
}
#endif

// Add menu handler
static void AudioMenuHandler([[maybe_unused]] void * mRef, void * iRef)
{
    const char* tag = static_cast<const char *>(iRef);
    if (!strcmp(tag, "Show")) {
        if (!audioControlWidget) {
            ClampAudioWindowOnScreen();
            CreateAudioControlWindow(s_audioWindow.left,  s_audioWindow.top,
                                     s_audioWindow.width, s_audioWindow.height);
        } else if (!XPIsWidgetVisible(audioControlWidget)) {
            XPShowWidget(audioControlWidget);
            UpdateAOATextFields(); // Update text fields when showing the window
        } else {
            XPHideWidget(audioControlWidget);
        }
        // Refresh after the toggle so s_audioWindow.visible reflects
        // the new open/closed state *and* any unflushed drag/resize
        // the periodic poll hadn't picked up yet.  Persist if
        // anything changed so a reboot reopens (or stays closed) at
        // the right place.
        RefreshAudioWindowState();
        if (AudioWindowChanged()) {
            SaveSettings();
        }
        return;
    }
#ifdef ENABLE_M5_INDEXER
    if (!strcmp(tag, "IndexerToggle")) {
        if (onspeed_xplane::indexer::IsVisible())
            onspeed_xplane::indexer::Hide();
        else
            onspeed_xplane::indexer::Show();
        SaveIndexerWindowState();   // immediate persist of toggle
        return;
    }
    if (!strcmp(tag, "SerialOff")) {
        SetSerialPort("");
        return;
    }
    if (!strcmp(tag, "SerialRefresh")) {
        RebuildSerialMenu();
        return;
    }
    if (!strcmp(tag, "SerialNone")) {
        return;   // placeholder, no-op
    }
    // Anything else passing through here on the indexer side is a
    // serial-port path stored as one of the entries in
    // g_SerialMenuRefcons.  Match by string-equality against any of
    // those — that proves the click came from a port-row we built.
    for (const auto& p : g_SerialMenuRefcons) {
        if (p == tag && p != "SerialOff") {
            SetSerialPort(p);
            return;
        }
    }
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static onspeed::ToneThresholds buildThresholds() {
    return { fLDMAXAOA, fONSPEEDFASTAOA, fONSPEEDSLOWAOA, fSTALLWARNAOA };
}

// Audio render thread: pumps OpenAL's queued-buffer pipeline.  Each
// iteration unqueues any consumed buffer, fills it with the next
// chunk of synth+envelope+mix output, and re-queues it.  The
// envelope owns all per-pulse timing; this thread just produces
// sample-accurate PCM at kAudioSampleRateHz and lets OpenAL/CoreAudio
// resample to the device rate.
void AudioRenderThread() {
    int16_t mono[kFramesPerChunk];
    int16_t stereo[kFramesPerChunk * 2];

    while (threadRunning) {
        ALint processed = 0;
        alGetSourcei(audioSource, AL_BUFFERS_PROCESSED, &processed);

        // No buffer consumed yet — sleep a fraction of one chunk so we
        // wake before the source starves but don't busy-loop.
        if (processed == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }

        while (processed-- > 0 && threadRunning) {
            ALuint buf = 0;
            alSourceUnqueueBuffers(audioSource, 1, &buf);

            // Synthesize the carrier (phase-continuous across chunks
            // so the cosine never has a discontinuity) and mix through
            // the envelope under the mutex.  Pan + master volume
            // compose into the per-channel scales: gain × pan, capped
            // at 1.0 by Apply3DPan's max-gain normalization so the
            // mixer never has to hard-clip.
            {
                std::lock_guard<std::mutex> lock(g_EngineMutex);

                // Promote a pending carrier change only when the
                // envelope is currently silent (level == 0).  That
                // covers every transition window the spec relies on:
                //
                //   Idle    — no tone playing
                //   Delay   — silent first half of every fresh pulse,
                //             AND the silent first delay after a
                //             solid->pulsed re-arm (the moment the
                //             prior Release tail finishes and the
                //             queued spec fires)
                //   Release — only the very last sample
                //
                // Switching carriers under any of these is inaudible —
                // the cosine times zero is zero either way.  Resetting
                // carrierPhase here means the new frequency starts from
                // a known sample, so the discontinuity (different freq
                // at the same time t) sits inside silence and never
                // leaks into the audible attack ramp.
                //
                // IsIdle() alone is too strict for the solid->pulsed
                // case: Envelope::Tick fires the queued NoteOn the
                // same sample it reaches Idle, so the render thread
                // never observes the Idle phase between Release and
                // the new spec's Delay.  Checking Level == 0 catches
                // both the Idle moment AND the Delay phase that
                // immediately follows.
                if (g_Engine.envelope.Level() == 0.0f &&
                    g_Engine.activeCarrier != g_Engine.pendingCarrier) {
                    g_Engine.activeCarrier = g_Engine.pendingCarrier;
                    g_Engine.carrierPhase  = 0.0f;
                }

                const float carrierHz =
                    (g_Engine.activeCarrier == onspeed::EnToneType::High)
                      ? static_cast<float>(onspeed::HIGH_TONE_HZ)
                      : static_cast<float>(onspeed::LOW_TONE_HZ);

                g_Engine.carrierPhase = onspeed::audio::Synthesize(
                    carrierHz,
                    onspeed::audio::kLegacyToneAmplitude,
                    kAudioSampleRateHz,
                    mono, kFramesPerChunk,
                    g_Engine.carrierPhase);

                const float gain = g_Engine.muted ? 0.0f : g_Engine.volumeMult;
                onspeed::audio::MixerInputs in;
                in.in         = mono;
                in.leftScale  = gain * g_Engine.leftPanGain;
                in.rightScale = gain * g_Engine.rightPanGain;
                in.envelope   = &g_Engine.envelope;
                onspeed::audio::Mix(in, stereo, kFramesPerChunk,
                                    g_Engine.mixerState);
            }

            alBufferData(buf, AL_FORMAT_STEREO16, stereo, sizeof(stereo),
                         kAudioSampleRateHz);
            alSourceQueueBuffers(audioSource, 1, &buf);
        }

        // If the source under-ran (processed == queued), kick it back
        // into AL_PLAYING — OpenAL stops a source that runs out of
        // queued data and won't auto-resume.
        ALint state = 0;
        alGetSourcei(audioSource, AL_SOURCE_STATE, &state);
        if (state != AL_PLAYING) {
            alSourcePlay(audioSource);
        }
    }
}

// Hysteretic IAS gate state.  Mirrors the firmware:
// unmute at iMuteAudioUnderIAS + kIasMuteHysteresisKt, re-mute
// back at iMuteAudioUnderIAS.  iMuteAudioUnderIAS == 0 disables
// the gate entirely.
static bool s_iasGateOpen = false;

// Per-flight-loop AOA decision: smooth the raw AOA, run it through
// onspeed_core's ToneCalc, and update the engine's envelope and
// volume to match.  All audio shaping (per-pulse DAHD, click-free
// transitions, solid->pulsed shortened-delay) lives in the envelope
// — this function just hands it the next spec.
void PlayAOATone(float fAoa, float /* fElapsedTime */) {
    // Plugin-only AOA smoothing; see iAoaMedianWindow / iAoaMeanWindow.
    aoaMedian->add(fAoa);
    aoaMean->addValue(aoaMedian->getMedian());
    const float fSmoothedAoa = aoaMean->getFastAverage();

    const float fIas = XPLMGetDataf(iasDataRef);

    char aoaText[64];
    snprintf(aoaText, sizeof(aoaText),
             "AOA: %.1f (smooth: %.1f) IAS: %.1f",
             fAoa, fSmoothedAoa, fIas);
    XPSetWidgetDescriptor(widgetAOAValue, aoaText);

    // Pause + crash gate: silence audio when X-Plane is paused or the
    // user aircraft has crashed.  Both states keep the flight loop
    // ticking but make AOA-based audio meaningless or actively
    // misleading.  Drains the envelope cleanly via NoteOff so a resume
    // (or aircraft reset) doesn't replay a stale tone tail.  Checked
    // first so they override the Sound:On toggle — a paused or
    // crashed sim should never bleed audio.
    const bool simPaused = pausedDataRef
                             && XPLMGetDatai(pausedDataRef) != 0;
    const bool simCrashed = crashedDataRef
                             && XPLMGetDatai(crashedDataRef) != 0;
    if (simPaused || simCrashed) {
        std::lock_guard<std::mutex> lock(g_EngineMutex);
        g_Engine.envelope.NoteOff();
        g_Engine.muted = true;
        s_iasGateOpen  = false;
        setStatusOrIfSticky(simCrashed ? "Audio: Crashed"
                                       : "Audio: Paused (sim)");
        return;
    }

    // Master toggle: drains the envelope's release ramp cleanly via
    // NoteOff (never a hard stop on a running waveform).
    if (!audioEnabled) {
        std::lock_guard<std::mutex> lock(g_EngineMutex);
        g_Engine.envelope.NoteOff();
        g_Engine.muted = true;
        s_iasGateOpen  = false;
        setStatusOrIfSticky("");
        return;
    }

    // IAS gate with hysteresis.  iMuteAudioUnderIAS == 0 means
    // "always on" (firmware sentinel preserved for parity).
    if (iMuteAudioUnderIAS == 0) {
        s_iasGateOpen = true;
    } else if (s_iasGateOpen) {
        if (fIas < iMuteAudioUnderIAS) s_iasGateOpen = false;
    } else {
        if (fIas >= iMuteAudioUnderIAS + kIasMuteHysteresisKt) {
            s_iasGateOpen = true;
        }
    }
    if (!s_iasGateOpen) {
        {
            std::lock_guard<std::mutex> lock(g_EngineMutex);
            g_Engine.envelope.NoteOff();
            g_Engine.muted = true;
        }
        char gateMsg[64];
        snprintf(gateMsg, sizeof(gateMsg),
                 "Audio: None - below %d kt", iMuteAudioUnderIAS);
        setStatusOrIfSticky(gateMsg);
        return;
    }

    // Region decision (single source of truth shared with firmware).
    const onspeed::ToneResult result =
        onspeed::calculateTone(fSmoothedAoa, buildThresholds());

    // Stereo pan from lateral G — same Apply3DPan pipeline the firmware
    // uses, so a coordinated turn sounds the same in sim as in the
    // airplane.  Headphone-stereo only; OpenAL spatialization is
    // bypassed (Mix() emits stereo and stereo sources are un-spatialized
    // by spec).  When the dataref isn't found, lateralG falls back to 0
    // and the pan stays centered.
    const float fLateralG = lateralGDataRef
                              ? XPLMGetDataf(lateralGDataRef) : 0.0f;
    const onspeed::audio::PanResult pan =
        onspeed::audio::Apply3DPan(fLateralG, g_PanState, g_PanCfg);

    // Master volume folds in here.  The per-PPS fVolumeMult ramp
    // (cruise = 0.25 → stall = 1.0) is the spec-defined audibility
    // curve; multiplying by the user's master percent scales the
    // whole curve uniformly, so cruise-vs-stall ratio is preserved.
    const float fMasterScale  = iMasterVolumePct / 100.0f;
    const float fScaledVolMult = result.fVolumeMult * fMasterScale;

    {
        std::lock_guard<std::mutex> lock(g_EngineMutex);
        g_Engine.muted        = false;
        g_Engine.volumeMult   = fScaledVolMult;
        g_Engine.leftPanGain  = pan.leftGain;
        g_Engine.rightPanGain = pan.rightGain;
        // Queue the new carrier without disturbing what's currently
        // synthesizing.  The render thread promotes pendingCarrier
        // → activeCarrier when the envelope reaches Idle, so a
        // Low → High switch can never bleed onto the prior tone's
        // release ramp.
        if (result.enTone != onspeed::EnToneType::None) {
            g_Engine.pendingCarrier = result.enTone;
        }
        // DecideAndArm dispatches NoteOn/NoteOff and applies the
        // shortened first-pulse delay on solid->pulsed transitions.
        onspeed::audio::DecideAndArm(result, g_Engine.envelope, g_OrchCfg);
    }

    char audioStatusText[80];
    if (result.enTone == onspeed::EnToneType::None) {
        snprintf(audioStatusText, sizeof(audioStatusText), "Audio: None");
    } else if (result.fPulseFreq == 0.0f) {
        snprintf(audioStatusText, sizeof(audioStatusText),
                 "Audio: Steady - %s",
                 result.enTone == onspeed::EnToneType::High ? "High" : "OnSpeed");
    } else {
        const float fCarrierHz = (result.enTone == onspeed::EnToneType::High)
                                   ? static_cast<float>(onspeed::HIGH_TONE_HZ)
                                   : static_cast<float>(onspeed::LOW_TONE_HZ);
        snprintf(audioStatusText, sizeof(audioStatusText),
                 "Audio Hz: %.0f pps: %.1f vol: %.2f (x %d%%)",
                 fCarrierHz, result.fPulseFreq,
                 result.fVolumeMult, iMasterVolumePct);
    }
    setStatusOrIfSticky(audioStatusText);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// X-Plane flight-loop entry: pulls AOA dataref, hands it to PlayAOATone.
float CheckAOAAndPlayTone(float inElapsedSinceLastCall,
                          [[maybe_unused]] float inElapsedTimeSinceLastFlightLoop,
                          [[maybe_unused]] int inCounter,
                          [[maybe_unused]] void *inRefcon) {

    // use XPLMGetDataf to get the AOA value.  https://developer.x-plane.com/sdk/XPLMDataAccess/#XPLMDataRef

    // Suppress the sim's built-in stall horn so it doesn't talk over
    // OnSpeed's audio.  Reapplied every tick because X-Plane resets
    // acf_has_stallwarn from the .acf file each time the aircraft loads.
    // Capture the .acf-defined value the first time we mute for this
    // aircraft, so toggling off restores it instead of writing a hard 1
    // (which would be wrong for an aircraft that originally had no horn).
    if (bMuteStallHorn && acfHasStallwarnDataRef) {
        if (g_iAcfHasStallwarnOriginal < 0)
            g_iAcfHasStallwarnOriginal = XPLMGetDatai(acfHasStallwarnDataRef);
        XPLMSetDatai(acfHasStallwarnDataRef, 0);
    }

    // On the ground above the IAS-mute floor with a known Vs, drive
    // the audio path off the V² formula (same source the indicator
    // uses) instead of X-Plane's raw alpha dataref.  Reason: weight
    // on the gear pins X-Plane's geometric alpha well below the AOA
    // setpoints, so comparing raw alpha against fLDMAXAOA / etc.
    // never fires a tone during the takeoff roll — even though the
    // wing is at max effort and the indicator is correctly pegged at
    // ~99%.  MaybeSynthesizeAoaFromVSquared returns NaN unless the
    // V² regime is active; otherwise we use the raw alpha dataref
    // exactly as in flight.  See PercentLiftFill.cpp's docstring for
    // the full design.
    //
    // Debounce against single-tick flicker (rough taxi, gear bounce on
    // landing).  The debounced result is cached for the indexer to
    // read via GetDebouncedOnGround so audio and indicator agree on
    // the regime — without that, a one-frame raw-flicker would put
    // the audio in V² mode while the indexer's debouncer still saw
    // alpha mode (or vice versa).  Static state is safe because
    // CheckAOAAndPlayTone runs on the X-Plane flight-loop thread.
    static onspeed_xplane::indexer::OnGroundDebounceState
        s_onGroundDebounce{};
    const float fIas      = XPLMGetDataf(iasDataRef);
    const bool  iasValid  = (iMuteAudioUnderIAS == 0)
                            || (fIas >= iMuteAudioUnderIAS);
    const bool  rawOnGround = onGroundAnyDataRef
                              && XPLMGetDatai(onGroundAnyDataRef) != 0;
    const bool  onGround = onspeed_xplane::indexer::DebounceOnGround(
                              rawOnGround, s_onGroundDebounce);
    g_DebouncedOnGround = onGround;   // shared with the indexer's Tick
    const float synthAoa  = onspeed_xplane::indexer::
                            MaybeSynthesizeAoaFromVSquared(
                                fIas, iasValid, onGround);
    const float aoa = std::isfinite(synthAoa)
                        ? synthAoa
                        : XPLMGetDataf(aoaDataRef);
    PlayAOATone(aoa, inElapsedSinceLastCall);

#ifdef ENABLE_M5_INDEXER
    // Run the M5 renderer one tick.  Cheap when the indexer window is
    // hidden — the M5 still draws into our offscreen panel, but the
    // X-Plane window draw callback is never invoked so the GL upload
    // path is skipped.  Could short-circuit if hidden, but the M5
    // renderer is microseconds per frame on a desktop CPU.
    onspeed_xplane::indexer::Tick();
#endif

    return -1.0f;  // Negative value means "call me next frame"
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Periodic flight-loop callback for persisted UI state — handles
// the post-AIRPORT_LOADED indexer restore (M5 build only), the
// indexer dirty-poll save (M5 build only), and the audio control
// window geometry dirty-poll save (always).  All three live on the
// same 1 Hz tick so the .prf flush cadence stays predictable.
//
// On M5 builds the indexer needs the flight-loop context because
// Show() lazy-inits SDL/M5GFX/M5Unified-singleton/panel-framebuffer
// (the message-handler-based restore in v1, PR #406, crashed
// X-Plane).  The audio control window is just a widget — no lazy
// init — but folding its dirty-poll into the same tick keeps
// behavior consistent across both build flavors.
//
// Cadence: 1 Hz.  Mid-drag this means up to 1 s lag between user
// release and .prf flush — fine, matches the SDK's debounce
// recommendation.
constexpr float kPersistFlushInterval = 1.0f;

// Snapshot the audio control window's current geometry + visibility
// into s_audioWindow.  No-op when the widget hasn't been created.
// Called from the periodic callback to detect user drags / resizes.
static void RefreshAudioWindowState()
{
    if (!audioControlWidget) return;
    int left = 0, top = 0, right = 0, bottom = 0;
    XPGetWidgetGeometry(audioControlWidget, &left, &top, &right, &bottom);

    // X-Plane occasionally returns transient nonsense during drags
    // / monitor changes.  Reject pathological reads so the .prf
    // doesn't get poisoned.  Matches kSaneAbs in IndexerWindow.cpp.
    constexpr int kSaneAbs = 50000;
    const int width  = right - left;
    const int height = top   - bottom;
    if (std::abs(left) >= kSaneAbs || std::abs(top) >= kSaneAbs ||
        width <= 0 || height <= 0 ||
        width >= kSaneAbs || height >= kSaneAbs) {
        return;
    }
    s_audioWindow.left    = left;
    s_audioWindow.top     = top;
    s_audioWindow.width   = width;
    s_audioWindow.height  = height;
    s_audioWindow.visible = (XPIsWidgetVisible(audioControlWidget) != 0);
}

static bool AudioWindowChanged()
{
    return s_audioWindow.left    != s_audioWindowLastSaved.left    ||
           s_audioWindow.top     != s_audioWindowLastSaved.top     ||
           s_audioWindow.width   != s_audioWindowLastSaved.width   ||
           s_audioWindow.height  != s_audioWindowLastSaved.height  ||
           s_audioWindow.visible != s_audioWindowLastSaved.visible;
}

static float SavePersistedStatePeriodic(
    [[maybe_unused]] float elapsed,
    [[maybe_unused]] float elapsed_sim,
    [[maybe_unused]] int counter,
    [[maybe_unused]] void* ref)
{
#ifdef ENABLE_M5_INDEXER
    // 1) One-shot: apply persisted indexer state if AIRPORT_LOADED
    //    queued one.  Must run before MarkDirtyIfChanged so the
    //    snap-to-saved-spot write doesn't immediately get re-saved
    //    as "user changed geometry."
    if (s_indexerRestorePending && s_settingsLoaded) {
        s_indexerRestorePending = false;
        XPLMDebugString("FlyOnSpeed: applying persisted indexer state\n");
        onspeed_xplane::indexer::ApplyPersistedState(indexerSettings);
    }

    // 2) Poll for user-driven indexer change (drag / resize / pop-
    //    out / mode cycle), flush if dirty.
    onspeed_xplane::indexer::MarkDirtyIfChanged();
    if (onspeed_xplane::indexer::IsDirty() && s_settingsLoaded) {
        SaveIndexerWindowState();
    }
#endif

    // 3) Audio control window dirty-poll.  SaveSettings handles its
    //    own load-gate and writes the whole .prf in one fwrite, so
    //    we don't need to coordinate with the indexer save above.
    if (s_settingsLoaded) {
        RefreshAudioWindowState();
        if (AudioWindowChanged()) {
            SaveSettings();
        }
    }

    return kPersistFlushInterval;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// XPluginStart: validate sizes, find datarefs, register flight loops, build menu.
PLUGIN_API int XPluginStart(char *outName, char *outSig, char *outDesc) {
    strcpy(outName, "AOA-Tone-FlyOnSpeed");
    strcpy(outSig, "xplane.plugin.aoa-tone-flyon-speed");
    strcpy(outDesc, "A plugin that plays audio tones based on AOA");
    
	if( sizeof(unsigned int) != 4 ||
		sizeof(unsigned short) != 2)
	{
		XPLMDebugString("FlyOnSpeed: This example plugin was compiled with a compiler with weird type sizes.\n");
		return 0;
	}

    // Opt into POSIX-style paths everywhere.  Without this,
    // XPLMGetSystemPath returns legacy HFS-style paths on macOS
    // ("Macintosh HD:Users:..." with colon separators) that the
    // C file API can't open.  Linux/Windows are unaffected by the
    // setting but enabling it unconditionally keeps the same fopen
    // path working on every platform.  Must be called before any
    // path-reading SDK call.
    XPLMEnableFeature("XPLM_USE_NATIVE_PATHS", 1);

    // OpenAL init runs in init_sound on the next flight-loop tick
    // (the audio render thread is also spawned there, after the
    // OpenAL source exists).  See https://developer.x-plane.com/sdk/.
    XPLMRegisterFlightLoopCallback(init_sound, -1.0, nullptr);

    // sim/flightmodel/position/alpha is the airframe's true AOA in
    // degrees, derived from X-Plane's flight model.  Other candidates
    // (sim/cockpit2/gauges/indicators/AoA_pilot,
    // sim/cockpit2/gauges/indicators/aoa_angle_degrees) read from
    // panel instruments which lag and round.  We want the model-truth
    // value.  DataRef catalog: https://siminnovations.com/xplane/dataref/
    aoaDataRef = XPLMFindDataRef("sim/flightmodel/position/alpha");
    if (aoaDataRef == nullptr) {
        XPLMDebugString("FlyOnSpeed: Failed to find AOA DataRef");
        return 0;
    }

    // ias in knots
    iasDataRef = XPLMFindDataRef("sim/flightmodel/position/indicated_airspeed");
    if (iasDataRef == nullptr) {
        XPLMDebugString("FlyOnSpeed: Failed to find IAS DataRef");
        return 0;
    }

    // Lateral G in body frame.  Drives the slip-skid centripetal pan
    // (right channel emphasized when slipping right, etc.).  Optional —
    // if the dataref isn't available the pan stays centered.
    lateralGDataRef = XPLMFindDataRef("sim/flightmodel/forces/g_side");

    // Pause state.  When the user pauses X-Plane (Esc menu, "p" key)
    // the flight loop continues firing but we want audio silent —
    // a stuck stall warning during a paused sim is jarring and
    // disconnected from any actual flight state.  Optional: if the
    // dataref is missing the pause gate is skipped.
    pausedDataRef = XPLMFindDataRef("sim/time/paused");

    // Crash state.  After a crash X-Plane keeps the flight loop
    // ticking (so wreckage and post-crash physics still update), but
    // any AOA-based audio cue is meaningless and stale at that point.
    // We mute on crash for the same reason we mute on pause.  Optional:
    // if the dataref is missing the crash gate is skipped.
    crashedDataRef = XPLMFindDataRef("sim/flightmodel2/misc/has_crashed");

    // Aircraft-level "has audio stall warning" flag.  Writable int.
    // We toggle it to 0 each flight loop when bMuteStallHorn is set, so
    // X-Plane's own stall warning audio doesn't talk over OnSpeed's
    // tones.  X-Plane restores the .acf default on aircraft load, so the
    // override has to be reapplied — see CheckAOAAndPlayTone.  Optional:
    // if the dataref is missing the toggle is a no-op.
    acfHasStallwarnDataRef = XPLMFindDataRef("sim/aircraft/view/acf_has_stallwarn");

    // acf_Vs is a per-aircraft Plane Maker constant (1G clean stall
    // speed in KIAS — see OnAircraftLoaded for the unit citation).
    // Used to seed iVs1G on first aircraft load.  Optional: third-
    // party authors sometimes leave it 0; in that case the seed-
    // from-acf path stays a no-op and the pilot sets it via the
    // audio control window.
    acfVsDataRef = XPLMFindDataRef("sim/aircraft/view/acf_Vs");

    // sim/flightmodel/failures/onground_any — int, true while any
    // landing gear is touching.  Used to gate the V² audio synthesis
    // (audio path drives tones from V²-synthesized AOA on the ground
    // so cues match the indicator instead of comparing raw alpha
    // against thresholds).  See MaybeSynthesizeAoaFromVSquared.
    onGroundAnyDataRef = XPLMFindDataRef("sim/flightmodel/failures/onground_any");

    // Seed-on-first-load datarefs (issue #392).  Read once per
    // aircraft from SeedSetpointsFromDatarefs when no .prf is
    // present, then never again for that aircraft.  Optional: a
    // missing dataref falls back through the helper's safer path
    // (compiled-in defaults stand when acf_stall_warn_alpha is
    // missing entirely; flat alpha_stall when only Vs/Vso are
    // missing).
    acfStallWarnAlphaRef = XPLMFindDataRef("sim/aircraft/overflow/acf_stall_warn_alpha");
    acfVsoDataRef        = XPLMFindDataRef("sim/aircraft/view/acf_Vso");

    XPLMRegisterFlightLoopCallback(CheckAOAAndPlayTone, 1.0, nullptr);

    // 1 Hz callback that polls user-driven UI state changes (audio
    // control window always; indexer when ENABLE_M5_INDEXER) and
    // flushes them to the per-aircraft .prf.  On M5 builds it also
    // applies the AIRPORT_LOADED-deferred indexer restore.
    XPLMRegisterFlightLoopCallback(SavePersistedStatePeriodic,
                                   kPersistFlushInterval, nullptr);

    // Plugins menu entry that opens the control window.
    int item = XPLMAppendMenuItem(XPLMFindPluginsMenu(), "Fly On Speed", nullptr, 1);
    menuId = XPLMCreateMenu("Fly On Speed", XPLMFindPluginsMenu(), item, AudioMenuHandler, nullptr);
    // The SDK takes a non-const void* for menu item refcons; the matching
    // handler casts it back to const char* before strcmp. Discriminator
    // string is a literal — never written through the void*.
    XPLMAppendMenuItem(menuId, "Settings: Show/Hide",
                       static_cast<void*>(const_cast<char*>("Show")), 1);

#ifdef ENABLE_M5_INDEXER
    XPLMAppendMenuItem(menuId, "Indexer: Show/Hide",
                       static_cast<void*>(const_cast<char*>("IndexerToggle")), 1);

    // USB-serial submenu — routes the same wire frames to a physical
    // M5Stack so a Core2 plugged into a USB-C port behaves like a
    // real OnSpeed display.
    XPLMAppendMenuSeparator(menuId);
    int serialItem = XPLMAppendMenuItem(menuId, "Serial output", nullptr, 1);
    g_SerialMenuId = XPLMCreateMenu("Serial output", menuId, serialItem,
                                    AudioMenuHandler, nullptr);
    RebuildSerialMenu();
#endif

    // The audio render thread is started by init_sound, after the
    // OpenAL source and streaming buffers exist.  init_sound also
    // calls OnAircraftLoaded(), since XPLMGetNthAircraftModel returns
    // an empty filename if called from XPluginStart before X-Plane has
    // finished bringing the user aircraft up — deferring the call to
    // the first flight-loop tick guarantees the aircraft is loaded.

    return 1;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// XPluginStop: stop the audio thread (must precede cleanupAudio), tear down widgets/menu, release OpenAL.
PLUGIN_API void XPluginStop(void) {
    // Persist any unsaved tuning so an X-Plane shutdown still preserves
    // in-flight tweaks the pilot didn't explicitly Update.
    SaveSettings();

    // Stop the audio render thread before tearing down OpenAL.
    // Guarded by has_value() because init_sound may not have fired
    // (XPluginStart succeeded but X-Plane unloaded before the first
    // flight-loop tick).
    threadRunning = false;
    if (audioThread.has_value()) {
        audioThread->join();
        audioThread.reset();
    }

    // Capture the audio control window's final geometry + visibility
    // and flush to .prf before destroying the widget.  Mirrors the
    // WILL_WRITE_PREFS hook for paths where X-Plane skips that message
    // (e.g., crash-shutdown).  RefreshAudioWindowState early-returns
    // once the widget is gone, so this must run first.
    if (s_settingsLoaded) {
        RefreshAudioWindowState();
        if (AudioWindowChanged()) {
            SaveSettings();
        }
    }
    if (audioControlWidget) {
        XPDestroyWidget(audioControlWidget, 1);
        audioControlWidget = nullptr;
    }

    XPLMDestroyMenu(menuId);
    XPLMUnregisterFlightLoopCallback(CheckAOAAndPlayTone, nullptr);
    XPLMUnregisterFlightLoopCallback(SavePersistedStatePeriodic, nullptr);

#ifdef ENABLE_M5_INDEXER
    // Final flush of any pending dirty state — WILL_WRITE_PREFS
    // usually catches this earlier, but a "Reload all plugins" path
    // bypasses prefs writing and lands here directly.
    if (onspeed_xplane::indexer::IsDirty()) {
        SaveIndexerWindowState();
    }
    onspeed_xplane::indexer::Shutdown();
#endif

    cleanupAudio();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// X-Plane SDK lifecycle: enable.  Currently a no-op.
PLUGIN_API int XPluginEnable(void) {
    return 1;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// X-Plane SDK lifecycle: disable.  Currently a no-op.
PLUGIN_API void XPluginDisable(void) {
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// X-Plane SDK message dispatch.  We listen for plane-load events so
// each aircraft picks up its own saved settings (calibration is
// per-airframe).  inParam carries the aircraft index; we only care
// about index 0 (the user aircraft).
PLUGIN_API void XPluginReceiveMessage([[maybe_unused]] XPLMPluginID inFromWho,
                                      int inMessage,
                                      void* inParam) {
    if (inMessage == XPLM_MSG_PLANE_LOADED &&
        reinterpret_cast<intptr_t>(inParam) == 0)
    {
        OnAircraftLoaded();
    }

    // Final-flush hook for the audio control window's geometry
    // before X-Plane writes its own prefs and exits.  The periodic
    // tick may not have run since the user's last drag.
    if (inMessage == XPLM_MSG_WILL_WRITE_PREFS && s_settingsLoaded) {
        RefreshAudioWindowState();
        if (AudioWindowChanged()) {
            SaveSettings();
        }
    }
#ifdef ENABLE_M5_INDEXER
    // XPLM_MSG_AIRPORT_LOADED fires after scenery has finished loading
    // and the aircraft is positioned at its starting airport.  This is
    // the canonical X-Plane plugin signal for "screen bounds and OS
    // monitor mappings are now stable" (matches the timing of stock
    // X-Plane G1000 / X1000 windows snapping into their saved spots
    // during the "Preparing world" phase).  Set a one-shot flag here;
    // the periodic flight-loop callback observes it next tick and
    // calls ApplyPersistedState from a safe context.
    if (inMessage == XPLM_MSG_AIRPORT_LOADED) {
        XPLMDebugString("FlyOnSpeed: AIRPORT_LOADED — queuing indexer "
                        "restore for next flight-loop tick\n");
        s_indexerRestorePending = true;
    }

    // XPLM_MSG_WILL_WRITE_PREFS fires once during the X-Plane shutdown
    // sequence as a final-flush opportunity.  XPluginStop runs later
    // but doesn't always execute cleanly under crash conditions.
    if (inMessage == XPLM_MSG_WILL_WRITE_PREFS &&
        onspeed_xplane::indexer::IsDirty())
    {
        SaveIndexerWindowState();
    }
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Tear down OpenAL.  Must run AFTER the audio render thread has
// joined; otherwise the thread could touch a destroyed source.
void cleanupAudio() {
    if (context) {
        alSourceStop(audioSource);
        alDeleteSources(1, &audioSource);
        alDeleteBuffers(kStreamBufferCount, streamBuffers);

        alcMakeContextCurrent(nullptr);
        alcDestroyContext(context);
        context = nullptr;
    }

    if (device) {
        alcCloseDevice(device);
        device = nullptr;
    }
}
