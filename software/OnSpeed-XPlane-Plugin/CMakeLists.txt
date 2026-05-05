cmake_minimum_required(VERSION 3.15)
project(AOA-Tone-FlyOnSpeed C CXX)

# OnSpeed X-Plane plugin. Plays AOA-driven tones via OpenAL while X-Plane
# is running. Sources lived in the standalone flyonspeed/OnSpeed-XPlane
# repo before being subtreed in here per #220.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# SDK version + platform defines.  XPLM200..XPLM400 unlock the
# corresponding-version SDK API surface (each adds new entry points
# behind #if defined(XPLMxxx)).  We target XPLM400 so we get the full
# SDK 4.x API including XPLMEnableFeature ("XPLM_USE_NATIVE_PATHS"),
# which is required for fopen() to accept paths from XPLMGetSystemPath
# on macOS.
add_compile_definitions(XPLM200=1 XPLM210=1 XPLM300=1 XPLM301=1 XPLM303=1 XPLM400=1)

# Per-OS SDK library directory and platform define expected by the X-Plane SDK.
if(APPLE)
    if(ENABLE_M5_INDEXER)
        # Indexer build links SDL2 from Homebrew which is single-arch
        # on most dev installs.  Force native arch only.  Universal
        # build comes back when we statically link a fat SDL2 build.
        set(CMAKE_OSX_ARCHITECTURES "arm64")
    else()
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
    endif()
    set(XPLANE_SDK_LIBS "${CMAKE_CURRENT_SOURCE_DIR}/SDK/Libraries/Mac")
    add_compile_definitions(XPLM_64=1 APL=1)
    add_compile_options(-Wno-deprecated-declarations)  # OpenAL on macOS
elseif(WIN32)
    set(XPLANE_SDK_LIBS "${CMAKE_CURRENT_SOURCE_DIR}/SDK/Libraries/Win")
    add_compile_definitions(XPLM_64=1 IBM=1)
else()
    set(XPLANE_SDK_LIBS "${CMAKE_CURRENT_SOURCE_DIR}/SDK/Libraries/Lin")
    add_compile_definitions(XPLM_64=1 LIN=1)
endif()

# OpenAL discovery: macOS uses the system framework via find_library();
# Linux/Windows use find_package().
#
# On Linux, CMake's bundled FindOpenAL.cmake (MODULE mode) sets
# OPENAL_INCLUDE_DIR / OPENAL_LIBRARY from the system package. On
# Windows, vcpkg ships an OpenALConfig.cmake that exports a proper
# OpenAL::OpenAL target with include dirs wired in — CONFIG mode
# picks that up. We try CONFIG first, then fall back to MODULE, and
# synthesize OpenAL::OpenAL when only the module's loose variables
# are present (CMake < 3.25 doesn't create the target itself).
if(APPLE)
    find_library(OPENAL_LIBRARY OpenAL REQUIRED)
else()
    find_package(OpenAL CONFIG QUIET)
    if(NOT OpenAL_FOUND)
        find_package(OpenAL MODULE REQUIRED)
    endif()
    if(NOT TARGET OpenAL::OpenAL)
        add_library(OpenAL::OpenAL UNKNOWN IMPORTED)
        set_target_properties(OpenAL::OpenAL PROPERTIES
            IMPORTED_LOCATION "${OPENAL_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${OPENAL_INCLUDE_DIR}")
    endif()
endif()

# Link against onspeed_core via its CMake shim. PIO consumers continue
# to use library.json untouched; this is purely the CMake-side entry.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Libraries/onspeed_core
                 ${CMAKE_BINARY_DIR}/onspeed_core)

# Regenerate buildinfo.cpp from current git state so the plugin reports
# the same version string as the firmware/M5 builds. Same script, same
# tag-derived version. Run at configure time so the generated .cpp
# exists when add_subdirectory(version) decides which sources to compile.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
execute_process(
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/generate_buildinfo.py
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../..
    RESULT_VARIABLE BUILDINFO_RC
)
if(NOT BUILDINFO_RC EQUAL 0)
    message(WARNING
        "generate_buildinfo.py failed (rc=${BUILDINFO_RC}); "
        "plugin will report fallback version from buildinfo_default.cpp")
