cmake_minimum_required(VERSION 3.22)
project(DOMINATRIX VERSION 1.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")

# Same JUCE version as DREAD, Possessed, BuzzKill and Chop Shop.
# (Pass -DJUCE_LOCAL_DIR=/path/to/JUCE to build against a local checkout.)
if(JUCE_LOCAL_DIR)
    add_subdirectory(${JUCE_LOCAL_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.15
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

if(NOT DOMINATRIX_TESTS_ONLY)
juce_add_plugin(Dominatrix
    COMPANY_NAME "Castle Danger"
    PRODUCT_NAME "Dominatrix"
    BUNDLE_ID com.castledanger.dominatrix
    PLUGIN_MANUFACTURER_CODE Cdgr
    PLUGIN_CODE Dmx1
    FORMATS VST3 AU Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Dynamics Mastering
    AU_MAIN_TYPE kAudioUnitType_Effect)

target_sources(Dominatrix PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(Dominatrix PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Dominatrix
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
endif()

# Headless checks: renders the editor to gui.png and tests the DSP + presets
juce_add_console_app(DominatrixSnapshot PRODUCT_NAME "DominatrixSnapshot")
target_sources(DominatrixSnapshot PRIVATE test/snapshot.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_compile_definitions(DominatrixSnapshot PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1
    "JucePlugin_VersionString=\"${PROJECT_VERSION}\"")
target_link_libraries(DominatrixSnapshot PRIVATE juce::juce_audio_processors juce::juce_gui_basics juce::juce_dsp PUBLIC juce::juce_recommended_config_flags)
