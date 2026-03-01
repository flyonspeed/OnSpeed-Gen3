/*
 * Build information defaults — weak symbols for Arduino IDE fallback.
 *
 * When PlatformIO builds the firmware, generate_buildinfo.py creates
 * buildinfo.cpp with strong (non-weak) symbols that override these.
 * When building with Arduino IDE (no pre-build script), these weak
 * defaults provide safe fallback values.
 *
 * IMPORTANT: When tagging a new release (e.g. v4.16.0), update the
 * version, versionMajor, versionMinor, and versionPatch values below
 * to match the new tag. Otherwise Arduino IDE builds will report the
 * wrong version.
 */

#include "buildinfo.h"

namespace BuildInfo {

// char* pointers: const qualifies the pointed-to data, not the pointer,
// so they already have external linkage and weak works directly.
__attribute__((weak)) const char* version      = "4.15.0";
__attribute__((weak)) const char* gitSha       = "unknown";
__attribute__((weak)) const char* gitShortSha  = "unknown";
__attribute__((weak)) const char* gitBranch    = "unknown";
__attribute__((weak)) const char* buildDate    = "unknown";

// Scalar const in C++ has internal linkage by default, which prevents
// weak symbols. The 'extern' keyword forces external linkage.
extern __attribute__((weak)) const bool  isRelease    = false;
extern __attribute__((weak)) const int   versionMajor = 4;
extern __attribute__((weak)) const int   versionMinor = 15;
extern __attribute__((weak)) const int   versionPatch = 0;

}  // namespace BuildInfo
