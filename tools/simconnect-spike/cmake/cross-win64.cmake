# MinGW-w64 cross-compile toolchain for the SimConnect spike.
#
# Install on macOS:  brew install mingw-w64
# Install on Linux:  apt-get install mingw-w64

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(TOOLCHAIN_PREFIX x86_64-w64-mingw32)

set(CMAKE_C_COMPILER   ${TOOLCHAIN_PREFIX}-gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}-g++)
set(CMAKE_RC_COMPILER  ${TOOLCHAIN_PREFIX}-windres)

set(CMAKE_FIND_ROOT_PATH /usr/local/${TOOLCHAIN_PREFIX} /opt/homebrew/${TOOLCHAIN_PREFIX})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Static-link the MinGW C++ runtime so the resulting exe doesn't drag
# libstdc++-6.dll / libgcc_s_seh-1.dll requirements onto the target machine.
set(CMAKE_EXE_LINKER_FLAGS "-static -static-libgcc -static-libstdc++")
