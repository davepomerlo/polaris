cmake_minimum_required(VERSION 3.22)
project(Polaris VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(POLARIS_CORE_ONLY "Build only the JUCE-free DSP core and its tests (no plugin)" OFF)
option(POLARIS_BUILD_TESTS "Build the core unit tests" ON)

# ---------------------------------------------------------------- DSP core (no JUCE)
add_library(polaris_core STATIC
    Source/core/LoudnessMeter.cpp
    Source/core/Analyzer.cpp
    Source/core/Assistant.cpp)
target_include_directories(polaris_core PUBLIC Source/core)
set_target_properties(polaris_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(POLARIS_BUILD_TESTS)
    enable_testing()
    add_executable(polaris_core_tests tests/core_tests.cpp)
    target_link_libraries(polaris_core_tests PRIVATE polaris_core)
    add_test(NAME polaris_core_tests COMMAND polaris_core_tests)
endif()

if(POLARIS_CORE_ONLY)
    return()
endif()

# ---------------------------------------------------------------- JUCE
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(Polaris
    COMPANY_NAME                "davepomerlo"
    BUNDLE_ID                   "com.davepomerlo.polaris"
    PLUGIN_MANUFACTURER_CODE    Dvpm
    PLUGIN_CODE                 Plrs
    FORMATS                     VST3 AU Standalone     # formats a platform can't build are skipped
    PRODUCT_NAME                "Polaris"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES             Fx Mastering Analyzer
    COPY_PLUGIN_AFTER_BUILD     FALSE)

target_sources(Polaris PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/ui/SpectrumView.cpp
    Source/ui/AssistantPanel.cpp
    Source/ui/MeterPanel.cpp
    Source/ui/ModulePanel.cpp)

target_include_directories(Polaris PRIVATE Source)

target_compile_definitions(Polaris PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Polaris
    PRIVATE
        polaris_core
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
