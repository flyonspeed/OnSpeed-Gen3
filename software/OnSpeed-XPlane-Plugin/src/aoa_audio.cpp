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
#include "XPLMPlugin.h"
#include "XPLMUtilities.h"
#include "XPWidgets.h"
#include "XPStandardWidgets.h"
#include "XPWidgetUtils.h"
#include "XPLMMenus.h"

#include <iostream>
#include <sstream>
#include <cmath>
#include <vector>
#include <cstring>
#include <thread>
#include <mutex>
#include <atomic>

// Plugin shares its tone semantics, smoothing pipeline, and PCM
// generation with the firmware via onspeed_core — single source of
// truth across panel-mounted Gen3, M5 display, and this sim plugin.
#include <audio/ToneCalc.h>
#include <audio/ToneSynth.h>
#include <filters/RunningMean.h>
#include <filters/RunningMedian.h>

// Function declarations
void cleanupAudio();
static void UpdateAOATextFields();

// AOA ranges for different states (default values)
float AOA_BELOW_LDMAX           = 6.0f;     // Below this is "Below LDMax" - no tone
float AOA_BELOW_ONSPEED         = 7.3f;     // Between LDMax and this is "Below OnSpeed"
float AOA_ONSPEED_MAX           = 9.6f;     // Between Below OnSpeed and this is "OnSpeed"
float AOA_ABOVE_ONSPEED_MAX     = 12.5f;    // Above this is "Above OnSpeed"

float AOA_IAS_TONE_ENABLE       = 25.0f;    // IAS (knots) above this value will enable the tone

// Tone configuration. The two carrier frequencies stay here because
// they're plugin-side OpenAL buffer identities; the firmware uses the
// same values via Audio.cpp's aTone_400Hz / aTone_1600Hz.
//
// Pulse rates and stall PPS live in onspeed_core/audio/ToneCalc.h and
// reach the audio path via calculateTone() — no per-region constants
// duplicated in this file.
#define TONE_NORMAL_FREQ       400.0f   // Low tone in Hz (LDmax band, OnSpeed band)
#define TONE_HIGH_FREQ         1600.0f  // High tone in Hz (above OnSpeed, stall warn)
#define DEFAULT_VOLUME          1.0f    // OpenAL gain (0..1)

// OpenAL device and context
ALCdevice* device = nullptr;
ALCcontext* context = nullptr;
ALuint audioSource;
ALuint audioBufferNormal;  // Rename existing audioBuffer
ALuint audioBufferHigh;    // New buffer for high frequency

// DataRef for AOA and IAS (indicated airspeed)
XPLMDataRef aoaDataRef = nullptr;
XPLMDataRef iasDataRef = nullptr;
XPLMDataRef aircraftNameDataRef = nullptr;

// Add these globals for the UI
static XPWidgetID audioControlWidget = nullptr;
static XPWidgetID audioToggleCheckbox = nullptr;
static XPWidgetID widgetAOAValue = nullptr;
static XPWidgetID widgetButtonReload = nullptr;
static XPWidgetID widgetAudioStatus = nullptr;
static bool audioEnabled = false;
static XPLMMenuID menuId;

// AOA smoothing pipeline matches the firmware's SensorIO pressure path:
//   raw → RunningMedian (kill spikes) → RunningMean (smooth)
// Window sizes are picked to match the firmware spirit: median=5 (enough
// to reject 1-2 sample spikes without noticeable lag), mean=10 (firmware's
// PfwdAvg fixed size). At X-Plane's typical ~30-60 Hz flight loop rate
// this is ~80-160 ms of effective smoothing.
constexpr int kMedianWindow = 5;
constexpr int kMeanWindow   = 10;
onspeed::RunningMedian aoaMedian(kMedianWindow);
onspeed::RunningMean   aoaMean(kMeanWindow);

// Pulse-thread plumbing. The flight-loop callback fills these from the
// onspeed_core decision; the pulse thread reads them and plays one
// pulse cycle at the requested rate. atomic<float> is fine here — the
// reads happen at pulse cadence (a few times per second), the writes
// at flight-loop cadence (~30-60 Hz), and inconsistency for one frame
// is inaudible.
std::thread* pulseThread = nullptr;
std::atomic<bool>  threadRunning{false};
std::atomic<bool>  shouldPlay{false};
std::atomic<float> currentToneFreqHz{0.0f};   // 0 = no tone
std::atomic<float> currentPulseFreqHz{0.0f};  // 0 = solid (not pulsed)

