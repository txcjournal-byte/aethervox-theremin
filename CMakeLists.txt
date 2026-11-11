cmake_minimum_required(VERSION 3.22)

project(AetherVox VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if(APPLE)
    # Universal binary (Apple Silicon + Intel)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
endif()

# -----------------------------------------------------------------------------
#  JUCE: use a local checkout (-DJUCE_DIR=/path/to/JUCE) or download it
# -----------------------------------------------------------------------------
set(JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (optional)")

if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.4
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# -----------------------------------------------------------------------------
#  Plugin target
# -----------------------------------------------------------------------------
set(AETHERVOX_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND AETHERVOX_FORMATS AU)
endif()

juce_add_plugin(AetherVox
    COMPANY_NAME                "AetherVox Audio"
    COMPANY_WEBSITE             "https://example.com"
    BUNDLE_ID                   com.aethervox.theremin
    PLUGIN_MANUFACTURER_CODE    Aevx
    PLUGIN_CODE                 Aetv
    PRODUCT_NAME                "AetherVox Theremin"
    FORMATS                     ${AETHERVOX_FORMATS}
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Instrument Synth
    AU_MAIN_TYPE                kAudioUnitType_MusicDevice
    MICROPHONE_PERMISSION_ENABLED FALSE)

target_sources(AetherVox PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/DSP/ThereminEngine.cpp
    Source/GUI/AetherLookAndFeel.cpp
    Source/GUI/InstrumentView.cpp)

# Artwork: physically rendered product shot + its "sketch" pass (see Tools/render)
juce_add_binary_data(AetherVoxAssets SOURCES
    Assets/aethervox_render.jpg
    Assets/aethervox_sketch.png)

target_include_directories(AetherVox PRIVATE Source)

target_compile_definitions(AetherVox PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_MODAL_LOOPS_PERMITTED=0)

target_link_libraries(AetherVox
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        AetherVoxAssets
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# -----------------------------------------------------------------------------
#  Offline DSP test (pure C++, no JUCE):  ctest  or  ./AetherVoxDspTest
# -----------------------------------------------------------------------------
option(AETHERVOX_BUILD_TESTS "Build the offline DSP test" ON)
if(AETHERVOX_BUILD_TESTS)
    enable_testing()
    add_executable(AetherVoxDspTest Tests/DspTest.cpp Source/DSP/ThereminEngine.cpp)
    target_include_directories(AetherVoxDspTest PRIVATE Source/DSP)
    add_test(NAME AetherVoxDspTest COMMAND AetherVoxDspTest)
endif()
