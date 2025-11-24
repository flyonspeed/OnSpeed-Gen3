
#include "Globals.h"
#include "Switch.h"

// Switch State
bool        bSwitchDoSingleClick = false;
bool        bSwitchDoLongPress   = false;

BaseType_t  xWasDelayed;
TickType_t  xLastWakeTime = xTaskGetTickCount();

// ----------------------------------------------------------------------------

// This is the main OneButton task to handle switch presses.
// This same functionality would probably be trivial to duplicate without
// OneButton in the RTOS task.

void SwitchCheckTask(void * pvParams)
    {
    BaseType_t      xWasDelayed;
    TickType_t      xLastWakeTime = xTaskGetTickCount();

    // Setup OneButton switch
    pinMode(SWITCH_PIN,   INPUT_PULLUP);
    g_Switch.setPressTicks(1000); // long press time
    g_Switch.attachClick(SwitchSingleClick);
    g_Switch.attachLongPressStart(SwitchLongPress);

    while (true)
    {
        // Run every 10 msec
        vTaskDelay(pdMS_TO_TICKS(10));

        // Let OneButton do its thing
        g_Switch.tick();

        // Check for a single click event
        if (bSwitchDoSingleClick)
            {
            ToggleAudioEnable();
            bSwitchDoSingleClick = false;
            }

        // Check for a long press event
        if (bSwitchDoLongPress)
            {
            g_iDataMark++;
            g_Log.println(MsgLog::EnSwitch, MsgLog::EnDebug, "Data Mark");
            g_AudioPlay.SetVoice(enVoiceDatamark);
            bSwitchDoLongPress = false;
            }
        } // end while forever
    }


// ----------------------------------------------------------------------------

// Toggle the audio enable switch value.

void ToggleAudioEnable()
    {
    // Toggle audio enable
    g_bAudioEnable = !g_bAudioEnable;

    if (g_bAudioEnable)
        {
        // Turn on knob LED. No analog output with ESP32.
        digitalWrite(PIN_LED_KNOB, 1);
//        analogWrite(PIN_LED_KNOB,200); No analog output with ESP32

        // Play turn on sound
        g_AudioPlay.SetVoice(enVoiceEnabled);

        g_Log.println(MsgLog::EnSwitch, MsgLog::EnDebug, "Audio Enabled");
        }
    else
        {
        // Turn off knob LED
        digitalWrite(PIN_LED_KNOB, 1);
//        analogWrite(PIN_LED_KNOB, 0);

        // Play turn off sound
        g_AudioPlay.SetVoice(enVoiceDisabled);

        g_Log.println(MsgLog::EnSwitch, MsgLog::EnDebug, "Audio Disabled");
        }
    }


// ----------------------------------------------------------------------------

// Switch callback to handle single clicks

void SwitchSingleClick()
    {
    bSwitchDoSingleClick = true;
    }


// ----------------------------------------------------------------------------

// Switch callback to handle long press

void SwitchLongPress()
    {
    bSwitchDoLongPress = true;
    }