// Add these globals with other globals
static int lastWidgetBottom = 0;
static const int WIDGET_HEIGHT = 20;
static const int WIDGET_MARGIN = 5;

// Add these globals with other globals
static XPWidgetID widgetAOABelowLDMax = nullptr;
static XPWidgetID widgetAOABelowOnSpeed = nullptr;
static XPWidgetID widgetAOAOnSpeedMax = nullptr;
static XPWidgetID widgetAOAAboveOnSpeedMax = nullptr;
static XPWidgetID widgetAOAIASToneEnable = nullptr;
static XPWidgetID widgetButtonUpdateValues = nullptr;

// Temporary variables to store text field values
static float temp_AOA_BELOW_LDMAX = 0.0f;
static float temp_AOA_BELOW_ONSPEED = 0.0f;
static float temp_AOA_ONSPEED_MAX = 0.0f;
static float temp_AOA_ABOVE_ONSPEED_MAX = 0.0f;
static float temp_AOA_IAS_TONE_ENABLE = 0.0f;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Sample rate for the precomputed OpenAL tone buffers. 44.1 kHz matches
// what the plugin used pre-onspeed_core; the firmware nominally runs
// at 16 kHz but ToneSynth::Synthesize accepts any rate.
constexpr int kBufferSampleRateHz = 44100;
constexpr int kBufferSamples      = kBufferSampleRateHz / 10;   // 0.1s
constexpr int16_t kBufferAmp      = 32767;                      // peak

