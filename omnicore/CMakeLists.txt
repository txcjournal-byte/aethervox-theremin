cmake_minimum_required(VERSION 3.22)
project(OmniCore VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
endif()

set(JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (optional)")
if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.4 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(OMNI_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND OMNI_FORMATS AU)
endif()

juce_add_plugin(OmniCore
    COMPANY_NAME                "TrapWin"
    COMPANY_WEBSITE             "https://trapwin.com"
    BUNDLE_ID                   com.trapwin.omnicore3000
    PLUGIN_MANUFACTURER_CODE    Trpw
    PLUGIN_CODE                 Omc3
    PRODUCT_NAME                "OmniCore 3000"
    FORMATS                     ${OMNI_FORMATS}
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           TRUE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Instrument Synth
    AU_MAIN_TYPE                kAudioUnitType_MusicDevice)

target_sources(OmniCore PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/DSP/OmniSynth.cpp)
juce_add_binary_data(OmniCoreAssets SOURCES Assets/pyramid.jpg)
target_include_directories(OmniCore PRIVATE Source Source/DSP)
target_compile_definitions(OmniCore PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_MODAL_LOOPS_PERMITTED=0)
target_link_libraries(OmniCore
    PRIVATE juce::juce_audio_utils juce::juce_dsp OmniCoreAssets
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

option(OMNI_BUILD_TESTS "Build the offline DSP test" ON)
if(OMNI_BUILD_TESTS)
    enable_testing()
    add_executable(OmniDspTest Tests/OmniTest.cpp Source/DSP/OmniSynth.cpp)
    target_include_directories(OmniDspTest PRIVATE Source/DSP)
    add_test(NAME OmniDspTest COMMAND OmniDspTest)
endif()
