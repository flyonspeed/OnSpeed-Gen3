cmake_minimum_required(VERSION 3.15)
project(AOA-Tone-FlyOnSpeed C CXX)

# OnSpeed X-Plane plugin. Plays AOA-driven tones via OpenAL while X-Plane
# is running. Sources lived in the standalone flyonspeed/OnSpeed-XPlane
# repo before being subtreed in here per #220.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Indexer feature flag is read by the per-OS arch logic below
# (single-arch macOS builds when ON, universal otherwise), so the
# option() declaration must precede it.  Full description and the
# rationale for ON-by-default lives at the dedicated block further
# down — this is just the early declaration.
option(ENABLE_M5_INDEXER "Build the embedded M5 indexer renderer" ON)

# SDK version + platform defines.  XPLM200..XPLM400 unlock the
# corresponding-version SDK API surface (each adds new entry points
# behind #if defined(XPLMxxx)).  We target XPLM400 so we get the full
# SDK 4.x API including XPLMEnableFeature ("XPLM_USE_NATIVE_PATHS"),
# which is required for fopen() to accept paths from XPLMGetSystemPath
# on macOS.
add_compile_definitions(XPLM200=1 XPLM210=1 XPLM300=1 XPLM301=1 XPLM303=1 XPLM400=1)

# Per-OS SDK library directory and platform define expected by the X-Plane SDK.
if(APPLE)
    # SDL2 is built from source as a static library (see the
    # ENABLE_M5_INDEXER block below), so the resulting .xpl is
    # self-contained and works on any Mac without a Homebrew SDL2
    # install.  We can build a universal binary in either
    # configuration since the static SDL2 lib is built for both
    # architectures alongside the rest of the plugin.
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
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
# The option() itself is declared near the top of this file so the
# arch logic can read it; this section keeps the dependency wiring.
# --------------------------------------------------------------------

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
    #
    # We build SDL2 from source as a static library so the resulting
    # .xpl is self-contained.  Pilot-side installs don't need a
    # `brew install sdl2` (or apt-get / vcpkg equivalent); copying
    # the .xpl into place is the only step.  On macOS this also
    # allows universal-binary builds — Homebrew SDL2 is single-arch
    # on most dev installs.
    #
    # Set ONSPEED_SYSTEM_SDL2=ON to opt out and link the system SDL2
    # instead (faster incremental dev builds; produces a non-portable
    # .xpl).
    option(ONSPEED_SYSTEM_SDL2
           "Link system SDL2 instead of building it statically" OFF)

    if(ONSPEED_SYSTEM_SDL2)
        # System-SDL2 path: same finder logic the build used pre-PR.
        # Try CONFIG first (vcpkg, modern installs); fall back to a
        # brute-force find_path/find_library (Homebrew, apt).
        find_package(SDL2 CONFIG QUIET)
        if(SDL2_FOUND AND TARGET SDL2::SDL2)
            list(APPEND PLUGIN_EXTRA_LIBS SDL2::SDL2)
        else()
            find_path(SDL2_INCLUDE_DIR SDL.h
                HINTS /opt/homebrew/include /usr/local/include /usr/include
                PATH_SUFFIXES SDL2)
            find_library(SDL2_LIBRARY NAMES SDL2 SDL2-2.0
                HINTS /opt/homebrew/lib /usr/local/lib /usr/lib)
            if(NOT SDL2_INCLUDE_DIR OR NOT SDL2_LIBRARY)
                message(FATAL_ERROR
                    "ONSPEED_SYSTEM_SDL2=ON but no SDL2 found.  Install via "
                    "`brew install sdl2` (macOS) or `apt install libsdl2-dev` "
                    "(Linux), or unset the option to build SDL2 from source.")
            endif()
            list(APPEND PLUGIN_INCLUDE_DIRS ${SDL2_INCLUDE_DIR})
            list(APPEND PLUGIN_EXTRA_LIBS ${SDL2_LIBRARY})
        endif()
    else()
        # FetchContent path: download SDL2 source at configure time and
        # build it as a static library inside our build tree.  The
        # SDL2-static target is wired into PLUGIN_EXTRA_LIBS along with
        # everything else SDL2 transitively needs (Cocoa, IOKit,
        # CoreVideo, etc. on macOS — SDL2's CMake handles those via
        # interface link libraries).
        include(FetchContent)
        # Pin to a release tag so configure-time isn't a moving target
        # and the resulting .xpl ships with a known SDL2 version.
        # 2.32.10 is the current stable release as of May 2026.
        FetchContent_Declare(SDL2_static
            GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
            GIT_TAG        release-2.32.10
            GIT_SHALLOW    TRUE
        )
        # SDL2's option() declarations gate every subsystem; we set
        # them BEFORE FetchContent_MakeAvailable so they take effect
        # when SDL2's CMakeLists.txt runs.
        #
        # Static build, no shared lib, no test programs, no
        # installable rules.  PIC because the .xpl is a shared object
        # (X-Plane dlopen's it) and any static archive linked into a
        # shared object needs position-independent code on Linux.
        set(SDL_SHARED      OFF CACHE BOOL "" FORCE)
        set(SDL_STATIC      ON  CACHE BOOL "" FORCE)
        set(SDL_STATIC_PIC  ON  CACHE BOOL "" FORCE)
        set(SDL_TEST        OFF CACHE BOOL "" FORCE)
        set(SDL_TESTS       OFF CACHE BOOL "" FORCE)
        set(SDL_INSTALL_TESTS OFF CACHE BOOL "" FORCE)
        # The plugin only calls SDL_GetTicks / SDL_Delay (timer
        # subsystem); the lines below declare every other subsystem as
        # unused.  In SDL2 2.32.10 these flags are documentary rather
        # than load-bearing — SDL2's CMakeLists.txt still compiles the
        # video/audio/render TUs into the static archive and links
        # Cocoa/Carbon/CoreVideo/Foundation unconditionally on macOS.
        # The size win in the shipped `.xpl` comes from the linker:
        # `-Wl,-dead_strip` (mac) and `--gc-sections` (Linux) drop
        # unreferenced code, and the hidden-visibility + per-section
        # compile flags applied to the indexer TUs further down this
        # file are what make dead-strip effective.  The flags stay as a
        # signal of intent — future SDL2 versions may honor them as
        # real size optimizations, and they document the dependency
        # surface for anyone auditing the build.
        set(SDL_AUDIO       OFF CACHE BOOL "" FORCE)
        set(SDL_VIDEO       OFF CACHE BOOL "" FORCE)
        set(SDL_RENDER      OFF CACHE BOOL "" FORCE)
        set(SDL_JOYSTICK    OFF CACHE BOOL "" FORCE)
        set(SDL_HAPTIC      OFF CACHE BOOL "" FORCE)
        set(SDL_HIDAPI      OFF CACHE BOOL "" FORCE)
        set(SDL_POWER       OFF CACHE BOOL "" FORCE)
        set(SDL_SENSOR      OFF CACHE BOOL "" FORCE)
        set(SDL_FILE        OFF CACHE BOOL "" FORCE)
        set(SDL_LOADSO      OFF CACHE BOOL "" FORCE)
        set(SDL_CPUINFO     OFF CACHE BOOL "" FORCE)
        set(SDL_FILESYSTEM  OFF CACHE BOOL "" FORCE)
        set(SDL_LOCALE      OFF CACHE BOOL "" FORCE)
        set(SDL_MISC        OFF CACHE BOOL "" FORCE)

        FetchContent_MakeAvailable(SDL2_static)
        # SDL2 exports the static lib as `SDL2-static`.  M5GFX's
        # source includes use the bare `<SDL.h>` form, which SDL2
        # places under `<src>/include/`; FetchContent's target
        # propagates that include directory automatically.
        list(APPEND PLUGIN_EXTRA_LIBS SDL2-static)
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
        src/m5_indexer/PercentLiftFill.cpp
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
    #
    # Per-function/per-data sections + hidden visibility so the linker
    # can dead-strip everything we don't actually call into from the
    # X-Plane plugin entry points.  The unstripped indexer build is
    # ~54 MB, dominated by M5GFX's CJK font tables and ~30 unused
    # panel drivers.  Hidden visibility means those symbols aren't
    # exported from the .xpl, so -dead_strip / --gc-sections drops
    # the unreferenced sections — bringing the build down to ~660 KB.
    set_source_files_properties(
        ${M5_FW_SOURCES} ${M5G_SOURCES} ${M5U_SOURCES} ${TINYXML_SRC}
        src/m5_indexer/Panel_PluginCanvas.cpp
        src/m5_indexer/IndexerWindow.cpp
        src/m5_indexer/DataRefAdapter.cpp
        src/m5_indexer/PercentLiftFill.cpp
        PROPERTIES
        COMPILE_FLAGS
            "-w -ffunction-sections -fdata-sections -fvisibility=hidden -fvisibility-inlines-hidden"
    )

    # Drop unreferenced sections at link time.  Combined with the
    # hidden-visibility + sectioned compile flags above, this is what
    # actually shrinks the output — without -dead_strip / --gc-sections
    # the static-data sections (font glyph tables, vtables for unused
    # panel classes, etc.) stay in the dylib even though nothing calls
    # them.
    if(APPLE)
        target_link_options(AOA-Tone-FlyOnSpeed PRIVATE "-Wl,-dead_strip")
    else()
        target_link_options(AOA-Tone-FlyOnSpeed PRIVATE "-Wl,--gc-sections")
    endif()
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
    -Wno-error=format-nonliteral
    # -Wold-style-cast is C++-only; gating with a generator expression
    # so the M5GFX C font sources (font_fmt_txt.c, lvgl glyph blobs)
    # don't blow up under -Werror with "-Wold-style-cast not valid for C".
    "$<$<COMPILE_LANGUAGE:CXX>:-Wold-style-cast>"
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