endif()

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Libraries/version
                 ${CMAKE_BINARY_DIR}/version)

# --------------------------------------------------------------------
# Optional M5 indexer feature.
#
# Linking the M5 firmware + M5GFX + M5Unified + SDL2 produces an
# in-process indexer renderer that draws into an X-Plane window.  The
# audio path doesn't need any of this; the option is provided so an
# audio-only build remains possible (e.g., if SDL2 or the M5 libdeps
# are unavailable on a host).
#
# Enabled by default — release artifacts ship with the indexer.
# Disable with -DENABLE_M5_INDEXER=OFF for an audio-only build.
# --------------------------------------------------------------------
option(ENABLE_M5_INDEXER "Build the embedded M5 indexer renderer" ON)

set(PLUGIN_SOURCES
    src/aoa_audio.cpp
    src/serial_port.cpp
)
set(PLUGIN_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/SDK/CHeaders/XPLM
    ${CMAKE_CURRENT_SOURCE_DIR}/SDK/CHeaders/Widgets
)
set(PLUGIN_EXTRA_LIBS "")

if(ENABLE_M5_INDEXER)
    # SDL2: lgfx/v1/platforms/sdl/common.cpp gates millis/micros/delay
    # on `#if defined(SDL_h_)`, which the M5 firmware calls every loop.
    # Even though we don't open an SDL window (we use a custom panel),
    # SDL2 must be linked so those time helpers compile in.
    # SDL2: try config first (works for vcpkg + most modern installs);
    # fall back to module mode (works for Homebrew + apt).  On macOS
    # without a system SDL2.framework, Homebrew installs a regular
    # dylib + headers under /opt/homebrew/include/SDL2/ — module mode
    # finds those via SDL2_INCLUDE_DIR + SDL2_LIBRARY.
    find_package(SDL2 CONFIG QUIET)
    if(SDL2_FOUND AND TARGET SDL2::SDL2)
        list(APPEND PLUGIN_EXTRA_LIBS SDL2::SDL2)
    else()
        # Module-mode fallback.  On macOS Homebrew, SDL2 lives at
        # ${brew_prefix}/opt/sdl2; CMake's find_package SDL2 in MODULE
        # mode (via FindSDL2.cmake — usually present, sometimes not)
        # may need PKG_CONFIG_PATH hints.  Use the brute-force find.
        find_path(SDL2_INCLUDE_DIR SDL.h
            HINTS /opt/homebrew/include /usr/local/include /usr/include
            PATH_SUFFIXES SDL2)
        find_library(SDL2_LIBRARY NAMES SDL2 SDL2-2.0
            HINTS /opt/homebrew/lib /usr/local/lib /usr/lib)
        if(NOT SDL2_INCLUDE_DIR OR NOT SDL2_LIBRARY)
            message(FATAL_ERROR
                "ENABLE_M5_INDEXER requires SDL2.  Install via "
                "`brew install sdl2` (macOS) or `apt install libsdl2-dev` (Linux).")
        endif()
        list(APPEND PLUGIN_INCLUDE_DIRS ${SDL2_INCLUDE_DIR})
        list(APPEND PLUGIN_EXTRA_LIBS ${SDL2_LIBRARY})
    endif()

    # M5GFX + M5Unified live under the M5 display project's PlatformIO
    # libdeps tree (built once via `pio run -e native` from
    # software/OnSpeed-M5-Display/).  Reuse that copy rather than
    # re-downloading.
    set(M5_DISPLAY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../OnSpeed-M5-Display)
    set(M5G_DIR ${M5_DISPLAY_DIR}/.pio/libdeps/native/M5GFX)
    set(M5U_DIR ${M5_DISPLAY_DIR}/.pio/libdeps/native/M5Unified)
    if(NOT EXISTS ${M5G_DIR} OR NOT EXISTS ${M5U_DIR})
        message(FATAL_ERROR
            "ENABLE_M5_INDEXER requires M5GFX/M5Unified at "
            "${M5_DISPLAY_DIR}/.pio/libdeps/native/.  Run "
            "'pio run -e native' from ${M5_DISPLAY_DIR} first.")
    endif()

    # Glob M5GFX sources, excluding ESP32-specific platforms.  Exclusion
    # list mirrors software/OnSpeed-M5-Display/sim/build_wasm.sh.
    file(GLOB_RECURSE M5G_LGFX_CXX
        ${M5G_DIR}/src/lgfx/v1/*.cpp)
    file(GLOB_RECURSE M5G_LGFX_C
        ${M5G_DIR}/src/lgfx/v1/*.c)
    file(GLOB_RECURSE M5G_FONTS_CXX
        ${M5G_DIR}/src/lgfx/Fonts/*.cpp)
    file(GLOB_RECURSE M5G_FONTS_C
        ${M5G_DIR}/src/lgfx/Fonts/*.c)
    file(GLOB_RECURSE M5G_UTIL_CXX
        ${M5G_DIR}/src/lgfx/utility/*.cpp)
    file(GLOB_RECURSE M5G_UTIL_C
        ${M5G_DIR}/src/lgfx/utility/*.c)
    # lv_font subtree contains the LVGL-style font blobs that
    # lgfx_fonts.cpp references via extern declarations.  Without
    # these the link fails with "undefined symbol _lv_font_*".
    file(GLOB_RECURSE M5G_LVFONT_C
        ${M5G_DIR}/src/lgfx/v1/lv_font/*.c)
    set(M5G_EXCLUDE_PATTERNS
        "/platforms/esp32/" "/platforms/samd"
        "/platforms/arduino_default/" "/platforms/opencv/"
        "/platforms/framebuffer/" "/platforms/rp2040/"
        "/platforms/stm32/" "/platforms/spresense/"
        "/platforms/wiced/" "/platforms/ra6m5/")
    foreach(_pat ${M5G_EXCLUDE_PATTERNS})
        list(FILTER M5G_LGFX_CXX  EXCLUDE REGEX "${_pat}")
        list(FILTER M5G_LGFX_C    EXCLUDE REGEX "${_pat}")
    endforeach()

    list(LENGTH M5G_LGFX_CXX  _cnt_lgfx_cxx)
    list(LENGTH M5G_LGFX_C    _cnt_lgfx_c)
    list(LENGTH M5G_FONTS_CXX _cnt_fonts_cxx)
    list(LENGTH M5G_FONTS_C   _cnt_fonts_c)
    list(LENGTH M5G_UTIL_CXX  _cnt_util_cxx)
    list(LENGTH M5G_UTIL_C    _cnt_util_c)
    list(LENGTH M5G_LVFONT_C  _cnt_lvfont_c)
    message(STATUS "M5GFX globs: lgfx=${_cnt_lgfx_cxx}cxx+${_cnt_lgfx_c}c "
                   "fonts=${_cnt_fonts_cxx}cxx+${_cnt_fonts_c}c "
                   "util=${_cnt_util_cxx}cxx+${_cnt_util_c}c "
                   "lvfont=${_cnt_lvfont_c}c")

    set(M5G_SOURCES
        ${M5G_DIR}/src/M5GFX.cpp
        ${M5G_LGFX_CXX} ${M5G_LGFX_C}
        ${M5G_FONTS_CXX} ${M5G_FONTS_C}
        ${M5G_UTIL_CXX} ${M5G_UTIL_C}
        ${M5G_LVFONT_C}
    )

    file(GLOB_RECURSE M5U_UTIL_CXX ${M5U_DIR}/src/utility/*.cpp)
    set(M5U_SOURCES
        ${M5U_DIR}/src/M5Unified.cpp
        ${M5U_UTIL_CXX}
    )

    # M5 firmware sources we link in to drive the renderer.
    set(M5_FW_SOURCES
        ${M5_DISPLAY_DIR}/src/main.cpp
        ${M5_DISPLAY_DIR}/src/SerialRead.cpp
        ${M5_DISPLAY_DIR}/lib/GaugeWidgets/GaugeWidgets.cpp
    )

    # tinyxml2 — onspeed_core's proto/LogCsv path transitively pulls
    # this in.  Include the .cpp directly so the symbols are available
    # at link time.
    set(TINYXML_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../Libraries/tinyxml2/tinyxml2.cpp)

    list(APPEND PLUGIN_SOURCES
        src/m5_indexer/Panel_PluginCanvas.cpp
        src/m5_indexer/IndexerWindow.cpp
        src/m5_indexer/DataRefAdapter.cpp
        ${M5_FW_SOURCES}
        ${M5G_SOURCES}
        ${M5U_SOURCES}
        ${TINYXML_SRC}
    )
    list(APPEND PLUGIN_INCLUDE_DIRS
        ${M5_DISPLAY_DIR}/include
        ${M5_DISPLAY_DIR}/src
        ${M5_DISPLAY_DIR}/sim
        ${M5_DISPLAY_DIR}/lib/GaugeWidgets
        ${M5G_DIR}/src
        ${M5U_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/../Libraries/tinyxml2
        ${CMAKE_CURRENT_SOURCE_DIR}/../Libraries/onspeed_core/src
    )
endif()

add_library(AOA-Tone-FlyOnSpeed SHARED ${PLUGIN_SOURCES})

target_include_directories(AOA-Tone-FlyOnSpeed PRIVATE ${PLUGIN_INCLUDE_DIRS})

if(ENABLE_M5_INDEXER)
    target_compile_definitions(AOA-Tone-FlyOnSpeed PRIVATE
        XPLANE_PLUGIN_BUILD=1
        ENABLE_M5_INDEXER=1
        M5GFX_BACK_COLOR=0x000000u
        M5GFX_SCALE=1
        # 16bpp = RGB565, matching our Panel_PluginCanvas's framebuffer.
        # Without this, main.cpp's setColorDepth(8) would invalidate the
        # panel's row-pointer table on every loop().
        XPLANE_PLUGIN_DEPTH=16
    )
    # The M5 firmware sources have casts and shadows that don't pass
    # our strict warning posture.  Downgrade -Werror for those TUs and
    # for our own indexer TUs (which transitively include M5GFX headers
    # that use C-style casts).
    set_source_files_properties(
        ${M5_FW_SOURCES} ${M5G_SOURCES} ${M5U_SOURCES} ${TINYXML_SRC}
        src/m5_indexer/Panel_PluginCanvas.cpp
        src/m5_indexer/IndexerWindow.cpp
        src/m5_indexer/DataRefAdapter.cpp
        PROPERTIES
        COMPILE_FLAGS "-w"
    )
endif()

if(NOT APPLE)
    target_link_libraries(AOA-Tone-FlyOnSpeed PRIVATE OpenAL::OpenAL)
endif()

target_link_libraries(AOA-Tone-FlyOnSpeed PRIVATE
    onspeed_core onspeed_version ${PLUGIN_EXTRA_LIBS})

if(APPLE AND ENABLE_M5_INDEXER)
    target_link_libraries(AOA-Tone-FlyOnSpeed PRIVATE
        "-framework OpenGL")
endif()

# Strict warning posture matching the M5 display firmware's. Plugin
# code is clean under all of these now that the move-into-onspeed_core
# refactor has touched the function signatures that previously tripped
# unused-parameter / unused-variable. -Wno-error=format-nonliteral is
# the only downgrade — vendored SDK headers and standard library
# headers occasionally use computed format strings that we can't fix
# upstream.
target_compile_options(AOA-Tone-FlyOnSpeed PRIVATE
    -Wall
    -Wextra
    -Werror
    -Wshadow
    -Wformat=2
    -Wunreachable-code
    -Wnull-dereference
    -Wold-style-cast       # plugin's only consumer of C, prefer C++ casts
    -Wno-error=format-nonliteral
)

# Plugin-side characterization tests (host-side, no X-Plane needed).
# Pin the boundary outcomes of onspeed::calculateTone() with the
# plugin's default thresholds. Built with `cmake --build build`,
# run with `ctest --test-dir build`.
enable_testing()
add_subdirectory(tests)

# Per-OS output directory and .xpl naming. X-Plane expects the plugin
# bundle/shared-lib to live in <Plugin>/<arch>/<plugin>.xpl.
if(WIN32)
    # On Windows a SHARED library is a DLL — CMake routes it to
    # RUNTIME_OUTPUT_DIRECTORY (not LIBRARY_OUTPUT_DIRECTORY, which
    # only covers the import .dll.a on MinGW). Set both so the .xpl
    # lands in win_x64/ regardless of which property wins.
    set_target_properties(AOA-Tone-FlyOnSpeed PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/win_x64"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/win_x64"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/win_x64"
        PREFIX ""
        SUFFIX ".xpl")
elseif(APPLE)
    # X-Plane on macOS accepts a bare dylib renamed to .xpl. Easier to do
    # here than to ship a true .bundle; keeps the per-OS layout symmetric.
    set_target_properties(AOA-Tone-FlyOnSpeed PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/mac_x64"
        PREFIX ""
        SUFFIX ".xpl")
else()
    set_target_properties(AOA-Tone-FlyOnSpeed PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lin_x64"
        PREFIX ""
        SUFFIX ".xpl")
endif()

# Link X-Plane SDK + OpenAL.
if(APPLE)
    target_link_libraries(AOA-Tone-FlyOnSpeed PRIVATE
        "${XPLANE_SDK_LIBS}/XPLM.framework"
        "${XPLANE_SDK_LIBS}/XPWidgets.framework"
        ${OPENAL_LIBRARY}
    )
elseif(WIN32)
    # Windows uses MinGW-w64 + vcpkg's x64-mingw-static triplet so the
    # resulting .xpl has zero non-OS DLL dependencies. That means: tell
    # OpenAL's headers we're linking statically, pull in the Win32 audio
    # backends OpenAL Soft calls into (winmm / ole32 / dsound / uuid),
    # and ask the linker to bake libgcc + libstdc++ into the plugin so
    # users don't need the MinGW runtime DLLs on PATH either. OpenAL
    # itself is already linked via the OpenAL::OpenAL imported target
    # above; here we just add the Win32 backends it depends on.
    target_compile_definitions(AOA-Tone-FlyOnSpeed PRIVATE AL_LIBTYPE_STATIC)
    target_link_libraries(AOA-Tone-FlyOnSpeed PRIVATE
        "${XPLANE_SDK_LIBS}/XPLM_64.lib"
        "${XPLANE_SDK_LIBS}/XPWidgets_64.lib"
        winmm
        ole32
        dsound
        uuid
    )
    # `-static` promotes every remaining -l<name> to static-linkage, which
    # also grabs winpthread (std::thread/std::mutex's runtime) into the
    # binary. Without it the plugin would still depend on
    # libwinpthread-1.dll at load time — breaking the single-file install
    # promise. Keep this AFTER the other link options.
    #
    # License note: libgcc + libstdc++ are covered by the GCC Runtime
    # Library Exception, which explicitly permits static linking into
    # non-GPL binaries. OpenAL Soft is LGPL — static linking follows
    # long-established X-Plane plugin convention; relinking against a
    # user-supplied OpenAL Soft is possible by swapping the .xpl.
    target_link_options(AOA-Tone-FlyOnSpeed PRIVATE
        -static-libgcc
        -static-libstdc++
        -static
    )
else()
    target_link_libraries(AOA-Tone-FlyOnSpeed PRIVATE
        "${XPLANE_SDK_LIBS}/XPLM_64.so"
        "${XPLANE_SDK_LIBS}/XPWidgets_64.so"
    )
endif()
