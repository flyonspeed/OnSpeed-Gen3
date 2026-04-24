cmake_minimum_required(VERSION 3.15)
project(onspeed_version CXX)

# OnSpeed BuildInfo — gives every consumer (firmware, M5, X-Plane plugin)
# the same version/git-sha symbols sourced from git tags via
# scripts/generate_buildinfo.py.
#
# Pattern matches the PlatformIO setup:
#   buildinfo_default.cpp - tracked, weak symbols, fallback values
#   buildinfo.cpp         - gitignored, strong symbols, regenerated each build
#
# CMake consumers must arrange to run generate_buildinfo.py before this
# target is compiled (see the X-Plane plugin's CMakeLists for an example
# using execute_process at configure time).

set(BUILDINFO_GENERATED ${CMAKE_CURRENT_SOURCE_DIR}/buildinfo.cpp)

# If the generator hasn't run yet, compile only the weak defaults so the
# library always has all four symbol definitions. The first cmake configure
# in the parent project should call generate_buildinfo.py.
# OBJECT library (not STATIC) so both translation units always get linked
# into the final binary. With STATIC, the linker only pulls in .o files
# whose symbols are referenced — the weak default from buildinfo_default.cpp
# satisfies the references first, the generated strong-symbol .o never
# enters the link, and the plugin reports "4.15.0" (the weak fallback)
# instead of the real git-derived version.
if(EXISTS ${BUILDINFO_GENERATED})
    add_library(onspeed_version OBJECT
        buildinfo_default.cpp
        ${BUILDINFO_GENERATED})
else()
    message(STATUS
        "onspeed_version: buildinfo.cpp not present, using weak defaults only. "
        "Run scripts/generate_buildinfo.py to embed real git metadata.")
    add_library(onspeed_version OBJECT buildinfo_default.cpp)
endif()

target_include_directories(onspeed_version PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(onspeed_version PUBLIC cxx_std_17)
