
#include "Globals.h"
#include "HeartBeat.h"

// Use 8 bit precision for LEDC timer
#define LEDC_TIMER_8_BIT    8

// Use 5000 Hz as a LEDC base frequency
 #define LEDC_BASE_FREQ     5000

// LED channel that will be used instead of automatic selection.
#define LEDC_CHANNEL        0

// ----------------------------------------------------------------------------

// Task to make the panel LED blink.
// Audio enabled: fast blink (300ms). Muted: slow blink (1000ms) to indicate stall warning still active.

void HeartbeatLedTask(void * pvParams)
{
    // state for the blinking LED
    static bool ledOn = false;

    // Setup output pin
    pinMode(PIN_LED_KNOB, OUTPUT);

    // Setup the PWM output
    ledcAttachChannel(PIN_LED_KNOB, LEDC_BASE_FREQ, LEDC_TIMER_8_BIT, LEDC_CHANNEL);

    // Tick counter for slow blink timing (muted state)
    static int slowBlinkCounter = 0;

    while (true)
    {
        // Base loop runs at 300ms — fast blink interval
        vTaskDelay(pdMS_TO_TICKS(300));

        if (g_bAudioEnable)
        {
            // Audio enabled: fast blink (300ms interval)
            slowBlinkCounter = 0;
            if (ledOn)
            {
                ledcWriteChannel(LEDC_CHANNEL, 0);
                ledOn = false;
            }
            else
            {
                ledcWriteChannel(LEDC_CHANNEL, 200);
                ledOn = true;
            }
        }
        else
        {
            // Audio muted: slow blink (1000ms ≈ 3 ticks of 300ms)
            slowBlinkCounter++;
            if (slowBlinkCounter >= 3)
            {
                slowBlinkCounter = 0;
                if (ledOn)
                {
                    ledcWriteChannel(LEDC_CHANNEL, 0);
                    ledOn = false;
                }
                else
                {
                    ledcWriteChannel(LEDC_CHANNEL, 200);
                    ledOn = true;
                }
            }
        }
    } // end while forever
} // end HeartbeatLedTask()