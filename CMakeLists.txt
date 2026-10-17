cmake_minimum_required(VERSION 3.22)

project(ClassicChannelStrip VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
option(COPY_PLUGIN_AFTER_BUILD "Copy built plug-in formats to user plug-in folders" OFF)

set(JUCE_SOURCE_DIR "" CACHE PATH "Path to a JUCE 8.0.9 source checkout")
if(JUCE_SOURCE_DIR AND EXISTS "${JUCE_SOURCE_DIR}/CMakeLists.txt")
    add_subdirectory("${JUCE_SOURCE_DIR}" juce-build)
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/JUCE/CMakeLists.txt")
    add_subdirectory(third_party/JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.9
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# The card model has no JUCE dependency, so its circuit regression suite runs offline.
file(GLOB_RECURSE CLASSIC_CHANNEL_DSP_SOURCES CONFIGURE_DEPENDS Source/dsp/*.cpp)
add_library(classic_channel_dsp STATIC ${CLASSIC_CHANNEL_DSP_SOURCES})
target_include_directories(classic_channel_dsp PUBLIC Source)
set_target_properties(classic_channel_dsp PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(classic_channel_dsp PRIVATE /O2 /W4)
else()
    target_compile_options(classic_channel_dsp PRIVATE -O3 -Wall -Wextra -Wpedantic)
endif()

enable_testing()
add_executable(classic_channel_test tests/ssl_test.cpp)
target_link_libraries(classic_channel_test PRIVATE classic_channel_dsp)
add_test(NAME classic_channel_test COMMAND classic_channel_test)

add_executable(classic_channel_measure tests/measure.cpp)
target_link_libraries(classic_channel_measure PRIVATE classic_channel_dsp)
if(MSVC)
    target_compile_options(classic_channel_measure PRIVATE /O2)
else()
    target_compile_options(classic_channel_measure PRIVATE -O2)
endif()

add_executable(classic_channel_eq_ac tools/ssl_eq_ac.cpp)
if(MSVC)
    target_compile_options(classic_channel_eq_ac PRIVATE /O2)
else()
    target_compile_options(classic_channel_eq_ac PRIVATE -O2)
endif()

juce_add_plugin(ClassicChannelStrip
    PRODUCT_NAME "Classic Channel Strip"
    COMPANY_NAME "codycodie"
    BUNDLE_ID "com.c0dypeng.classicchannelstrip"
    PLUGIN_MANUFACTURER_CODE Cdpg
    PLUGIN_CODE ClCs
    FORMATS AU VST3
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Fx EQ Dynamics
    AU_MAIN_TYPE kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD ${COPY_PLUGIN_AFTER_BUILD})

juce_add_binary_data(ClassicChannelStripAssets SOURCES
    Resources/fonts/Inter-SemiBold.ttf
    Resources/fonts/JetBrainsMono-Medium.ttf)
set_target_properties(ClassicChannelStripAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(CLASSIC_CHANNEL_PLUGIN_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)
target_sources(ClassicChannelStrip PRIVATE ${CLASSIC_CHANNEL_PLUGIN_SOURCES})
target_compile_definitions(ClassicChannelStrip PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(ClassicChannelStrip
    PRIVATE
        classic_channel_dsp
        ClassicChannelStripAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Offline processor tests cover old state migration, routing, sidechains, latency and allocations.
juce_add_console_app(classic_channel_processor_test)
target_sources(classic_channel_processor_test PRIVATE
    tests/processor_test.cpp
    ${CLASSIC_CHANNEL_PLUGIN_SOURCES})
target_compile_definitions(classic_channel_processor_test PRIVATE
    JucePlugin_Name="Classic Channel Strip"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)
target_link_libraries(classic_channel_processor_test PRIVATE
    classic_channel_dsp
    ClassicChannelStripAssets
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags)
add_test(NAME classic_channel_processor_test COMMAND classic_channel_processor_test)

# Development utility: render the editor, process a WAV, or measure card response.
juce_add_console_app(classic_channel_snapshot)
target_sources(classic_channel_snapshot PRIVATE
    tests/snapshot.cpp
    Source/ResponseAnalyser.cpp
    ${CLASSIC_CHANNEL_PLUGIN_SOURCES})
target_compile_definitions(classic_channel_snapshot PRIVATE
    JucePlugin_Name="Classic Channel Strip"
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)
target_link_libraries(classic_channel_snapshot PRIVATE
    classic_channel_dsp
    ClassicChannelStripAssets
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags)
