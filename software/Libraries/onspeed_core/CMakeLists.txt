cmake_minimum_required(VERSION 3.15)
project(onspeed_core CXX)

# Platform-independent OnSpeed algorithms (AHRS, AOA, audio, EFIS, etc).
# This shim is for CMake consumers (the X-Plane plugin). PlatformIO consumers
# (firmware, M5 display) continue to use library.json + library.properties
# untouched.
#
# Purity invariant: nothing under src/ may include a platform header
# (Arduino.h, FreeRTOS.h, etc). Enforced by scripts/check_core_purity.sh.

file(GLOB_RECURSE ONSPEED_CORE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

add_library(onspeed_core STATIC ${ONSPEED_CORE_SOURCES})

# src/ is the public include root: consumers write `#include <audio/ToneCalc.h>`
# matching the firmware's library.json `includeDir: src`.
target_include_directories(onspeed_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# C++17 required for std::optional and inline constexpr variables used in core.
target_compile_features(onspeed_core PUBLIC cxx_std_17)

# Vendored tinyxml2 lives next door; consumed by config/ConfigXml{Emit,Parse}.cpp.
if(NOT TARGET tinyxml2)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../tinyxml2
                     ${CMAKE_BINARY_DIR}/tinyxml2)
endif()
target_link_libraries(onspeed_core PUBLIC tinyxml2)
