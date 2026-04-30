// ApiHandlers.h — JSON /api/* endpoint surface added in PR 2.
//
// One declaration per handler, registered in ConfigWebServer.cpp's
// setup() block alongside the legacy form-encoded routes.  These are
// purely additive: every legacy endpoint (/aoaconfigsave, /calwiz POST,
// /getvalue, /format, /reboot, /delete-bulk, ...) keeps working
// unchanged through PR 2 and is replaced in later PRs.
//
// PLAN_WEB_PREACT_REWRITE §3 PR 2 + §4f.

#ifndef ONSPEED_API_HANDLERS_H
#define ONSPEED_API_HANDLERS_H

namespace onspeed::api {

// One-shot sample endpoints (replace /getvalue?name=...).
void HandleApiSampleAoa();
void HandleApiSampleFlapsRaw();
void HandleApiSampleVolume();
void HandleApiSamplePfwd();
void HandleApiSampleP45();

// Audio test triggers + status poll.  Wrap g_AudioPlay's existing
// API surface (StartAudioTest / StopAudioTest / IsAudioTestRunning /
// SetVoice(enVoiceVnoChime)) — same code path the legacy
// /getvalue?name=AUDIOTEST handler uses.
void HandleApiAudioTestStart();
void HandleApiAudioTestStop();
void HandleApiAudioTestStatus();
void HandleApiVnoChimeTest();

// Logs API.  HandleApiLogs lifts the same enumeration HandleLogs uses
// internally; HandleApiLogsDeleteBulk reuses the active-file guard
// and per-file delete-with-yield pattern.
void HandleApiLogs();
void HandleApiLogsDeleteBulk();

// Trigger endpoints.  HandleApiFormat returns immediately with a
// taskId; the format itself runs on the same SD-write path the
// legacy /format handler uses.  HandleApiFormatStatus returns the
// state of the most-recent format job.  HandleApiReboot ACKs and
// schedules a soft restart.
void HandleApiFormat();
void HandleApiFormatStatus();
void HandleApiReboot();

// Cal wizard state.  Read side: the GET endpoint snapshots the
// per-flap setpoints + aircraft scalars that seed the wizard.  Write
// side: POST accepts the wizard's computed setpoints + curve fit and
// mutates the matching SuFlaps via the pure helper
// `onspeed::api::ApplyCalwizSave`.  The pure helper lets the
// differential test (test_calwiz_save_diff) prove byte-identity vs the
// legacy ConfigWebServer.cpp HandleCalWizard step=save sequence.
void HandleApiCalwizState();
void HandleApiCalwizSave();

// Build / version metadata.  Stable JSON shape so external tools
// can detect the firmware version they're talking to without
// scraping HTML.
void HandleApiVersion();

}  // namespace onspeed::api

#endif  // ONSPEED_API_HANDLERS_H
