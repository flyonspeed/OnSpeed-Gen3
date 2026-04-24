# AOA Tone Fly On Speed

This plugin plays audio tones based on the Angle of Attack (AOA) and Fly On Speed 

## Releases

You can install a prebuilt mac or windows version of this plugin.

[Mac Release](https://github.com/flyonspeed/OnSpeed-XPlane/releases/tag/0.1) 

[Windows Release](https://github.com/flyonspeed/OnSpeed-XPlane/releases/tag/v0.1-Windows-debug-Compile)


## Building this plugin

To build this plugin you will need the XPlane SDK to be installed. You can download it from the [XPlane SDK](https://developer.x-plane.com/sdk/).

Currently using: X-Plane SDK 4.1.1 for Windows, Linux and Mac ZIP (X-Plane 12.1.0 & newer)

## Build on Mac

```bash
mkdir build
cd build
cmake ..
```

on unix/macos:

inside the 'build' folder run the following.

```bash
make
```

output  ```build/mac_x64/AOA-Tone-FlyOnSpeed.dylib```

The .dylib file needs to be copied to the ```Resources/plugins``` folder in your XPlane installation.
and renamed to .xpl

## Build on Windows (Visual Code 2022)

clone the repository

![Screenshot 2025-04-21 011649](https://github.com/user-attachments/assets/104a3b06-e479-42b8-bd2e-e94c481fc768)

install [OpenAl for windows](https://www.openal-soft.org/openal-binaries/openal-soft-1.24.3-bin.zip)

extract the zip to the root of the project

![Screenshot 2025-04-21 012550](https://github.com/user-attachments/assets/cf52fbe5-6876-4276-b1a9-fbe50dcb4811)

Go to the CMakeSettings.json in the CMake Settings Editor and update the CMake variables for OPENAL_INCLUDE_DIR and OPENAL_LIBRARY

![Screenshot 2025-04-21 012812](https://github.com/user-attachments/assets/ac0b2c81-308b-426f-8994-fa58768c9567)
![Screenshot 2025-04-21 015310](https://github.com/user-attachments/assets/f006b9ba-3fb3-4451-bdd2-0e6ef7ddcadf)

output ```out\build\x64-Debug\AOA-Tone-FlyOnSpeed.xpl```
The file needs to be copied to the ```Resources/plugins``` folder in your XPlane installation.

## Code notes

You can adjust the frequencies and durations by modifying the values in the initializeAudio() function. The tones will play continuously as long as the AOA is in their respective ranges, and will switch immediately when the AOA changes ranges.

Remember to install OpenAL development libraries on your system:
On Windows: Install OpenAL SDK
On Linux: sudo apt-get install libopenal-dev
On macOS: OpenAL is included in the system

## Install

