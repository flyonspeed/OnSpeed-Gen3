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

# OpenAL discovery: macOS provides it as a system framework, others as a
# package.
if(APPLE)
    find_library(OPENAL_LIBRARY OpenAL REQUIRED)
else()
    find_package(OpenAL REQUIRED)
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
    target_include_directories(AOA-Tone-FlyOnSpeed PRIVATE ${OPENAL_INCLUDE_DIR})
endif()

target_link_libraries(AOA-Tone-FlyOnSpeed PRIVATE onspeed_core onspeed_version)

# Match the firmware's strict warning posture so plugin-side drift
# can't silently land. SDK headers are vendored upstream and trip a
# few format/format-nonliteral diagnostics; downgrade those rather
# than scattering pragmas.
target_compile_options(AOA-Tone-FlyOnSpeed PRIVATE
    -Wall -Wextra -Werror
    # Upstream aoa_audio.cpp wasn't written under -Werror. Downgrade
    # the diagnostics it currently trips so this PR keeps to its
    # "preserve behavior" promise; PR 5 (the math swap) is the right
    # place to re-tighten as the function signatures get touched.
    -Wno-error=format-nonliteral
    -Wno-error=unused-parameter
    -Wno-error=unused-variable
    -Wno-error=missing-field-initializers
)

# Per-OS output directory and .xpl naming. X-Plane expects the plugin
# bundle/shared-lib to live in <Plugin>/<arch>/<plugin>.xpl.
if(WIN32)
    set_target_properties(AOA-Tone-FlyOnSpeed PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/win_x64"
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
    target_link_libraries(AOA-Tone-FlyOnSpeed PRIVATE
        "${XPLANE_SDK_LIBS}/XPLM_64.lib"
        "${XPLANE_SDK_LIBS}/XPWidgets_64.lib"
        ${OPENAL_LIBRARY}
    )
else()
    target_link_libraries(AOA-Tone-FlyOnSpeed PRIVATE
        "${XPLANE_SDK_LIBS}/XPLM_64.so"
        "${XPLANE_SDK_LIBS}/XPWidgets_64.so"
        openal
    )
endif()
