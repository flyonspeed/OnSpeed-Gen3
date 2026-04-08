/*
 * Build information header
 *
 * This file provides version and build metadata for the OnSpeed firmware.
 * The values in buildinfo.cpp are auto-generated at build time by
 * scripts/generate_buildinfo.py, which extracts version info from git tags.
 *
 * Usage:
 *   #include <buildinfo.h>
 *   Serial.printf("Version: %s\n", BuildInfo::version);
 *   Serial.printf("Git SHA: %s\n", BuildInfo::gitSha);
 */

#ifndef BUILDINFO_H
#define BUILDINFO_H

namespace BuildInfo {

// Semantic version string (e.g., "4.1.0" or "4.1.0-dev")
extern const char* version;

// Full git commit SHA (40 hex chars) or "unknown"
extern const char* gitSha;

// Short git commit SHA (first 7 chars) or "unknown"
extern const char* gitShortSha;

// Git branch name or "unknown"
extern const char* gitBranch;

// Build timestamp in ISO 8601 format
extern const char* buildDate;

// True if built from a tagged release in CI
extern const bool isRelease;

// Major version number
extern const int versionMajor;

// Minor version number
extern const int versionMinor;

// Patch version number
extern const int versionPatch;

}  // namespace BuildInfo

#endif  // BUILDINFO_H
