
#include "Globals.h"
#include "Helpers.h"
#include "Switch.h"

// Switch State
bool        bSwitchDoSingleClick = false;
bool        bSwitchDoLongPress   = false;

BaseType_t  xWasDelayed;
TickType_t  xLastWakeTime = xTaskGetTickCount();

// ----------------------------------------------------------------------------

// Reboot the system after playing a sound and logging the event.
void RebootFromSwitch()
{
    g_Log.println(MsgLog::EnSwitch, MsgLog::EnDebug, "Reboot triggered by 5-second switch press.");

    // Play a sound to indicate reboot is happening. Using an existing sound as a placeholder.
    // For a better user experience, you could add a new "rebooting" voice prompt.
    // g_AudioPlay.SetVoice(enVoiceCalMode);

    // Wait a moment for the user to hear the sound before restarting.
    // vTaskDelay(pdMS_TO_TICKS(1000));
    _softRestart();
}

// This is the main OneButton task to handle switch presses.
// This same functionality would probably be trivial to duplicate without
// OneButton in the RTOS task.

void SwitchCheckTask(void * pvParams)
    {
    BaseType_t      xWasDelayed;
    TickType_t      xLastWakeTime = xTaskGetTickCount();

    // Setup OneButton switch
    pinMode(SWITCH_PIN,   INPUT_PULLUP);
    // OneButton press timing values are in milliseconds (not "tick()" calls).
    // 1 second long-press for DataMark.
    g_Switch.setPressMs(1000);
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

        // Check for a long press event (1 second)
        if (bSwitchDoLongPress)
            {
            g_iDataMark++;
            g_Log.println(MsgLog::EnSwitch, MsgLog::EnDebug, "Data Mark");
            g_AudioPlay.SetVoice(enVoiceDatamark);
            bSwitchDoLongPress = false;
            }

        // Check for a very long press (5 seconds) to trigger a reboot.
        // getPressedMs() returns the number of milliseconds the button is pressed.
        if (g_Switch.isLongPressed() && (g_Switch.getPressedMs() > 5000))
        {
            // This function will not return.
            RebootFromSwitch();
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
        // The HeartbeatLedTask handles the LED state based on g_bAudioEnable.
        // Play turn on sound
        g_AudioPlay.SetVoice(enVoiceEnabled);

        g_Log.println(MsgLog::EnSwitch, MsgLog::EnDebug, "Audio Enabled");
        }
    else
        {
        // Turn off knob LED
        // The HeartbeatLedTask handles the LED state based on g_bAudioEnable.
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
