cmake_minimum_required(VERSION 3.22)

# macOS: one universal binary (Apple Silicon + Intel)
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "")

project(FogZ VERSION 1.3.0)

# Local builds can point to an existing JUCE checkout:  cmake -B build -DJUCE_DIR=/path/to/JUCE
if(DEFINED JUCE_DIR)
  add_subdirectory(${JUCE_DIR} juce)
else()
  include(FetchContent)
  FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.15
    GIT_SHALLOW ON)
  FetchContent_MakeAvailable(JUCE)
endif()

# every picture is embedded in the plugin (nothing to install next to it)
juce_add_binary_data(FogZAssets SOURCES
  background.png
  fader_cap.png button_up.png
  fs_preset_up.png fs_bypass_up.png)

juce_add_plugin(FogZ
  COMPANY_NAME "Studio Wiga"
  PLUGIN_MANUFACTURER_CODE Wiga
  PLUGIN_CODE WgF2
  FORMATS VST3
  VST3_CATEGORIES Fx "Pitch Shift"
  BUNDLE_ID com.studiowiga.fogz
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  PRODUCT_NAME "Fog Z"
  COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(FogZ PRIVATE PluginProcessor.cpp PluginEditor.cpp)

# the octave engine is ~150 band filters per sample: let the compiler vectorise it
if(MSVC)
  set_source_files_properties(PluginProcessor.cpp PROPERTIES COMPILE_OPTIONS "/fp:fast")
else()
  set_source_files_properties(PluginProcessor.cpp PROPERTIES COMPILE_OPTIONS "-O3;-ffast-math")
endif()

target_compile_definitions(FogZ PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(FogZ
  PRIVATE
    FogZAssets
    juce::juce_audio_utils
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
