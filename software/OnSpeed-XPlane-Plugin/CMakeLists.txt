cmake_minimum_required(VERSION 3.10)
project(AOA-Tone-FlyOnSpeed)

# Set C++ standard
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Define platform-specific settings
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64") # Support both Intel and Apple Silicon
    set(XPLANE_SDK_LIBS "${CMAKE_SOURCE_DIR}/SDK/Libraries/Mac")
elseif(WIN32)
    set(XPLANE_SDK_LIBS "${CMAKE_SOURCE_DIR}/SDK/Libraries/Win")
else()
    set(XPLANE_SDK_LIBS "${CMAKE_SOURCE_DIR}/SDK/Libraries/Lin")
endif()

# Define source files
set(SOURCES
    aoa_audio.cpp
)

# Add SDK include directories
include_directories(${CMAKE_SOURCE_DIR})

# Platform-specific settings
if(WIN32)
    # Windows settings
    add_definitions(-DXPLM_64=1 -DIBM=1)
    
    # OpenAL for Windows
    find_package(OpenAL REQUIRED)
    include_directories(${OPENAL_INCLUDE_DIR})
    
elseif(APPLE)
    # macOS settings
    add_definitions(-DXPLM_64=1 -DAPL=1)
    
    # OpenAL is part of the system on macOS
    find_library(OPENAL_FRAMEWORK OpenAL REQUIRED)
    set(OPENAL_LIBRARY ${OPENAL_FRAMEWORK})
    
    # Suppress deprecation warnings for OpenAL on macOS
    add_definitions(-Wno-deprecated-declarations)
    
else()
    # Linux settings
    add_definitions(-DXPLM_64=1 -DLIN=1)
    
    # OpenAL for Linux
    find_package(OpenAL REQUIRED)
    include_directories(${OPENAL_INCLUDE_DIR})
endif()

# Create shared library
add_library(AOA-Tone-FlyOnSpeed SHARED ${SOURCES})

# Link OpenAL
target_link_libraries(AOA-Tone-FlyOnSpeed ${OPENAL_LIBRARY})

# Set output name and suffix based on platform
if(WIN32)
    set_target_properties(AOA-Tone-FlyOnSpeed PROPERTIES 
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/win_x64"
        PREFIX ""
        SUFFIX ".xpl")
elseif(APPLE)
    set_target_properties(AOA-Tone-FlyOnSpeed PROPERTIES 
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/mac_x64"
        BUNDLE TRUE
        BUNDLE_EXTENSION "xpl"
        PREFIX "")
else()
    set_target_properties(AOA-Tone-FlyOnSpeed PROPERTIES 
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lin_x64"
        PREFIX ""
        SUFFIX ".xpl")
endif()

# Include directories
target_include_directories(AOA-Tone-FlyOnSpeed PRIVATE
    ${CMAKE_SOURCE_DIR}/SDK/CHeaders
    ${CMAKE_SOURCE_DIR}/SDK/CHeaders/XPLM
    ${CMAKE_SOURCE_DIR}/SDK/CHeaders/Widgets
)

# Link X-Plane SDK libraries
if(APPLE)
    target_link_libraries(AOA-Tone-FlyOnSpeed
        "${XPLANE_SDK_LIBS}/XPLM.framework"
        "${XPLANE_SDK_LIBS}/XPWidgets.framework"
        "-framework OpenAL"
    )
elseif(WIN32)
    target_link_libraries(AOA-Tone-FlyOnSpeed
        "${XPLANE_SDK_LIBS}/XPLM_64.lib"
        "${XPLANE_SDK_LIBS}/XPWidgets_64.lib"
        ${OPENAL_LIBRARY}
    )
else()
    target_link_libraries(AOA-Tone-FlyOnSpeed
        "${XPLANE_SDK_LIBS}/XPLM_64.so"
        "${XPLANE_SDK_LIBS}/XPWidgets_64.so"
        "openal"
    )
endif() 