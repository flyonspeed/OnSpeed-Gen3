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

# band_edge_iasvalid — pins the band-edge / pip iasValid contract.
# Compiles PercentLiftFill.cpp directly (no XPLM linkage needed) so
# the test exercises production code.
add_executable(band_edge_iasvalid
    band_edge_iasvalid.cpp
    ../src/m5_indexer/PercentLiftFill.cpp)
target_link_libraries(band_edge_iasvalid PRIVATE onspeed_core)
target_compile_features(band_edge_iasvalid PRIVATE cxx_std_17)
target_compile_options(band_edge_iasvalid PRIVATE
    -Wall -Wextra -Werror -Wshadow -Wformat=2)
target_include_directories(band_edge_iasvalid PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src)

add_test(NAME band_edge_iasvalid COMMAND band_edge_iasvalid)

# iasground_pct — pins the on-ground V² percent-lift formula.
# Same compile pattern as band_edge_iasvalid: links PercentLiftFill.cpp
# directly so the test exercises production code without XPLM stubs.
add_executable(iasground_pct
    iasground_pct.cpp
    ../src/m5_indexer/PercentLiftFill.cpp)
target_link_libraries(iasground_pct PRIVATE onspeed_core)
target_compile_features(iasground_pct PRIVATE cxx_std_17)
target_compile_options(iasground_pct PRIVATE
    -Wall -Wextra -Werror -Wshadow -Wformat=2)
target_include_directories(iasground_pct PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src)

add_test(NAME iasground_pct COMMAND iasground_pct)

# legacy_json_parse — pins the hand-rolled JSON parser used by the
# legacy json-config import path in aoa_audio.cpp::TryImportLegacyJson.
# The test mirrors the function under test (it's static in
# aoa_audio.cpp); see the comment block at the top of the test file
# for why we duplicate rather than expose.
add_executable(legacy_json_parse legacy_json_parse.cpp)
target_compile_features(legacy_json_parse PRIVATE cxx_std_17)
target_compile_options(legacy_json_parse PRIVATE
    -Wall -Wextra -Werror -Wshadow -Wformat=2)

add_test(NAME legacy_json_parse COMMAND legacy_json_parse)

# auto_setpoints — pins the per-frame derivation in auto mode that
# turns X-Plane's stall-AOA datarefs into the four AOA setpoints.
# Compiles AutoSetpoints.cpp directly (no XPLM, no onspeed_core).
add_executable(auto_setpoints
    auto_setpoints.cpp
    ../src/m5_indexer/AutoSetpoints.cpp)
target_compile_features(auto_setpoints PRIVATE cxx_std_17)
target_compile_options(auto_setpoints PRIVATE
    -Wall -Wextra -Werror -Wshadow -Wformat=2)
target_include_directories(auto_setpoints PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src)

add_test(NAME auto_setpoints COMMAND auto_setpoints)
