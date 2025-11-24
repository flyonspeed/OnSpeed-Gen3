
#define USE_NEOPIXEL

#include "Globals.h"
#include "HeartBeat.h"

#ifdef USE_NEOPIXEL
#include <Adafruit_NeoPixel.h>
#endif

// Use 8 bit precision for LEDC timer
#define LEDC_TIMER_8_BIT    8

// Use 5000 Hz as a LEDC base frequency
 #define LEDC_BASE_FREQ     5000

// LED channel that will be used instead of automatic selection.
#define LEDC_CHANNEL        0

#ifdef USE_NEOPIXEL
#define NEOPIXEL_PIN        38
#define NUMPIXELS           1
Adafruit_NeoPixel Pixels(NUMPIXELS, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);
#endif

// ----------------------------------------------------------------------------

// Task to make the panel LED fade in and out when audio is enabled.
// It might be nice to put in support for error code blinks someday.

void HeartbeatLedTask(void * pvParams)
    {
    int         iBrightness    =  0;  // how bright the LED is
    int         iFadeAmount    = 25;  // how many points to fade the LED by

    // Setup output pin
    pinMode(PIN_LED_KNOB, OUTPUT);

    // Setup the PWM output
    ledcAttachChannel(PIN_LED_KNOB, LEDC_BASE_FREQ, LEDC_TIMER_8_BIT, LEDC_CHANNEL);

#ifdef USE_NEOPIXEL
    Pixels.begin();
#endif

    while (true)
        {
        vTaskDelay(pdMS_TO_TICKS(50));

        // Set the brightness on LEDC channel 0
        ledcWriteChannel(LEDC_CHANNEL, iBrightness);

#ifdef USE_NEOPIXEL
        Pixels.setPixelColor(0, Pixels.Color(iBrightness/64, iBrightness/64, iBrightness/64));
        Pixels.show();
#endif

        if (g_bAudioEnable)
            {
#if 1
            // Fade in and out
            // ---------------

            // Change the brightness for next time through the loop:
            iBrightness = iBrightness + iFadeAmount;
            iBrightness = iBrightness <= 255 ? iBrightness : 255;
            iBrightness = iBrightness >=   0 ? iBrightness :   0;

            // reverse the direction of the fading at the ends of the fade:
            if (((iBrightness <=  25) && (iFadeAmount < 0)) ||
                ((iBrightness >= 200) && (iFadeAmount > 0)))
                {
                iFadeAmount = -iFadeAmount;
                }
#else
            // Breathing
            // ---------

            // This logic is cool but more CPU intensive. Also, sometimes sin() can get
            // weird when the argument is a large value. It is much better behaved when
            // in the usual +2pi to -2pi range.
            // Funky sine wave, https://sean.voisen.org/blog/2011/10/breathing-led-with-arduino/
            iBrightness = 15+(exp(sin(millis()/2000.0*PI)) - 0.36787944)*63.81;
#endif
            } // end if audio has been enabled

        else
            iBrightness = 50;

        } // end while forever
    } // end HeartbeatLedTask()