cmake_minimum_required(VERSION 3.15)
project(tinyxml2 CXX)

# Vendored tinyxml2. Upstream: https://github.com/leethomason/tinyxml2
# This shim exists so CMake consumers (the X-Plane plugin) can link
# against the same source the firmware uses via PlatformIO's library.json.

add_library(tinyxml2 STATIC tinyxml2.cpp)
target_include_directories(tinyxml2 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(tinyxml2 PUBLIC cxx_std_17)
