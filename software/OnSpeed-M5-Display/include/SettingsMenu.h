// software/OnSpeed-M5-Display/include/SettingsMenu.h
//
// In-flight settings menu for the OnSpeed display. Implements issue #419
// — runtime KTS/MPH toggle persisted to NVS — and provides a list-driven
// framework for future settings.
//
// The pure-logic state machine lives in lib/MenuModel/. This file is the
// platform glue: M5GFX render, NVS read/write, button polling, lifecycle.
//
// Lifecycle (called from main.cpp's loop()):
//   1. After M5.update() and SerialRead(), if isSettingsMenuActive()
//      returns true, the caller should call tickSettingsMenu() and return
//      early — the menu owns the screen and the live-mode render is
//      suppressed for the duration. SerialRead() runs every iteration
//      regardless so the UART RX FIFO keeps draining (the OnSpeed wire
//      pushes 77 B × 20 Hz ≈ 1540 B/s; the ESP32 RX FIFO is 256 B and
//      would overflow inside ~200 ms otherwise).
//   2. Otherwise, on the platform-specific entry gesture (BtnB long-hold
//      on M5, BtnA long-hold on huVVer), call enterSettingsMenu().
#pragma once

// Initialize from NVS. Call from setup() after M5.begin() and after the
// existing brightness / displayType reads. Reads the persisted SpeedMph
// preference; default false (KTS) — pilots flip via the settings menu
// and the choice persists across reboots.
void initSettingsMenu();

// Enter the menu: builds the items array, resets MenuModel state. Allocates
// the menu's off-screen sprite. Idempotent if already active.
void enterSettingsMenu();

// Per-loop tick while the menu is active. Polls M5 buttons (M5.BtnA/B/C
// and BtnD on huVVer), advances MenuModel idle timer, renders the frame,
// handles the "wants exit" flag (deletes the sprite, sets active=false).
void tickSettingsMenu();

// True between enterSettingsMenu() and the tick that processes wantsExit().
bool isSettingsMenuActive();

// The runtime preference. Read by main.cpp's IAS-render block. Default
// false (KTS) on a fresh device with no saved preference.
extern bool g_speedInMph;

// Data source: 0 = AUTO (auto-detect at boot, default), 1 = UART
// (Serial2-only probe, ignore USB-CDC), 2 = USB (skip probe, force
// USB-CDC). Read by SerialRead.cpp::serialSetup() to gate boot probing.
// Loaded from NVS by initSettingsMenu(); changes via the settings menu
// persist immediately. NVS key "DataSource" in namespace "OnSpeed".
extern int g_dataSource;
