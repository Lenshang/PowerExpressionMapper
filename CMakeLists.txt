cmake_minimum_required(VERSION 3.21 FATAL_ERROR)

project(PowerExpressionMapper
    VERSION 0.1.0
    DESCRIPTION "A CLAP MIDI note-name mapper: named notes pass through, all other notes are delayed"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# -----------------------------------------------------------------------------
# Dependencies: JUCE + clap-juce-extensions (which bundles the CLAP SDK).
#
# By default they are reused from the sibling PowerDrumMapper checkout
# (folder still named BitwigDrumMapper on disk) so no extra downloads are
# needed. Override with -DJUCE_DIR=... / -DCLAP_JUCE_EXTENSIONS_DIR=... or
# drop checkouts into libs/ .
# -----------------------------------------------------------------------------
set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../BitwigDrumMapper/libs/JUCE"
    CACHE PATH "Path to a JUCE checkout")
set(CLAP_JUCE_EXTENSIONS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../BitwigDrumMapper/libs/clap-juce-extensions"
    CACHE PATH "Path to a clap-juce-extensions checkout")

if(NOT EXISTS "${JUCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "JUCE not found at '${JUCE_DIR}'. "
        "Set -DJUCE_DIR=<path to JUCE> (a checkout of JUCE 8).")
endif()
if(NOT EXISTS "${CLAP_JUCE_EXTENSIONS_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "clap-juce-extensions not found at '${CLAP_JUCE_EXTENSIONS_DIR}'. "
        "Set -DCLAP_JUCE_EXTENSIONS_DIR=<path>.")
endif()

add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE)
add_subdirectory(${CLAP_JUCE_EXTENSIONS_DIR} ${CMAKE_BINARY_DIR}/clap-juce-extensions EXCLUDE_FROM_ALL)

# -----------------------------------------------------------------------------
# The plugin target. It is a pure MIDI effect (no audio buses).
#
# Notes whose number appears in the user-edited list pass through untouched at
# their original sample position ("direct trigger"). All other notes are
# delayed by a configurable number of samples (carried across block boundaries
# sample-accurately). Entry names are pushed to the host via the CLAP
# note-name extension, so Bitwig shows them on the piano roll / note lanes.
#
# The build target / product name is PowerExpressionMapper; internal stable
# IDs (PLUGIN_MANUFACTURER_CODE, PLUGIN_CODE, CLAP_ID) are kept from the
# original ExpressionMapper release so existing host sessions resolve the
# same plugin identity.
# -----------------------------------------------------------------------------
juce_add_plugin(PowerExpressionMapper
    COMPANY_NAME            "PowerExpressionMapper"
    PLUGIN_MANUFACTURER_CODE "ExMp"
    PLUGIN_CODE             "ExM1"
    FORMATS                 VST3 Standalone
    PRODUCT_NAME            "PowerExpressionMapper"
    IS_SYNTH                FALSE
    NEEDS_MIDI_INPUT        TRUE
    NEEDS_MIDI_OUTPUT       TRUE
    IS_MIDI_EFFECT          TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE)

# CLAP format target -> `PowerExpressionMapper_CLAP`
clap_juce_extensions_plugin(
    TARGET       PowerExpressionMapper
    CLAP_ID      "com.expressionmapper.expressionmapper"
    CLAP_FEATURES note-effect utility)

# -----------------------------------------------------------------------------
# Sources
# -----------------------------------------------------------------------------
target_sources(PowerExpressionMapper PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/ExpressionMapping.cpp
    Source/NoteNameUtils.cpp
    Source/MappingTableComponent.cpp)

# -----------------------------------------------------------------------------
# JUCE configuration: trim unused subsystems for faster / leaner builds
# -----------------------------------------------------------------------------
target_compile_definitions(PowerExpressionMapper PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_MODAL_LOOPS_PERMITTED=1)

target_link_libraries(PowerExpressionMapper
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_plugin_client
        clap_juce_extensions
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# -----------------------------------------------------------------------------
# Headless test runner. Compiles the same sources as the plugin and exercises
# the MIDI routing / delay logic, state persistence, CSV I/O and the CLAP
# note-name reporting without a host or audio device.
# -----------------------------------------------------------------------------
juce_add_console_app(PowerExpressionMapperTests PRODUCT_NAME "PowerExpressionMapperTests")

target_sources(PowerExpressionMapperTests PRIVATE
    Tests/ProcessorTests.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/ExpressionMapping.cpp
    Source/NoteNameUtils.cpp
    Source/MappingTableComponent.cpp)

target_compile_definitions(PowerExpressionMapperTests PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    # Plugin targets get these from juce_add_plugin; the test runner needs the
    # handful our sources actually reference spelled out manually.
    JucePlugin_Name="PowerExpressionMapper"
    JucePlugin_Manufacturer="PowerExpressionMapper"
    JucePlugin_ManufacturerCode=ExMp
    JucePlugin_PluginCode=ExM1)

target_link_libraries(PowerExpressionMapperTests
    PRIVATE
        juce::juce_audio_utils
        clap_juce_extensions
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

add_test(NAME PowerExpressionMapperTests
         COMMAND PowerExpressionMapperTests)
