cmake_minimum_required(VERSION 3.24)
project(w_vst3 VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# libobs: headers + import library. Prepare with scripts/setup-libobs.ps1
set(LIBOBS_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/deps/libobs/include" CACHE PATH "libobs headers (contains obs-module.h)")
set(LIBOBS_LIB "${CMAKE_SOURCE_DIR}/deps/libobs/lib/obs.lib" CACHE FILEPATH "obs.lib import library")

include(FetchContent)
FetchContent_Declare(
  JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.8
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

add_library(w_vst3 MODULE
  src/plugin-main.cpp
  src/juce-host.cpp
  src/scanner.cpp
  src/vst-filter.cpp)

target_include_directories(w_vst3 PRIVATE "${LIBOBS_INCLUDE_DIR}")
target_link_libraries(w_vst3 PRIVATE
  "${LIBOBS_LIB}"
  juce::juce_audio_processors
  juce::juce_gui_basics
  juce::juce_recommended_config_flags)

target_compile_definitions(w_vst3 PRIVATE
  JUCE_PLUGINHOST_VST3=1
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_MODAL_LOOPS_PERMITTED=1
  JUCE_DISPLAY_SPLASH_SCREEN=0
  JUCE_REPORT_APP_USAGE=0)

target_compile_options(w_vst3 PRIVATE /utf-8)

set_target_properties(w_vst3 PROPERTIES PREFIX "" OUTPUT_NAME "w_vst3")

# Out-of-process VST3 probe: a crashing plugin kills this exe, not OBS.
add_executable(w_vst3_scanner src/scanner-main.cpp)
target_link_libraries(w_vst3_scanner PRIVATE
  juce::juce_audio_processors
  juce::juce_gui_basics
  juce::juce_recommended_config_flags)
target_compile_definitions(w_vst3_scanner PRIVATE
  JUCE_PLUGINHOST_VST3=1
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_DISPLAY_SPLASH_SCREEN=0
  JUCE_REPORT_APP_USAGE=0)
target_compile_options(w_vst3_scanner PRIVATE /utf-8)

# Install to OBS plugin dir layout: <prefix>/obs-plugins/64bit/w_vst3.dll
install(TARGETS w_vst3 w_vst3_scanner RUNTIME DESTINATION obs-plugins/64bit LIBRARY DESTINATION obs-plugins/64bit)
