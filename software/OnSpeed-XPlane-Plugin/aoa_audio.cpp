#define FLYONSPEED_VERSION  "0.1.12"
#define FLYONSPEED_DATE     "4/23/2025"

#define XPLM_64 1  // Define XPLM_64 as 1 for 64-bit compatibility
#ifndef XPLM_API   
#define XPLM_API   // XPLM_API 
#endif

#ifdef _WIN32
    #define PLUGIN_API __declspec(dllexport)
    #include <AL/al.h>
    #include <AL/alc.h>
    #include <sstream>
    #include <cmath>
    #define _USE_MATH_DEFINES
    #define IBM 1
    #define XPWIDGETS 1
    #define XPMENUS 1
    #define XPLM_64 1

#else
    #if defined(__APPLE__)
        #define APL 1
        #define IBM 0
        #define LIN 0
        #define XPMENUS 1
        #define XPLM_64 1
    #endif
    #include <OpenAL/al.h>
    #include <OpenAL/alc.h>
#endif

#include "SDK/CHeaders/XPLM/XPLMDisplay.h"
#include "SDK/CHeaders/XPLM/XPLMGraphics.h"
#include "SDK/CHeaders/XPLM/XPLMProcessing.h"
#include "SDK/CHeaders/XPLM/XPLMDataAccess.h"
#include "SDK/CHeaders/XPLM/XPLMPlugin.h"
#include "SDK/CHeaders/XPLM/XPLMUtilities.h"
#include "SDK/CHeaders/Widgets/XPWidgets.h"
#include "SDK/CHeaders/Widgets/XPStandardWidgets.h"
#include "SDK/CHeaders/Widgets/XPWidgetUtils.h"
#include "SDK/CHeaders/XPLM/XPLMMenus.h"

#include <iostream>
#include <cmath>
#include <vector>
#include <cstring>
#include <thread>
#include <mutex>
#include <atomic>

// Function declarations
void cleanupAudio();
static void UpdateAOATextFields();

// AOA ranges for different states (default values)
float AOA_BELOW_LDMAX           = 6.0f;     // Below this is "Below LDMax" - no tone
float AOA_BELOW_ONSPEED         = 7.3f;     // Between LDMax and this is "Below OnSpeed"
float AOA_ONSPEED_MAX           = 9.6f;     // Between Below OnSpeed and this is "OnSpeed"
float AOA_ABOVE_ONSPEED_MAX     = 12.5f;    // Above this is "Above OnSpeed"

float AOA_IAS_TONE_ENABLE       = 25.0f;    // IAS (knots) above this value will enable the tone

// Tone configuration
#define TONE_NORMAL_FREQ       400.0f   // Normal frequency in Hz
#define TONE_HIGH_FREQ         1600.0f  // High frequency in Hz
#define PULSE_RATE_NORMAL      6.2f     // Standard pulse rate
#define PULSE_RATE_STALL       20.0f    // Stall warning pulse rate
#define DEFAULT_VOLUME          1.0f    // Default volume level (0.0 to 1.0)

// Add these new constants
#define PULSE_RATE_MIN         1.5f    // Minimum pulses per second at AOA_BELOW_LDMAX
#define PULSE_RATE_MAX         8.2f    // Maximum pulses per second at AOA_BELOW_ONSPEED

// Add these new constants near the other constants
#define ABOVE_ONSPEED_PULSE_MIN  1.5f    // Minimum pulses per second at AOA_ONSPEED_MAX
#define ABOVE_ONSPEED_PULSE_MAX  6.2f    // Maximum pulses per second at AOA_ABOVE_ONSPEED_MAX

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

// Add these globals at the top of the file with other globals
const int AOA_HISTORY_SIZE = 20;
std::vector<float> aoaHistory;
float lastValidAoa = 0.0f;
const float MAX_AOA_CHANGE = 6.0f;  // Maximum allowed change in degrees

// Add these globals with other globals
std::thread* pulseThread = nullptr;
std::mutex aoaMutex;
std::atomic<bool> threadRunning{false};
std::atomic<float> currentAOA{0.0f};
std::atomic<bool> shouldPlay{false};

