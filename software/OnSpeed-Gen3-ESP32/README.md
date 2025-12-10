# OnSpeed-Gen3-ESP32 Software Development Notes

FlyOnSpeed (FOS) Gen 3 software is targetted for the ESP32-S3-WROOM-2 system on a chip made by
Espressif. Different FOS platforms may have slightly different versions of this device
but for use in FOS all should be compatible.

The ESP32-S3-WROOM-2 comes with varying amounts of Flash RAM and external PSRAM accessed via
a dedicated SPI interface. All FOS hardware use the 32 MB Flash variant. Currently PSRAM is
not used and so the size of PSRAM available is not significant.

Gen 3 compiles under the Arduino 2 integrated development environment (IDE). After installing
the Arduino 2 IDE support for the ESP32-S3 is included via the Arduino Board Manager. Before
using Board Manager the Preferences must be updated to include the URL of the Espressif
ESP32 boards to be used. In the "Additional Boards Manager URLs" include the following URL...

    https://espressif.github.io/arduino-esp32/package_esp32_index.json

If you anticipate working with M5 Stack code also then also include the following URL...

    https://static-cdn.m5stack.com/resource/arduino/package_m5stack_index.json

In the Preferences dialog also make sure your sketch location is correctly set. This is
necessary to find the necessary libraries.

From the Arduino 2 Board Manager select the ESP32S3 Dev Module Octal (WROOM2) device. Then under
the Tools menu select at least the following...

    Flash Mode: "OPI 80 MHz"
    Flash Size: "32MB (256 Mb)"
    Partition Scheme: "32M Flash (4.8MB APP/22MB LittleFS)
    Upload Speed: "921600"

## Required Libraries

-- Non-arduino Libs
    csv-parser
    tinyxml2

-- Arduino Libs
    Adafruit_NeoPixel           by Adafruit 1.15.2
    SavitzkyGolayFilter-master  by James Deromedi 1.0.1 (1.0.0???)
    SdFat                       by Bill Greiman 2.3.0
    RunningAverage              by Rob Tilaart 0.4.8
    RunningMedian               by Rob Tilaart 0.3.10
    BasicLinearAlgebra          by Tom Stewart 5.1
    DallasTemperature           by Miles Burton 3.9.0
    OneButton                   by Matthias Hertel 2.6.1
    OneWire                     by Jim Studt, et al 2.3.8
    WebSockets                  by Markus Sattler 2.6.1

-- Arduino Libs not needed for now
    EspSoftwareSerial           by Dirk Kaar, et al 8.1.0
