cmake_minimum_required(VERSION 3.22)
project(JUKEBOX VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
# Static MSVC runtime: the plugin loads without installing the Visual C++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(JUKEBOX_BUILD_TESTS "Build the offline render test tool" OFF)

# JUCE is fetched automatically (pass -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE to use a local copy).
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.9
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

set(JUKEBOX_FORMATS VST3)
if(APPLE)
    list(APPEND JUKEBOX_FORMATS AU)
endif()

juce_add_plugin(Jukebox
    PRODUCT_NAME "JUKEBOX"
    COMPANY_NAME "BUY OR BE A LOSER"
    BUNDLE_ID com.buyorbealoser.jukebox
    PLUGIN_MANUFACTURER_CODE Bobl
    PLUGIN_CODE Jkbx
    FORMATS ${JUKEBOX_FORMATS}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Synth
    AU_MAIN_TYPE kAudioUnitType_MusicDevice)

juce_add_binary_data(JukeboxAssets SOURCES
    Assets/jukebox.png
    Assets/Minecraft.ttf
    Assets/discs/disc_yellow.png
    Assets/discs/disc_green.png
    Assets/discs/disc_red.png
    Assets/discs/disc_purple.png
    Assets/discs/disc_black.png
    Assets/discs/disc_white.png
    Assets/discs/disc_broken.png
    Assets/discs/disc_blue.png
    Assets/discs/disc_orange.png
    Assets/discs/disc_swirl.png)

set(ENGINE_SOURCES
    Source/Engine/Instruments.cpp
    Source/Engine/Engine.cpp)

target_sources(Jukebox PRIVATE
    ${ENGINE_SOURCES}
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/UI/Scene.cpp
    Source/UI/VoxelNotes.cpp)

target_compile_definitions(Jukebox PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Jukebox
    PRIVATE
        JukeboxAssets
        juce::juce_audio_processors
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(JUKEBOX_BUILD_TESTS)
    juce_add_console_app(JukeboxRenderTest PRODUCT_NAME "JukeboxRenderTest")
    target_sources(JukeboxRenderTest PRIVATE Tools/RenderTest.cpp ${ENGINE_SOURCES})
    target_compile_definitions(JukeboxRenderTest PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
    target_link_libraries(JukeboxRenderTest PRIVATE juce::juce_dsp juce::juce_audio_formats
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

    # End-to-end test against the real plugin code (MIDI, gate, state, editor screenshots)
    juce_add_console_app(JukeboxPluginTest PRODUCT_NAME "JukeboxPluginTest")
    target_sources(JukeboxPluginTest PRIVATE Tools/PluginTest.cpp)
    target_compile_definitions(JukeboxPluginTest PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
    target_link_libraries(JukeboxPluginTest PRIVATE Jukebox JukeboxAssets juce::juce_audio_formats
        PUBLIC juce::juce_recommended_config_flags)

    # Loads the built JUKEBOX.vst3 bundle through JUCE's VST3 host, like a DAW
    juce_add_console_app(JukeboxHostTest PRODUCT_NAME "JukeboxHostTest")
    target_sources(JukeboxHostTest PRIVATE Tools/HostTest.cpp)
    target_compile_definitions(JukeboxHostTest PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
    target_link_libraries(JukeboxHostTest PRIVATE juce::juce_audio_processors
        PUBLIC juce::juce_recommended_config_flags)
endif()
