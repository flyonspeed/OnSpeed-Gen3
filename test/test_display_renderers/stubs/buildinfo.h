// buildinfo.h (host stub) — the real one is auto-generated from git tags
// by scripts/generate_buildinfo.py and lives in lib/version/. For host
// render tests we pin a stable value so the splash-screen hash does not
// drift with every commit.

#ifndef RENDERTEST_BUILDINFO_H
#define RENDERTEST_BUILDINFO_H

namespace BuildInfo {
    inline constexpr const char* version = "test-build";
}

#endif // RENDERTEST_BUILDINFO_H
