/*
 * Build information defaults — weak symbols overridden by buildinfo.cpp.
 *
 * When PlatformIO or CMake builds, scripts/generate_buildinfo.py creates
 * buildinfo.cpp with strong (non-weak) symbols that override these.
 * These weak defaults are the fallback when the generator hasn't run
 * — typically Arduino IDE builds, or a CMake configure that couldn't
 * shell out to git.
 *
 * The fallback is deliberately "0.0.0-fallback" rather than a real-looking
 * version number. A binary that reports this string is one whose build
 * tooling didn't include the version-generation step, and we'd rather
 * surface that than silently look like an old release.
 */

#include "buildinfo.h"

namespace BuildInfo {

// char* pointers: const qualifies the pointed-to data, not the pointer,
// so they already have external linkage and weak works directly.
__attribute__((weak)) const char* version      = "0.0.0-fallback";
__attribute__((weak)) const char* gitSha       = "unknown";
__attribute__((weak)) const char* gitShortSha  = "unknown";
__attribute__((weak)) const char* gitBranch    = "unknown";
__attribute__((weak)) const char* buildDate    = "unknown";

// Scalar const in C++ has internal linkage by default, which prevents
// weak symbols. The 'extern' keyword forces external linkage.
extern __attribute__((weak)) const bool  isRelease    = false;
extern __attribute__((weak)) const int   versionMajor = 0;
extern __attribute__((weak)) const int   versionMinor = 0;
extern __attribute__((weak)) const int   versionPatch = 0;

}  // namespace BuildInfo
