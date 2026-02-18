"""Pre-build script: override src_dir for the hardware_test environment.

PlatformIO's src_dir is a [platformio]-level setting and cannot be overridden
per-env.  This script changes PROJECT_SRC_DIR so only the hardware test sketch
is compiled.
"""

Import("env")

import os

hwtest_dir = os.path.join(env.subst("$PROJECT_DIR"), "software", "OnSpeed-hardware-test")
env.Replace(PROJECT_SRC_DIR=hwtest_dir)
