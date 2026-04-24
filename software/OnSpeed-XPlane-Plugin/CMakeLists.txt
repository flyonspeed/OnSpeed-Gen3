cmake_minimum_required(VERSION 3.15)
project(AOA-Tone-FlyOnSpeed CXX)

# OnSpeed X-Plane plugin. Plays AOA-driven tones via OpenAL while X-Plane
# is running. Sources lived in the standalone flyonspeed/OnSpeed-XPlane
# repo before being subtreed in here per #220.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Per-OS SDK library directory and platform define expected by the X-Plane SDK.
if(APPLE)
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

add_library(AOA-Tone-FlyOnSpeed SHARED src/aoa_audio.cpp)

target_include_directories(AOA-Tone-FlyOnSpeed PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/SDK/CHeaders/XPLM
    ${CMAKE_CURRENT_SOURCE_DIR}/SDK/CHeaders/Widgets
)
if(NOT APPLE)
    target_link_libraries(AOA-Tone-FlyOnSpeed PRIVATE OpenAL::OpenAL)
endif()

target_link_libraries(AOA-Tone-FlyOnSpeed PRIVATE onspeed_core onspeed_version)

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
