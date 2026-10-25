cmake_minimum_required(VERSION 3.22)

set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build for Apple Silicon and Intel")

project(TaperGain VERSION 1.0.0)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 7.0.12
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(TaperGain
    COMPANY_NAME "TaperTools"
    PLUGIN_MANUFACTURER_CODE Tptl
    PLUGIN_CODE Tpgn
    FORMATS AU
    PRODUCT_NAME "Taper Gain"
    AU_MAIN_TYPE kAudioUnitType_Effect
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(TaperGain PRIVATE Source/Plugin.cpp)

target_compile_definitions(TaperGain PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)

target_link_libraries(TaperGain
    PRIVATE juce::juce_audio_processors
    PUBLIC juce::juce_recommended_config_flags)