float audioFrequency = TONE_NORMAL_FREQ;
float audioPulseRate = PULSE_RATE_NORMAL;

// Add these globals with other globals
static int lastWidgetBottom = 0;
static const int WIDGET_HEIGHT = 20;
static const int WIDGET_MARGIN = 5;

// Add these globals with other globals
static XPWidgetID widgetAOABelowLDMax = nullptr;
static XPWidgetID widgetAOABelowOnSpeed = nullptr;
static XPWidgetID widgetAOAOnSpeedMax = nullptr;
static XPWidgetID widgetAOAAboveOnSpeedMax = nullptr;
static XPWidgetID widgetAOAStallWarning = nullptr;
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
// Function to generate a sine wave tone
std::vector<ALshort> generateTone(float frequency, float duration, int sampleRate = 44100) {
    std::vector<ALshort> buffer;
    int samples = static_cast<int>(duration * sampleRate);
    buffer.reserve(samples);
    
    for (int i = 0; i < samples; i++) {
        float t = static_cast<float>(i) / sampleRate;
        float value = 32767.0f * std::sin(2.0f * M_PI * frequency * t);
        buffer.push_back(static_cast<ALshort>(value));
    }
    
    return buffer;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Function to initialize OpenAL and create tone
static float init_sound(float elapsed, float elapsed_sim, int counter, void * ref)
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
    
    // Create normal frequency tone
    auto toneNormal = generateTone(TONE_NORMAL_FREQ, 0.1f);
    alBufferData(audioBufferNormal, AL_FORMAT_MONO16, toneNormal.data(), 
                 toneNormal.size() * sizeof(ALshort), 44100);
                 
    // Create high frequency tone
    auto toneHigh = generateTone(TONE_HIGH_FREQ, 0.1f);
    alBufferData(audioBufferHigh, AL_FORMAT_MONO16, toneHigh.data(), 
                 toneHigh.size() * sizeof(ALshort), 44100);
    
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
    XPWidgetID inWidget,
    intptr_t inParam1,
    intptr_t inParam2)
{
    printMessageDescription(inMessage);
    //XPLMDebugString(("FlyOnSpeed: AudioControlHandler. Message: " + std::to_string(inMessage) + "\n").c_str());
    if (inMessage == xpMessage_CloseButtonPushed) {
        XPHideWidget(audioControlWidget);
        return 1;
    }

    if (inMessage == xpMsg_PushButtonPressed) {
        XPLMDebugString(("FlyOnSpeed: AudioControlHandler. Button state changed " + std::to_string(inParam1) + "\n").c_str());
        if (inParam1 == (intptr_t)audioToggleCheckbox) {
            audioEnabled = !audioEnabled;
            //audioEnabled = XPGetWidgetProperty(audioToggleCheckbox, xpProperty_ButtonState, nullptr);
            //XPSetWidgetProperty(audioToggleCheckbox, xpProperty_ButtonState, audioEnabled);
            if(audioEnabled) XPSetWidgetDescriptor(audioToggleCheckbox, "Sound: On");
            else XPSetWidgetDescriptor(audioToggleCheckbox, "Sound: Off");

            XPLMDebugString(("FlyOnSpeed: AudioControlHandler. Button state: " + std::to_string(audioEnabled) + "\n").c_str());
            return 1;
        }
        // Add handler for reload button
        else if (inParam1 == (intptr_t)widgetButtonReload) {
            XPLMDebugString("FlyOnSpeed: Reloading plugins\n");
            XPLMReloadPlugins();
            return 1;
        }
        // Add handler for update values button
        else if (inParam1 == (intptr_t)widgetButtonUpdateValues) {
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
        
        if (inParam1 == (intptr_t)widgetAOABelowLDMax) {
            XPGetWidgetDescriptor(widgetAOABelowLDMax, buffer, sizeof(buffer));
            value = atof(buffer);
            if (value > 0) {
                temp_AOA_BELOW_LDMAX = value;
                XPLMDebugString(("FlyOnSpeed: Text field changed - Below LDMax: " + std::to_string(value) + "\n").c_str());
            }
        }
        else if (inParam1 == (intptr_t)widgetAOABelowOnSpeed) {
            XPGetWidgetDescriptor(widgetAOABelowOnSpeed, buffer, sizeof(buffer));
            value = atof(buffer);
            if (value > 0) {
                temp_AOA_BELOW_ONSPEED = value;
                XPLMDebugString(("FlyOnSpeed: Text field changed - Below OnSpeed: " + std::to_string(value) + "\n").c_str());
            }
        }
        else if (inParam1 == (intptr_t)widgetAOAOnSpeedMax) {
            XPGetWidgetDescriptor(widgetAOAOnSpeedMax, buffer, sizeof(buffer));
            value = atof(buffer);
            if (value > 0) {
                temp_AOA_ONSPEED_MAX = value;
                XPLMDebugString(("FlyOnSpeed: Text field changed - OnSpeed Max: " + std::to_string(value) + "\n").c_str());
            }
        }
        else if (inParam1 == (intptr_t)widgetAOAAboveOnSpeedMax) {
            XPGetWidgetDescriptor(widgetAOAAboveOnSpeedMax, buffer, sizeof(buffer));
            value = atof(buffer);
            if (value > 0) {
                temp_AOA_ABOVE_ONSPEED_MAX = value;
                XPLMDebugString(("FlyOnSpeed: Text field changed - Above OnSpeed: " + std::to_string(value) + "\n").c_str());
            }
        }
        else if (inParam1 == (intptr_t)widgetAOAIASToneEnable) {
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
    
    // Create label
    XPWidgetID labelWidget = XPCreateWidget(
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
    snprintf(versionText, sizeof(versionText), "Version: %s %s", FLYONSPEED_VERSION, FLYONSPEED_DATE);
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
static void AudioMenuHandler(void * mRef, void * iRef)
{
    if (!strcmp((char *)iRef, "Show")) {
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
// Modify the PulseThreadFunction to handle variable pulse rates
void PulseThreadFunction() {
    float lastFrequency = 0.0f;

    while (threadRunning) {

        if (!audioEnabled || !shouldPlay) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        // localAOA is the current AOA, using a mutex to access it because it is updated in the flight loop
        float localAOA;
        {
            std::lock_guard<std::mutex> lock(aoaMutex);
            localAOA = currentAOA;
        }

        // Determine pulse rate and frequency based on AOA
        if (localAOA > AOA_ABOVE_ONSPEED_MAX) {
            audioPulseRate = PULSE_RATE_STALL;
            audioFrequency = TONE_HIGH_FREQ;
        } else if (localAOA > AOA_ONSPEED_MAX) {
            // Calculate variable pulse rate for Above OnSpeed condition
            float t = (localAOA - AOA_ONSPEED_MAX) / (AOA_ABOVE_ONSPEED_MAX - AOA_ONSPEED_MAX);
            audioPulseRate = ABOVE_ONSPEED_PULSE_MIN + t * (ABOVE_ONSPEED_PULSE_MAX - ABOVE_ONSPEED_PULSE_MIN);
            audioFrequency = TONE_HIGH_FREQ;
        } else if (localAOA >= AOA_BELOW_LDMAX && localAOA <= AOA_BELOW_ONSPEED) {
            float t = (localAOA - AOA_BELOW_LDMAX) / (AOA_BELOW_ONSPEED - AOA_BELOW_LDMAX);
            audioPulseRate = PULSE_RATE_MIN + t * (PULSE_RATE_MAX - PULSE_RATE_MIN);
            audioFrequency = TONE_NORMAL_FREQ;
        }

        // If frequency changed, switch buffers
        if (lastFrequency != audioFrequency) {
            lastFrequency = audioFrequency;
            alSourceStop(audioSource);
            if (audioFrequency == TONE_HIGH_FREQ) {
                alSourcei(audioSource, AL_BUFFER, audioBufferHigh);
            } else {
                alSourcei(audioSource, AL_BUFFER, audioBufferNormal);
            }
        }

        // Calculate sleep duration based on pulse rate
        int sleepMs = static_cast<int>(1000.0f / audioPulseRate);

        alSourcei(audioSource, AL_LOOPING, AL_FALSE);
        alSourcePlay(audioSource);

        std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Modify PlayAOATone to handle the Below OnSpeed condition
void PlayAOATone(float aoa, float elapsedTime) {
    // Spike filter - if change is too large, use last valid value
    if (std::abs(aoa - lastValidAoa) > MAX_AOA_CHANGE) {
        aoa = lastValidAoa;
    } else {
        lastValidAoa = aoa;
    }

    // Update moving average
    if (aoaHistory.size() >= AOA_HISTORY_SIZE) {
        aoaHistory.erase(aoaHistory.begin());
    }
    aoaHistory.push_back(aoa);

    // Calculate moving average
    float avgAoa = 0.0f;
    for (float value : aoaHistory) {
        avgAoa += value;
    }
    avgAoa /= aoaHistory.size();

    float ias = XPLMGetDataf(iasDataRef);

    // Update widgetAOAValue with both current and averaged AOA values
    char aoaText[50];
    snprintf(aoaText, sizeof(aoaText), "AOA: %.1f (avg: %.1f) IAS: %.1f", aoa, avgAoa, ias);
    XPSetWidgetDescriptor(widgetAOAValue, aoaText);


    if (!audioEnabled) {
        shouldPlay = false;
        alSourceStop(audioSource);
        XPSetWidgetDescriptor(widgetAudioStatus, "");
        return;
    }

    // Check if IAS is above the threshold
    if (ias < AOA_IAS_TONE_ENABLE) {
        shouldPlay = false;
        alSourceStop(audioSource);
        XPSetWidgetDescriptor(widgetAudioStatus, ("Audio: None - Below IAS " + std::to_string(AOA_IAS_TONE_ENABLE)).c_str());
        return;
    }

    // Update the current AOA for the pulse thread
    {
        std::lock_guard<std::mutex> lock(aoaMutex);
        currentAOA = avgAoa;
    }

    if (avgAoa < AOA_BELOW_LDMAX) {
        shouldPlay = false;
        alSourceStop(audioSource);
        XPSetWidgetDescriptor(widgetAudioStatus, ("Audio: None - Below L/DMax " + std::to_string(AOA_BELOW_LDMAX)).c_str());
        return;
    }
    
    // Handle steady tone for OnSpeed condition
    if (avgAoa >= AOA_BELOW_ONSPEED && avgAoa <= AOA_ONSPEED_MAX) {
        shouldPlay = false;  // Disable pulsing
        alSourcef(audioSource, AL_FREQUENCY, TONE_NORMAL_FREQ);
        alSourcei(audioSource, AL_LOOPING, AL_TRUE);
        ALint state;
        alGetSourcei(audioSource, AL_SOURCE_STATE, &state);
        if (state != AL_PLAYING) {
            alSourcePlay(audioSource);
        }
        XPSetWidgetDescriptor(widgetAudioStatus, "Audio: Steady - OnSpeed");
    } else {
        shouldPlay = true;  // Enable pulsing for all other conditions

        char audioStatusText[50];
        snprintf(audioStatusText, sizeof(audioStatusText), "Audio Hz: %.1f pps: %.1f", audioFrequency, audioPulseRate);
        XPSetWidgetDescriptor(widgetAudioStatus, audioStatusText);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Updated flight loop callback
float CheckAOAAndPlayTone(float inElapsedSinceLastCall, 
                         float inElapsedTimeSinceLastFlightLoop, 
                         int inCounter, 
                         void *inRefcon) {

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
    XPLMAppendMenuItem(menuId, "Show", (void*)"Show", 1);

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
PLUGIN_API void XPluginReceiveMessage(XPLMPluginID inFromWho, int inMessage, void *inParam) {
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
