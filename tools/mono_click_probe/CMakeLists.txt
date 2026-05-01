cmake_minimum_required(VERSION 3.15)

project(mono_click_probe LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(libOPNMIDI_STATIC ON CACHE BOOL "" FORCE)
set(libOPNMIDI_SHARED OFF CACHE BOOL "" FORCE)
set(WITH_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(WITH_MIDIPLAY OFF CACHE BOOL "" FORCE)
set(WITH_VLC_PLUGIN OFF CACHE BOOL "" FORCE)
set(WITH_DAC_UTIL OFF CACHE BOOL "" FORCE)

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/libOPNMIDI"
                 "${CMAKE_CURRENT_BINARY_DIR}/libOPNMIDI")

add_executable(mono_click_probe mono_click_probe.cpp)
target_link_libraries(mono_click_probe PRIVATE OPNMIDI_static)

enable_testing()
add_test(
    NAME mono_click_probe_smoke
    COMMAND mono_click_probe
            "${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/libOPNMIDI/fm_banks/gs-by-papiezak-and-sneakernets.wopn"
            "${CMAKE_CURRENT_SOURCE_DIR}/temp_midi_bass_excerpt.tsv"
            "${CMAKE_CURRENT_BINARY_DIR}/mono_click_probe_smoke.wav"
            0.5
            mono
)
