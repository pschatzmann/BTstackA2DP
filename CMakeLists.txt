# Desktop (Linux / macOS) build of BlueA2DP: vendored btstack + header-only C++ API.
# The Arduino IDE / arduino-cli / PlatformIO do not use this file.
#
#   cmake -S . -B build && cmake --build build
#   ./build/blue_a2dp sink --usb 1:2 | aplay -f S16_LE -r 44100 -c 2   # needs a USB dongle
#   ./build/blue_a2dp test
cmake_minimum_required(VERSION 3.13)
project(BlueA2DP C CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_C_STANDARD 99)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED libusb-1.0)
find_package(Threads REQUIRED)

file(GLOB_RECURSE BTSTACK_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c)
add_library(blue_a2dp STATIC ${BTSTACK_SOURCES})
target_include_directories(blue_a2dp PUBLIC src ${LIBUSB_INCLUDE_DIRS})
target_compile_definitions(blue_a2dp PUBLIC BLUEA2DP_USE_LIBUSB)
target_compile_options(blue_a2dp PRIVATE -ffunction-sections -w)
target_link_libraries(blue_a2dp PUBLIC ${LIBUSB_LIBRARIES} Threads::Threads)
target_link_options(blue_a2dp PUBLIC -Wl,--gc-sections)

# Optional features of the command line tool (Miniaudio playback, MP3 / AAC files) use
# arduino-audio-tools on the desktop, which needs the Arduino-Emulator, libhelix and miniaudio.
# Each dependency is downloaded unless you point to a local copy (-DBLUEA2DP_xxx_DIR=...).
option(BLUEA2DP_AUDIO_TOOLS "CLI: playback and MP3 / AAC files via arduino-audio-tools" OFF)

# command line tool: blue_a2dp sink | source | test
add_executable(blue_a2dp_tool
  extras/cli/main.cpp
  extras/test/sbc_loopback.cpp extras/test/aac_adts.cpp
  extras/test/aptx_info.cpp extras/test/mp3_util.cpp extras/test/wav_io.cpp)
set_target_properties(blue_a2dp_tool PROPERTIES OUTPUT_NAME blue_a2dp)
target_link_libraries(blue_a2dp_tool blue_a2dp)
target_compile_options(blue_a2dp_tool PRIVATE -Wall -Wextra -Wno-unused-parameter)

if(BLUEA2DP_AUDIO_TOOLS)
  include(FetchContent)
  set(BLUEA2DP_AUDIO_TOOLS_DIR "" CACHE PATH "local arduino-audio-tools (default: download)")
  set(BLUEA2DP_EMULATOR_DIR "" CACHE PATH "local Arduino-Emulator (default: download)")
  set(BLUEA2DP_HELIX_DIR "" CACHE PATH "local codec-helix (default: download)")
  set(BLUEA2DP_MINIAUDIO_DIR "" CACHE PATH "directory with miniaudio.h (default: download)")

  function(blue_a2dp_dependency name dir url tag)
    if(dir)
      set(${name}_SRC "${dir}" PARENT_SCOPE)
    else()
      FetchContent_Declare(${name} GIT_REPOSITORY ${url} GIT_TAG ${tag} GIT_SHALLOW TRUE)
      FetchContent_GetProperties(${name})
      if(NOT ${name}_POPULATED)
        FetchContent_Populate(${name})
      endif()
      set(${name}_SRC "${${name}_SOURCE_DIR}" PARENT_SCOPE)
    endif()
  endfunction()

  blue_a2dp_dependency(audio_tools "${BLUEA2DP_AUDIO_TOOLS_DIR}" https://github.com/pschatzmann/arduino-audio-tools.git main)
  blue_a2dp_dependency(arduino_emulator "${BLUEA2DP_EMULATOR_DIR}" https://github.com/pschatzmann/Arduino-Emulator.git main)
  blue_a2dp_dependency(helix "${BLUEA2DP_HELIX_DIR}" https://github.com/pschatzmann/codec-helix.git main)
  if(BLUEA2DP_MINIAUDIO_DIR)
    set(miniaudio_SRC "${BLUEA2DP_MINIAUDIO_DIR}")
  else()
    blue_a2dp_dependency(miniaudio "" https://github.com/mackron/miniaudio.git master)
  endif()

  add_subdirectory(${arduino_emulator_SRC} ${CMAKE_BINARY_DIR}/emulator)
  add_subdirectory(${helix_SRC} ${CMAKE_BINARY_DIR}/helix)

  target_sources(blue_a2dp_tool PRIVATE extras/cli/at_io.cpp)
  target_compile_definitions(blue_a2dp_tool PRIVATE BLUEA2DP_CLI_AUDIO_TOOLS ARDUINO IS_DESKTOP)
  target_include_directories(blue_a2dp_tool PRIVATE ${audio_tools_SRC}/src ${miniaudio_SRC})
  target_compile_options(blue_a2dp_tool PRIVATE -Wno-format)
  target_link_libraries(blue_a2dp_tool arduino_emulator arduino_helix ${CMAKE_DL_LIBS} m)
endif()
