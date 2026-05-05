// /aoaconfig page client logic — legacy form-driven UI.
//
// This file is bundled into legacy_pages.h by build_web_bundle.py and
// substituted into the page template by HandleConfig in
// software/sketch_common/src/web_server/ConfigWebServer.cpp.
//
// The form's submit path (POST /aoaconfigsave -> HandleConfigSave) is
// untouched.  The button-driven sample reads route through the
// /api/sample/* JSON endpoints added in PR 2 of the web rewrite.
// /audiotest start/stop and /vnochime/test go through their /api/*
// counterparts.

// FillInValue queries for a single sample value and writes it into the
// targetID field.  Senders:
//   - "FLAPS"          -> /api/sample/flaps-raw   {adcCounts, position}
//   - "VOLUME"         -> /api/sample/volume      {adcCounts}
//   - "AOA"            -> /api/sample/aoa         {aoa}
//   - "VNOCHIMETEST"   -> POST /api/vnochime/test  (no value to set)
function FillInValue(SenderID, ValueName, TargetID) {
    document.getElementById(SenderID).disabled = true;

    if (ValueName == "VNOCHIMETEST") {
        let ButtonLabel = document.getElementById(SenderID).value;
        document.getElementById(SenderID).value = "Testing...";
        TriggerVnoChimeTest();
        document.getElementById(SenderID).value = ButtonLabel;
        document.getElementById(SenderID).disabled = false;
        return;
    }

    document.getElementById(SenderID).value = "Reading...";

    var ReturnValue = SampleValue(ValueName);

    if (ReturnValue !== null) {
        document.getElementById(TargetID).value = ReturnValue;
        document.getElementById(SenderID).value = "Updated!";
        // Refresh setpoint display if this was an AOA field
        var m = TargetID.match(/^id_flap(LDMAXAOA|ONSPEEDFASTAOA|ONSPEEDSLOWAOA|STALLWARNAOA)(\d+)$/);
        if (m && typeof updateSetpointDisplay === 'function') {
            updateSetpointDisplay(parseInt(m[2]), m[1]);
        }
    } else {
        document.getElementById(SenderID).value = "Error!";
    }

    document.getElementById(SenderID).disabled = false;
}

// SampleValue performs a synchronous GET to /api/sample/<name> and
// returns the human-readable string the caller should write into the
// target field.  Returns null on any error.
function SampleValue(ValueName) {
    var url = null;
    var pick = null;
    if (ValueName == "FLAPS") {
        url = "/api/sample/flaps-raw";
        pick = function (j) { return j.adcCounts; };
    } else if (ValueName == "VOLUME") {
        url = "/api/sample/volume";
        pick = function (j) { return j.adcCounts; };
    } else if (ValueName == "AOA") {
        url = "/api/sample/aoa";
        pick = function (j) { return j.aoa; };
    } else {
        console.error("SampleValue: unknown ValueName", ValueName);
        return null;
    }

    var xhr = new XMLHttpRequest();
    xhr.open("GET", url, false);
    xhr.send(null);

    if (xhr.status !== 200) {
        console.error("SampleValue: " + url + " returned " + xhr.status);
        return null;
    }
    try {
        var j = JSON.parse(xhr.responseText);
        var v = pick(j);
        if (v === undefined || v === null) return null;
        return String(v);
    } catch (e) {
        console.error("SampleValue: bad JSON from " + url + ": " + e);
        return null;
    }
}

// Async POST to /api/vnochime/test.  Fire-and-forget; the server
// schedules the chime and returns immediately.
function TriggerVnoChimeTest() {
    var xhr = new XMLHttpRequest();
    xhr.open("POST", "/api/vnochime/test", false);
    xhr.send(null);
    if (xhr.status !== 200) {
        console.error("TriggerVnoChimeTest: " + xhr.status);
    }
}

// Audio test start/stop.  The button toggles between "Test Audio" and
// "Stop"; while running we poll /api/audiotest/status every 400 ms so
// the button reverts when the test playback finishes on its own.
let gAudioTestPollTimer = null;

function AudioTestSetButton(SenderID, state) {
    const btn = document.getElementById(SenderID);
    if (!btn) return;

    btn.dataset.audiotestState = state;

    if (state === "running") {
        btn.value = "Stop";
        btn.disabled = false;
    } else if (state === "stopping") {
        btn.value = "Stopping...";
        btn.disabled = true;
    } else {
        btn.value = "Test Audio";
        btn.disabled = false;
    }
}

function AudioTestStopPolling() {
    if (gAudioTestPollTimer !== null) {
        clearInterval(gAudioTestPollTimer);
        gAudioTestPollTimer = null;
    }
}

function AudioTestStartPolling(SenderID) {
    AudioTestStopPolling();
    gAudioTestPollTimer = setInterval(function () {
        var xhr = new XMLHttpRequest();
        xhr.open("GET", "/api/audiotest/status", true);
        xhr.onreadystatechange = function () {
            if (xhr.readyState !== 4) return;
            if (xhr.status !== 200) return;
            try {
                var j = JSON.parse(xhr.responseText);
                if (j && j.state === "running") {
                    AudioTestSetButton(SenderID, "running");
                } else {
                    AudioTestSetButton(SenderID, "idle");
                    AudioTestStopPolling();
                }
            } catch (e) {
                // Treat parse failure as idle so the button doesn't
                // freeze in "Stop".
                AudioTestSetButton(SenderID, "idle");
                AudioTestStopPolling();
            }
        };
        xhr.send(null);
    }, 400);
}

function ToggleAudioTest(SenderID) {
    const btn = document.getElementById(SenderID);
    if (!btn) return;

    const state = btn.dataset.audiotestState || "idle";

    if (state === "running" || state === "stopping") {
        AudioTestSetButton(SenderID, "stopping");
        var stopXhr = new XMLHttpRequest();
        stopXhr.open("POST", "/api/audiotest/stop", true);
        stopXhr.onreadystatechange = function () {
            if (stopXhr.readyState !== 4) return;
            AudioTestStartPolling(SenderID);
        };
        stopXhr.send(null);
    } else {
        btn.disabled = true;
        btn.value = "Starting...";

        var startXhr = new XMLHttpRequest();
        startXhr.open("POST", "/api/audiotest", true);
        startXhr.onreadystatechange = function () {
            if (startXhr.readyState !== 4) return;
            // 200 -> started; 409 -> already running.  Both mean the
            // poll loop should run.
            if (startXhr.status === 200 || startXhr.status === 409) {
                AudioTestSetButton(SenderID, "running");
                AudioTestStartPolling(SenderID);
            } else {
                AudioTestSetButton(SenderID, "idle");
            }
        };
        startXhr.send(null);
    }
}

window.addEventListener('load', function () {
    const btn = document.getElementById('id_volumeTestButton');
    if (!btn) return;

    btn.dataset.audiotestState = "idle";

    // Sync initial state if a test was started elsewhere (console).
    var xhr = new XMLHttpRequest();
    xhr.open("GET", "/api/audiotest/status", true);
    xhr.onreadystatechange = function () {
        if (xhr.readyState !== 4) return;
        if (xhr.status !== 200) {
            AudioTestSetButton(btn.id, "idle");
            return;
        }
        try {
            var j = JSON.parse(xhr.responseText);
            if (j && j.state === "running") {
                AudioTestSetButton(btn.id, "running");
                AudioTestStartPolling(btn.id);
            } else {
                AudioTestSetButton(btn.id, "idle");
            }
        } catch (e) {
            AudioTestSetButton(btn.id, "idle");
        }
    };
    xhr.send(null);
});
