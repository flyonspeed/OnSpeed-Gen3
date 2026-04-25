// HuvverPinSetup.h — TFT_eSPI User_Setup-equivalent for huVVer-AVI
// (ESP32 + ST7789 240x320 SPI display). Force-included via
// `-include "$PROJECT_DIR/sim/HuvverPinSetup.h"` and paired with
// `-DUSER_SETUP_LOADED` so TFT_eSPI's default User_Setup.h is skipped.
//
// Pin map taken verbatim from V.R. Little's huVVer upstream
// (OnSpeed_huVVer_display/my_custom_setup.h, "HUVVER_AVI" branch).

#pragma once

// Driver
#define ST7789_DRIVER
#define TFT_RGB_ORDER  TFT_BGR
#define TFT_WIDTH      240
#define TFT_HEIGHT     320
#define TFT_INVERSION_OFF

// Backlight
#define TFT_BL              15
#define TFT_BACKLIGHT_ON    HIGH

// SPI pins (HSPI default mapping on classic ESP32)
#define TFT_MISO   19
#define TFT_MOSI   23
#define TFT_SCLK   18
#define TFT_CS      5
#define TFT_DC      2
#define TFT_RST     0

// Fonts loaded into flash
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF

// SPI clocks
#define SPI_FREQUENCY        40000000
#define SPI_READ_FREQUENCY    6000000
#define SPI_TOUCH_FREQUENCY   2500000
