cmake_minimum_required(VERSION 3.15)
project(simconnect-spike LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/SDK")

if(NOT EXISTS "${SDK_DIR}/SimConnect.h")
    message(FATAL_ERROR
        "SimConnect SDK not found at ${SDK_DIR}/SimConnect.h. "
        "Copy SimConnect.h and lib/SimConnect.lib from your P3D install "
        "into ${SDK_DIR}. See tools/simconnect-spike/README.md.")
endif()

add_executable(simconnect-spike src/main.cpp)
target_include_directories(simconnect-spike PRIVATE "${SDK_DIR}")
target_link_libraries(simconnect-spike PRIVATE
    "${SDK_DIR}/lib/SimConnect.lib")
target_compile_options(simconnect-spike PRIVATE
    -Wall -Wextra -Wno-unknown-pragmas)

if(WIN32 AND EXISTS "${SDK_DIR}/lib/SimConnect.dll")
    add_custom_command(TARGET simconnect-spike POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SDK_DIR}/lib/SimConnect.dll"
        "$<TARGET_FILE_DIR:simconnect-spike>/SimConnect.dll")
endif()
