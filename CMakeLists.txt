cmake_minimum_required(VERSION 3.22)

# Static MSVC runtime so nothing extra needs installing on Windows.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
# Apple universal binary (Apple silicon + Intel) by default; Windows builds are x64.
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")

project(LiveAlign VERSION 0.19.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(LA_BUILD_TESTS "Build the analysis engine tests" OFF)

# JUCE is fetched at configure time. Set JUCE_DIR_OVERRIDE to use a local checkout instead.
if (JUCE_DIR_OVERRIDE)
    add_subdirectory(${JUCE_DIR_OVERRIDE} JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 9.0.3
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(LiveAlign
    COMPANY_NAME "LiveAlign"
    PLUGIN_MANUFACTURER_CODE LvAl
    PLUGIN_CODE LvAg
    FORMATS Standalone VST3
    PRODUCT_NAME "LiveAlign"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/LiveAlignIcon.png"
    VST3_CATEGORIES Fx Analyzer
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "LiveAlign measures your sound system through a measurement microphone."
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(LiveAlign PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/StandaloneApp.cpp)

target_compile_definitions(LiveAlign PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)

# ASIO on Windows, using the ASIO headers bundled with JUCE 9 (Steinberg licence terms apply).
if (WIN32)
    target_compile_definitions(LiveAlign PUBLIC JUCE_ASIO=1)
endif()

target_link_libraries(LiveAlign
    PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

if (LA_BUILD_TESTS)
    enable_testing()
    add_executable(AnalysisTests Tests/AnalysisTests.cpp)
    target_include_directories(AnalysisTests PRIVATE Source)
    add_test(NAME AnalysisTests COMMAND AnalysisTests)

    # the tuning logic with JUCE's core: level balance, the sheet, prefs, coverage, headroom, history, venue list
    juce_add_console_app(AppTests PRODUCT_NAME "AppTests")
    target_sources(AppTests PRIVATE Tests/AppTests.cpp)
    target_include_directories(AppTests PRIVATE Source)
    target_compile_definitions(AppTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1
                                               JucePlugin_VersionString="${PROJECT_VERSION}")
    target_link_libraries(AppTests PRIVATE juce::juce_core juce::juce_events juce::juce_data_structures
                                   PUBLIC juce::juce_recommended_config_flags)
    add_test(NAME AppTests COMMAND AppTests)
endif()

# Local UI check: simulated PA + room, screenshots of every view (not built in CI).
option(LA_BUILD_PREVIEW "Build the screenshot preview tool" OFF)
if (LA_BUILD_PREVIEW)
    target_sources(LiveAlign PRIVATE Tools/Preview.cpp)
    file(WRITE ${CMAKE_BINARY_DIR}/preview_main.cpp "int laPreviewMain (int, char**);\nint main (int c, char** v) { return laPreviewMain (c, v); }\n")
    add_executable(LiveAlignPreview ${CMAKE_BINARY_DIR}/preview_main.cpp)
    target_link_libraries(LiveAlignPreview PRIVATE LiveAlign)
endif()
