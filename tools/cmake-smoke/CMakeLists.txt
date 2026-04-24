cmake_minimum_required(VERSION 3.15)
project(onspeed_core_cmake_smoke CXX)

# Smallest possible CMake consumer of onspeed_core. Verifies the shim at
# software/Libraries/onspeed_core/CMakeLists.txt links and exposes the
# expected public include path. Run from the repo root:
#
#   cmake -S tools/cmake-smoke -B tools/cmake-smoke/build
#   cmake --build tools/cmake-smoke/build
#   ./tools/cmake-smoke/build/onspeed_core_smoke

add_subdirectory(${CMAKE_SOURCE_DIR}/../../software/Libraries/onspeed_core
                 ${CMAKE_BINARY_DIR}/onspeed_core)

add_executable(onspeed_core_smoke smoke.cpp)
target_link_libraries(onspeed_core_smoke PRIVATE onspeed_core)