// Fill a vector with one period-length cosine buffer at the requested
// frequency, ready to be uploaded to an OpenAL buffer and looped. Phase
// is reset each call (these are one-shot precomputes).
static std::vector<int16_t> precomputeTone(float frequencyHz) {
    std::vector<int16_t> buffer(kBufferSamples);
    onspeed::audio::Synthesize(frequencyHz, kBufferAmp, kBufferSampleRateHz,
                               buffer.data(), buffer.size(), 0.0f);
    return buffer;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Function to initialize OpenAL and create tone
static float init_sound([[maybe_unused]] float elapsed,
                        [[maybe_unused]] float elapsed_sim,
                        [[maybe_unused]] int counter,
                        [[maybe_unused]] void * ref)
{
    device = alcOpenDevice(nullptr);
    XPLMDebugString("FlyOnSpeed: Initializing audio device\n");
    if (!device) {
        XPLMDebugString("FlyOnSpeed: Failed to open device\n");
        return false;
    }
    
    context = alcCreateContext(device, nullptr);
    XPLMDebugString("FlyOnSpeed: Creating audio context\n");
    if (!context) {
        XPLMDebugString("FlyOnSpeed: Failed to create context\n");
        alcCloseDevice(device);
        return false;
    }
    
    alcMakeContextCurrent(context);

    // Generate source and buffers
    alGenSources(1, &audioSource);
    alGenBuffers(1, &audioBufferNormal);
    alGenBuffers(1, &audioBufferHigh);
    
    // Set the initial volume (gain)
    alSourcef(audioSource, AL_GAIN, DEFAULT_VOLUME);
    
    // Precompute the two tone carriers (low = 400 Hz, high = 1600 Hz)
    // via onspeed_core's PCM synth. The buffers are uploaded once and
    // looped by OpenAL for steady tones / cycled manually for pulses.
    auto toneNormal = precomputeTone(TONE_NORMAL_FREQ);
    alBufferData(audioBufferNormal, AL_FORMAT_MONO16, toneNormal.data(),
                 toneNormal.size() * sizeof(int16_t), kBufferSampleRateHz);

    auto toneHigh = precomputeTone(TONE_HIGH_FREQ);
    alBufferData(audioBufferHigh, AL_FORMAT_MONO16, toneHigh.data(),
                 toneHigh.size() * sizeof(int16_t), kBufferSampleRateHz);
    
    // Set initial buffer
    alSourcei(audioSource, AL_BUFFER, audioBufferNormal);
    
    // Configure source to loop
    alSourcei(audioSource, AL_LOOPING, AL_FALSE);
    
    return 0.0f;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static void printMessageDescription(XPWidgetMessage msg) {
    switch(msg) {
        case xpMessage_CloseButtonPushed:
            XPLMDebugString("FlyOnSpeed: xpMessage_CloseButtonPushed\n");
            break;
        case xpMsg_PushButtonPressed:
            XPLMDebugString("FlyOnSpeed: xpMsg_PushButtonPressed\n");
            break;
        case xpMsg_ButtonStateChanged:
            XPLMDebugString("FlyOnSpeed: xpMsg_ButtonStateChanged\n");
            break;
        case xpMsg_TextFieldChanged:
            XPLMDebugString("FlyOnSpeed: xpMsg_TextFieldChanged\n");
            break;
        case xpMsg_ScrollBarSliderPositionChanged:
            XPLMDebugString("FlyOnSpeed: xpMsg_ScrollBarSliderPositionChanged\n");
            break;
        default:
            XPLMDebugString(("FlyOnSpeed: Unknown message type: " + std::to_string(msg) + "\n").c_str());
            break;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Widget handler function
static int AudioControlHandler(
    XPWidgetMessage inMessage,
    [[maybe_unused]] XPWidgetID inWidget,
    intptr_t inParam1,
    [[maybe_unused]] intptr_t inParam2)
{
    printMessageDescription(inMessage);
    //XPLMDebugString(("FlyOnSpeed: AudioControlHandler. Message: " + std::to_string(inMessage) + "\n").c_str());
    if (inMessage == xpMessage_CloseButtonPushed) {
        XPHideWidget(audioControlWidget);
        return 1;
    }

    if (inMessage == xpMsg_PushButtonPressed) {
        XPLMDebugString(("FlyOnSpeed: AudioControlHandler. Button state changed " + std::to_string(inParam1) + "\n").c_str());
        if (inParam1 == reinterpret_cast<intptr_t>(audioToggleCheckbox)) {
            audioEnabled = !audioEnabled;
            //audioEnabled = XPGetWidgetProperty(audioToggleCheckbox, xpProperty_ButtonState, nullptr);
            //XPSetWidgetProperty(audioToggleCheckbox, xpProperty_ButtonState, audioEnabled);
            if(audioEnabled) XPSetWidgetDescriptor(audioToggleCheckbox, "Sound: On");
            else XPSetWidgetDescriptor(audioToggleCheckbox, "Sound: Off");

            XPLMDebugString(("FlyOnSpeed: AudioControlHandler. Button state: " + std::to_string(audioEnabled) + "\n").c_str());
            return 1;
        }
        // Add handler for reload button
        else if (inParam1 == reinterpret_cast<intptr_t>(widgetButtonReload)) {
            XPLMDebugString("FlyOnSpeed: Reloading plugins\n");
            XPLMReloadPlugins();
            return 1;
        }
        // Add handler for update values button. Note: each field is
        // accepted as long as it parses to a positive float — the
        // handler does not check that LDmax < BelowOnSpeed < OnSpeedMax
        // < AboveOnSpeedMax. If a pilot enters values that violate
        // that ordering, onspeed::calculateTone falls back to silence
        // for the misordered region (deliberate fail-safe — silence is
        // safer than wrong tones in flight). Add a UI-side ordering
        // check here if we ever see field reports of confused pilots.
        else if (inParam1 == reinterpret_cast<intptr_t>(widgetButtonUpdateValues)) {
            XPLMDebugString("FlyOnSpeed: Updating AOA values\n");

            // Directly read from text fields when updating
            char buffer[32];
            float value;

            // Get Below LDMax value
            XPGetWidgetDescriptor(widgetAOABelowLDMax, buffer, sizeof(buffer));
            value = atof(buffer);
            if (value > 0) AOA_BELOW_LDMAX = value;
            
            // Get Below OnSpeed value
            XPGetWidgetDescriptor(widgetAOABelowOnSpeed, buffer, sizeof(buffer));
            value = atof(buffer);
            if (value > 0) AOA_BELOW_ONSPEED = value;
            
            // Get OnSpeed Max value
            XPGetWidgetDescriptor(widgetAOAOnSpeedMax, buffer, sizeof(buffer));
            value = atof(buffer);
            if (value > 0) AOA_ONSPEED_MAX = value;
            
            // Get Above OnSpeed Max value
            XPGetWidgetDescriptor(widgetAOAAboveOnSpeedMax, buffer, sizeof(buffer));
            value = atof(buffer);
            if (value > 0) AOA_ABOVE_ONSPEED_MAX = value;
                        
            // Get IAS Tone Enable value
            XPGetWidgetDescriptor(widgetAOAIASToneEnable, buffer, sizeof(buffer));
            value = atof(buffer);
            if (value > 0) AOA_IAS_TONE_ENABLE = value;
            
            // Debug output to check values
            char debugMsg[256];
            snprintf(debugMsg, sizeof(debugMsg), 
                     "FlyOnSpeed: Updated values - IAS Enable: %.1f, Below LDMax: %.1f, Below OnSpeed: %.1f, OnSpeed Max: %.1f, Above OnSpeed: %.1f\n",
                     AOA_IAS_TONE_ENABLE, AOA_BELOW_LDMAX, AOA_BELOW_ONSPEED, AOA_ONSPEED_MAX, AOA_ABOVE_ONSPEED_MAX);
            XPLMDebugString(debugMsg);
            
            // Update the temporary variables to match the new values
            temp_AOA_BELOW_LDMAX = AOA_BELOW_LDMAX;
            temp_AOA_BELOW_ONSPEED = AOA_BELOW_ONSPEED;
            temp_AOA_ONSPEED_MAX = AOA_ONSPEED_MAX;
            temp_AOA_ABOVE_ONSPEED_MAX = AOA_ABOVE_ONSPEED_MAX;
            temp_AOA_IAS_TONE_ENABLE = AOA_IAS_TONE_ENABLE;
            
            // Update the display with the new values
            UpdateAOATextFields();
            
            return 1;
        }
    }
    
    if (inMessage == xpMsg_TextFieldChanged) {
        char buffer[32];
        float value;
        
        if (inParam1 == reinterpret_cast<intptr_t>(widgetAOABelowLDMax)) {
            XPGetWidgetDescriptor(widgetAOABelowLDMax, buffer, sizeof(buffer));
            value = atof(buffer);
            if (value > 0) {
                temp_AOA_BELOW_LDMAX = value;
                XPLMDebugString(("FlyOnSpeed: Text field changed - Below LDMax: " + std::to_string(value) + "\n").c_str());
            }
        }
        else if (inParam1 == reinterpret_cast<intptr_t>(widgetAOABelowOnSpeed)) {
            XPGetWidgetDescriptor(widgetAOABelowOnSpeed, buffer, sizeof(buffer));
            value = atof(buffer);
            if (value > 0) {
                temp_AOA_BELOW_ONSPEED = value;
                XPLMDebugString(("FlyOnSpeed: Text field changed - Below OnSpeed: " + std::to_string(value) + "\n").c_str());
            }
        }
        else if (inParam1 == reinterpret_cast<intptr_t>(widgetAOAOnSpeedMax)) {
            XPGetWidgetDescriptor(widgetAOAOnSpeedMax, buffer, sizeof(buffer));
            value = atof(buffer);
            if (value > 0) {
                temp_AOA_ONSPEED_MAX = value;
                XPLMDebugString(("FlyOnSpeed: Text field changed - OnSpeed Max: " + std::to_string(value) + "\n").c_str());
            }
        }
        else if (inParam1 == reinterpret_cast<intptr_t>(widgetAOAAboveOnSpeedMax)) {
            XPGetWidgetDescriptor(widgetAOAAboveOnSpeedMax, buffer, sizeof(buffer));
            value = atof(buffer);
            if (value > 0) {
                temp_AOA_ABOVE_ONSPEED_MAX = value;
                XPLMDebugString(("FlyOnSpeed: Text field changed - Above OnSpeed: " + std::to_string(value) + "\n").c_str());
            }
        }
        else if (inParam1 == reinterpret_cast<intptr_t>(widgetAOAIASToneEnable)) {
            XPGetWidgetDescriptor(widgetAOAIASToneEnable, buffer, sizeof(buffer));
            value = atof(buffer);
            if (value > 0) {
                temp_AOA_IAS_TONE_ENABLE = value;
                XPLMDebugString(("FlyOnSpeed: Text field changed - IAS Tone Enable: " + std::to_string(value) + "\n").c_str());
            }
        }
        
        return 1;
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Add this helper function before CreateAudioControlWindow
static XPWidgetID createWidget(int widgetClass, const char* description, int leftOffset = 20, int width = 140) {
    if (audioControlWidget == nullptr) {
        return nullptr;
    }
    
    // Get the main window dimensions
    int left, top, right, bottom;
    XPGetWidgetGeometry(audioControlWidget, &left, &top, &right, &bottom);
    
    // If this is the first widget, start from the top
    if (lastWidgetBottom == 0) {
        lastWidgetBottom = top - 15;  // Initial offset from top
    } else {
        lastWidgetBottom -= (WIDGET_HEIGHT + WIDGET_MARGIN);  // Space between widgets
    }
    
    XPWidgetID newWidget = XPCreateWidget(
        left + leftOffset,                    // left
        lastWidgetBottom,                     // top
        left + leftOffset + width,            // right
        lastWidgetBottom - WIDGET_HEIGHT,     // bottom
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
// Add this helper function to create a labeled text field
static XPWidgetID createLabeledTextField(const char* label, float value, int leftOffset = 20) {
    if (audioControlWidget == nullptr) {
        return nullptr;
    }
    
    // Get the main window dimensions
    int left, top, right, bottom;
    XPGetWidgetGeometry(audioControlWidget, &left, &top, &right, &bottom);
    
    // Adjust lastWidgetBottom for the new widget
    lastWidgetBottom -= (WIDGET_HEIGHT + WIDGET_MARGIN);
    
    // Create label widget. The handle is intentionally not stored — the
    // label is a passive caption that the parent window owns and tears
    // down on close.
    XPCreateWidget(
        left + leftOffset,                    // left
        lastWidgetBottom,                     // top
        left + leftOffset + 90,               // right
        lastWidgetBottom - WIDGET_HEIGHT,     // bottom
        1,                                    // visible
        label,                                // descriptor
        0,                                    // not root
        audioControlWidget,                   // container
        xpWidgetClass_Caption                 // class
    );
    
    // Create text field
    XPWidgetID textField = XPCreateWidget(
        left + leftOffset + 100,              // left
        lastWidgetBottom,                     // top
        left + leftOffset + 160,              // right
        lastWidgetBottom - WIDGET_HEIGHT,     // bottom
        1,                                    // visible
        "",                                   // descriptor (will be set below)
        0,                                    // not root
        audioControlWidget,                   // container
        xpWidgetClass_TextField               // class
    );
    
    // Set additional text field properties
    XPSetWidgetProperty(textField, xpProperty_TextFieldType, xpTextEntryField);
    XPSetWidgetProperty(textField, xpProperty_Enabled, 1);
    
    // Set the initial value
    char valueText[16];
    snprintf(valueText, sizeof(valueText), "%.1f", value);
    XPSetWidgetDescriptor(textField, valueText);
    
    XPLMDebugString(("FlyOnSpeed: Created text field for " + std::string(label) + " with initial value " + valueText + "\n").c_str());
    
    return textField;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Modify CreateAudioControlWindow to use the helper function
static void CreateAudioControlWindow(int x, int y, int w, int h) {
    int x2 = x + w;
    int y2 = y - h;
    
    // Reset the lastWidgetBottom for new window
    lastWidgetBottom = 0;
    
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
    
    // Add text fields for editing AOA threshold values
    widgetAOABelowLDMax = createLabeledTextField("Below LDMax:", AOA_BELOW_LDMAX);
    widgetAOABelowOnSpeed = createLabeledTextField("Below OnSpeed:", AOA_BELOW_ONSPEED);
    widgetAOAOnSpeedMax = createLabeledTextField("OnSpeed Max:", AOA_ONSPEED_MAX);
    widgetAOAAboveOnSpeedMax = createLabeledTextField("Above OnSpeed:", AOA_ABOVE_ONSPEED_MAX);
    widgetAOAIASToneEnable = createLabeledTextField("IAS Tone Enable:", AOA_IAS_TONE_ENABLE);
    
    // Add the Update Values button
    widgetButtonUpdateValues = createWidget(
        xpWidgetClass_Button,
        "Update Values"
    );
    
    audioToggleCheckbox = createWidget(
        xpWidgetClass_Button,
        "Sound: Off"
    );
    
    widgetButtonReload = createWidget(
        xpWidgetClass_Button,
        "Reload Plugins"
    );
    
    XPAddWidgetCallback(audioControlWidget, AudioControlHandler);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Add a function to update text fields with current values
static void UpdateAOATextFields() {
    if (!audioControlWidget || !XPIsWidgetVisible(audioControlWidget)) {
        return;
    }
    
    char buffer[16];
    
    // Initialize temporary variables with current values
    temp_AOA_BELOW_LDMAX = AOA_BELOW_LDMAX;
    temp_AOA_BELOW_ONSPEED = AOA_BELOW_ONSPEED;
    temp_AOA_ONSPEED_MAX = AOA_ONSPEED_MAX;
    temp_AOA_ABOVE_ONSPEED_MAX = AOA_ABOVE_ONSPEED_MAX;
    temp_AOA_IAS_TONE_ENABLE = AOA_IAS_TONE_ENABLE;
    
    snprintf(buffer, sizeof(buffer), "%.1f", AOA_BELOW_LDMAX);
    XPSetWidgetDescriptor(widgetAOABelowLDMax, buffer);
    
    snprintf(buffer, sizeof(buffer), "%.1f", AOA_BELOW_ONSPEED);
    XPSetWidgetDescriptor(widgetAOABelowOnSpeed, buffer);
    
    snprintf(buffer, sizeof(buffer), "%.1f", AOA_ONSPEED_MAX);
    XPSetWidgetDescriptor(widgetAOAOnSpeedMax, buffer);
    
    snprintf(buffer, sizeof(buffer), "%.1f", AOA_ABOVE_ONSPEED_MAX);
    XPSetWidgetDescriptor(widgetAOAAboveOnSpeedMax, buffer);
        
    snprintf(buffer, sizeof(buffer), "%.1f", AOA_IAS_TONE_ENABLE);
    XPSetWidgetDescriptor(widgetAOAIASToneEnable, buffer);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Add menu handler
static void AudioMenuHandler([[maybe_unused]] void * mRef, void * iRef)
{
    if (!strcmp(static_cast<const char *>(iRef), "Show")) {
        if (!audioControlWidget) {
            CreateAudioControlWindow(300, 600, 250, 350);
        } else if (!XPIsWidgetVisible(audioControlWidget)) {
            XPShowWidget(audioControlWidget);
            UpdateAOATextFields(); // Update text fields when showing the window
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Pulse thread plays one short buffer per pulse cycle. Region decision
// happened upstream in PlayAOATone (via onspeed_core::calculateTone);
// this thread just consumes (toneFreqHz, pulseFreqHz) atomics and
// switches OpenAL buffers when the frequency changes.
void PulseThreadFunction() {
    float lastFrequency = 0.0f;

    while (threadRunning) {
        if (!audioEnabled || !shouldPlay) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        const float toneFreq  = currentToneFreqHz.load();
        const float pulseFreq = currentPulseFreqHz.load();

        // Defensive: if PlayAOATone hasn't picked a tone yet (or is in
        // a non-pulsing region that forgot to clear shouldPlay), skip.
        if (toneFreq <= 0.0f || pulseFreq <= 0.0f) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        if (lastFrequency != toneFreq) {
            lastFrequency = toneFreq;
            alSourceStop(audioSource);
            alSourcei(audioSource, AL_BUFFER,
                      toneFreq == TONE_HIGH_FREQ ? audioBufferHigh : audioBufferNormal);
        }

        const int sleepMs = static_cast<int>(1000.0f / pulseFreq);

        alSourcei(audioSource, AL_LOOPING, AL_FALSE);
        alSourcePlay(audioSource);

        std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Map the four UI-editable plugin thresholds onto onspeed_core's
// ToneThresholds struct. Mapping is direct:
//    plugin              -> core
//    AOA_BELOW_LDMAX     -> fLDMAXAOA       (silence threshold)
//    AOA_BELOW_ONSPEED   -> fONSPEEDFASTAOA (bottom of OnSpeed band)
//    AOA_ONSPEED_MAX     -> fONSPEEDSLOWAOA (top of OnSpeed band)
//    AOA_ABOVE_ONSPEED_MAX -> fSTALLWARNAOA (stall warning trigger)
static onspeed::ToneThresholds buildThresholds() {
    return {
        AOA_BELOW_LDMAX,
        AOA_BELOW_ONSPEED,
        AOA_ONSPEED_MAX,
        AOA_ABOVE_ONSPEED_MAX,
    };
}

// Run one frame of the audio decision: smooth the raw AOA, decide what
// to play, push the result to the pulse thread (or play it directly).
void PlayAOATone(float aoa, float /* elapsedTime */) {
    // Sensor-style smoothing: median-despike then mean-smooth, same
    // pipeline the firmware uses for pressure samples in SensorIO.
    aoaMedian.add(aoa);
    aoaMean.addValue(aoaMedian.getMedian());
    const float smoothedAoa = aoaMean.getFastAverage();

    const float ias = XPLMGetDataf(iasDataRef);

    char aoaText[64];
    snprintf(aoaText, sizeof(aoaText),
             "AOA: %.1f (smooth: %.1f) IAS: %.1f", aoa, smoothedAoa, ias);
    XPSetWidgetDescriptor(widgetAOAValue, aoaText);

    if (!audioEnabled) {
        shouldPlay = false;
        alSourceStop(audioSource);
        XPSetWidgetDescriptor(widgetAudioStatus, "");
        return;
    }

    // IAS gate: silent on the ground / in initial roll. Plugin-specific;
    // the firmware uses a different mute scheme so this stays in the plugin.
    if (ias < AOA_IAS_TONE_ENABLE) {
        shouldPlay = false;
        alSourceStop(audioSource);
        XPSetWidgetDescriptor(widgetAudioStatus,
            ("Audio: None - Below IAS " + std::to_string(AOA_IAS_TONE_ENABLE)).c_str());
        return;
    }

    // The single source of truth for region decisions. Returns
    // EnToneType (None/Low/High) and a pulse frequency (0 = solid).
    const onspeed::ToneResult result =
        onspeed::calculateTone(smoothedAoa, buildThresholds());

    if (result.enTone == onspeed::EnToneType::None) {
        shouldPlay = false;
        alSourceStop(audioSource);
        XPSetWidgetDescriptor(widgetAudioStatus, "Audio: None");
        return;
    }

    const float toneFreq = (result.enTone == onspeed::EnToneType::High)
                               ? TONE_HIGH_FREQ : TONE_NORMAL_FREQ;

    if (result.fPulseFreq == 0.0f) {
        // Solid tone. Today calculateTone() only returns this in the
        // OnSpeed band (Low/400Hz looped), but we pick the buffer from
        // result.enTone rather than hardcoding audioBufferNormal so a
        // future core change adding a solid High variant can't silently
        // play through the wrong carrier.
        shouldPlay = false;
        alSourcei(audioSource, AL_BUFFER,
                  result.enTone == onspeed::EnToneType::High
                      ? audioBufferHigh : audioBufferNormal);
        alSourcei(audioSource, AL_LOOPING, AL_TRUE);
        ALint state;
        alGetSourcei(audioSource, AL_SOURCE_STATE, &state);
        if (state != AL_PLAYING) {
            alSourcePlay(audioSource);
        }
        XPSetWidgetDescriptor(widgetAudioStatus,
                              result.enTone == onspeed::EnToneType::High
                                  ? "Audio: Steady - High"
                                  : "Audio: Steady - OnSpeed");
        return;
    }

    // Pulsing tone — hand off to the pulse thread via the atomics.
    currentToneFreqHz  = toneFreq;
    currentPulseFreqHz = result.fPulseFreq;
    shouldPlay = true;

    char audioStatusText[64];
    snprintf(audioStatusText, sizeof(audioStatusText),
             "Audio Hz: %.0f pps: %.1f", toneFreq, result.fPulseFreq);
    XPSetWidgetDescriptor(widgetAudioStatus, audioStatusText);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Updated flight loop callback
float CheckAOAAndPlayTone(float inElapsedSinceLastCall,
                          [[maybe_unused]] float inElapsedTimeSinceLastFlightLoop,
                          [[maybe_unused]] int inCounter,
                          [[maybe_unused]] void *inRefcon) {

    // use XPLMGetDataf to get the AOA value.  https://developer.x-plane.com/sdk/XPLMDataAccess/#XPLMDataRef

    float aoa = XPLMGetDataf(aoaDataRef);
    PlayAOATone(aoa, inElapsedSinceLastCall);
    return -1.0f;  // Negative value means "call me next frame"
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Modified XPluginStart to register a flight loop for updating text fields
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

    // if (!initializeAudio()) {
    //     XPLMDebugString("Failed to initialize audio");
    //     return 0;
    // }

	XPLMRegisterFlightLoopCallback(init_sound,-1.0,NULL);	


    // find the AOA DataRef 
    // https://developer.x-plane.com/sdk/XPLMDataAccess/#XPLMDataRef

    // here is a site that shows a list of DataRefs:
    // https://siminnovations.com/xplane/dataref/index.php

    // find the AOA DataRef.
    // use sim/cockpit2/gauges/indicators/AoA_pilot ??
    // or sim/cockpit2/gauges/indicators/aoa_angle_degrees ??
    // sim/flightmodel/position/alpha

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

    // aircraftNameDataRef = XPLMFindDataRef("sim/aircraft/view/acf_name");
    // if (aircraftNameDataRef == nullptr) {
    //     XPLMDebugString("FlyOnSpeed: Failed to find aircraft name DataRef");
    //     return 0;
    // }

    XPLMRegisterFlightLoopCallback(CheckAOAAndPlayTone, 1.0, nullptr);

    // Add menu item
    int item = XPLMAppendMenuItem(XPLMFindPluginsMenu(), "Fly On Speed", nullptr, 1);
    menuId = XPLMCreateMenu("Fly On Speed", XPLMFindPluginsMenu(), item, AudioMenuHandler, nullptr);
    // The SDK takes a non-const void* for menu item refcons; the matching
    // handler casts it back to const char* before strcmp. Discriminator
    // string is a literal — never written through the void*.
    XPLMAppendMenuItem(menuId, "Show",
                       static_cast<void*>(const_cast<char*>("Show")), 1);

    // Start the pulse thread
    threadRunning = true;
    pulseThread = new std::thread(PulseThreadFunction);

    // Initialize temporary variables
    temp_AOA_BELOW_LDMAX = AOA_BELOW_LDMAX;
    temp_AOA_BELOW_ONSPEED = AOA_BELOW_ONSPEED;
    temp_AOA_ONSPEED_MAX = AOA_ONSPEED_MAX;
    temp_AOA_ABOVE_ONSPEED_MAX = AOA_ABOVE_ONSPEED_MAX;
    temp_AOA_IAS_TONE_ENABLE = AOA_IAS_TONE_ENABLE;

    return 1;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Modify XPluginStop to cleanup the thread
PLUGIN_API void XPluginStop(void) {
    // Stop the pulse thread
    threadRunning = false;
    if (pulseThread) {
        pulseThread->join();
        delete pulseThread;
        pulseThread = nullptr;
    }

    if (audioControlWidget) {
        XPDestroyWidget(audioControlWidget, 1);
        audioControlWidget = nullptr;
    }
    XPLMDestroyMenu(menuId);
    XPLMUnregisterFlightLoopCallback(CheckAOAAndPlayTone, nullptr);
    cleanupAudio();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Plugin enable
PLUGIN_API int XPluginEnable(void) {
    return 1;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Plugin disable
PLUGIN_API void XPluginDisable(void) {
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Plugin receive message
PLUGIN_API void XPluginReceiveMessage([[maybe_unused]] XPLMPluginID inFromWho,
                                      [[maybe_unused]] int inMessage,
                                      [[maybe_unused]] void *inParam) {
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Update cleanup function
void cleanupAudio() {
    if (context) {
        alSourceStop(audioSource);
        alDeleteSources(1, &audioSource);
        alDeleteBuffers(1, &audioBufferNormal);
        alDeleteBuffers(1, &audioBufferHigh);
        
        alcMakeContextCurrent(nullptr);
        alcDestroyContext(context);
        context = nullptr;
    }
    
    if (device) {
        alcCloseDevice(device);
        device = nullptr;
    }
}
