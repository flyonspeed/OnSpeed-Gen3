
#include "Globals.h"
#include "HeartBeat.h"

// Use 8 bit precision for LEDC timer
#define LEDC_TIMER_8_BIT    8

// Use 5000 Hz as a LEDC base frequency
 #define LEDC_BASE_FREQ     5000

// LED channel that will be used instead of automatic selection.
#define LEDC_CHANNEL        0

// ----------------------------------------------------------------------------

// Task to make the panel LED blink when audio is enabled.
// It might be nice to put in support for error code blinks someday.

void HeartbeatLedTask(void * pvParams)
{
    // state for the blinking LED
    static bool ledOn = false;

    // Setup output pin
    pinMode(PIN_LED_KNOB, OUTPUT);

    // Setup the PWM output
    ledcAttachChannel(PIN_LED_KNOB, LEDC_BASE_FREQ, LEDC_TIMER_8_BIT, LEDC_CHANNEL);

    while (true)
    {
        // The user requested a 300ms interval for the heartbeat.
        vTaskDelay(pdMS_TO_TICKS(300));

        if (g_bAudioEnable)
        {
            // If audio is enabled, blink the LED.
            if (ledOn)
            {
                ledcWriteChannel(LEDC_CHANNEL, 0); // Turn LED off
                ledOn = false;
            }
            else
            {
                ledcWriteChannel(LEDC_CHANNEL, 200); // Turn LED on (brightness 200)
                ledOn = true;
            }
        }
        else
        {
            // If audio is disabled, ensure the LED is off.
            ledcWriteChannel(LEDC_CHANNEL, 0);
            ledOn = false; // Reset state for when it's re-enabled
        }
    } // end while forever
} // end HeartbeatLedTask()