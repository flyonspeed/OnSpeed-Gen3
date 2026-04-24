cmake_minimum_required(VERSION 3.15)
project(onspeed_xplane_plugin_tests CXX)

# Plugin-side characterization tests. The plugin's audio decisions go
# through onspeed_core::calculateTone(); these tests pin the
# decision boundaries for the plugin's default thresholds, so a
# future onspeed_core change that would silently shift sim tone
# behavior fails CI here.
#
# We don't test calculateTone() itself — that's covered exhaustively
# in software/Libraries/onspeed_core/test/test_tone_calc/. We test
# the plugin's boundary contract.

enable_testing()

# Pull in onspeed_core as a sub-project (parallel to the plugin's
# own use). Same shim, same source.
if(NOT TARGET onspeed_core)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../Libraries/onspeed_core
                     ${CMAKE_BINARY_DIR}/onspeed_core)
endif()

add_executable(tone_characterization tone_characterization.cpp)
target_link_libraries(tone_characterization PRIVATE onspeed_core)
target_compile_features(tone_characterization PRIVATE cxx_std_17)
target_compile_options(tone_characterization PRIVATE
    -Wall -Wextra -Werror -Wshadow -Wformat=2)

add_test(NAME tone_characterization COMMAND tone_characterization)
