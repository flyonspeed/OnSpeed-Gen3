"""Pre-build hook: override PROJECT_SRC_DIR for the hardware_test env.

PlatformIO's `src_dir` is a [platformio]-level setting and cannot be
overridden per-env. This script redirects PROJECT_SRC_DIR so the
hardware-test build compiles only the standalone .ino sketch in
software/OnSpeed-hardware-test/, not the full firmware tree under
software/OnSpeed-Gen3-ESP32/.

Pattern: identical to other PlatformIO projects that need per-env src
selection. Note that this also changes the include search root, so the
sketch's "#include" paths are relative to the hardware-test folder, not
the firmware sketch folder. We add the firmware folder to the include
path explicitly via build_flags in platformio.ini so the sketch can
include "HardwareMap.h" directly.
"""

Import("env")  # noqa: F821 (provided by SCons)

import os

hwtest_dir = os.path.join(env.subst("$PROJECT_DIR"), "software", "OnSpeed-hardware-test")
env.Replace(PROJECT_SRC_DIR=hwtest_dir)
