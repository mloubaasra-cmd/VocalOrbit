cmake_minimum_required(VERSION 3.22)
project(VocalOrbit VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Use a local JUCE checkout if you pass -DJUCE_DIR=path/to/JUCE,
# otherwise CMake downloads JUCE automatically.
if(DEFINED JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.4
        GIT_SHALLOW    ON)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(VocalOrbit
    COMPANY_NAME              "VocalOrbit Audio"
    PLUGIN_MANUFACTURER_CODE  Vorb
    PLUGIN_CODE               Vob1
    FORMATS                   VST3 Standalone
    PRODUCT_NAME              "VocalOrbit"
    IS_SYNTH                  FALSE
    NEEDS_MIDI_INPUT          FALSE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES           Fx Spatial
    COPY_PLUGIN_AFTER_BUILD   FALSE)

target_sources(VocalOrbit PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Scene3D.cpp)

target_compile_definitions(VocalOrbit PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(VocalOrbit
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

option(VOCALORBIT_BUILD_TEST "Build the offline render test" OFF)
if(VOCALORBIT_BUILD_TEST)
    juce_add_console_app(RenderTest)
    target_sources(RenderTest PRIVATE test/RenderTest.cpp Source/PluginProcessor.cpp
                                      Source/PluginEditor.cpp Source/Scene3D.cpp)
    target_compile_definitions(RenderTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
                                                  JucePlugin_Name="VocalOrbit" JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(RenderTest PRIVATE juce::juce_audio_utils juce::juce_dsp
                                             juce::juce_recommended_config_flags)
endif()
