cmake_minimum_required(VERSION 3.22)

project(GoSequencer VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static CRT, so the built .vst3 does not depend on the VC++ redistributable.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# JUCE lives next to the Rack SDK in .toolchains by default. Override with
#   cmake -B build -DJUCE_PATH=/somewhere/else/JUCE
set(JUCE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../.toolchains/JUCE" CACHE PATH "Path to a JUCE checkout")

if(NOT EXISTS "${JUCE_PATH}/CMakeLists.txt")
    message(FATAL_ERROR
        "JUCE was not found at ${JUCE_PATH}\n"
        "Clone it with:  git clone --depth 1 https://github.com/juce-framework/JUCE.git \"${JUCE_PATH}\"")
endif()

add_subdirectory("${JUCE_PATH}" JUCE)

#==============================================================================
# The plugin is built as an instrument that emits MIDI rather than as a MIDI
# effect: that is the shape hosts route MIDI out of (in Live, the receiving
# track picks "MIDI From > Go Sequencer's track > Go Sequencer").
juce_add_plugin(GoSequencer
    PRODUCT_NAME "Go Sequencer"
    COMPANY_NAME "Florian Motz"
    BUNDLE_ID "com.florianmotz.gosequencer"
    PLUGIN_MANUFACTURER_CODE Flmz
    PLUGIN_CODE Gseq
    FORMATS VST3
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE
    VST3_CATEGORIES "Instrument" "Tools")

target_sources(GoSequencer
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/BoardComponent.cpp
        Source/MidiPortOut.cpp)

target_compile_definitions(GoSequencer
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_REPORT_APP_USAGE=0)

target_link_libraries(GoSequencer
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

#==============================================================================
# The rules engine is plain C++, so it can be checked without loading a host.
enable_testing()

add_executable(GoRulesTests tests/GoRulesTests.cpp)
target_include_directories(GoRulesTests PRIVATE Source)
target_compile_features(GoRulesTests PRIVATE cxx_std_17)

# The example record is replayed through the rules engine as part of the test.
add_test(NAME go_rules
         COMMAND GoRulesTests "${CMAKE_CURRENT_SOURCE_DIR}/sgf/89706031-145-Gruener123-FloMo.sgf")

#==============================================================================
# The two self-play players, outside the plugin: writes the games they play as
# .sgf files (loadable straight back into the plugin) and as the JSON the mockup
# animations in docs/mockups are drawn from.
add_executable(GoAiDump tools/GoAiDump.cpp)
target_include_directories(GoAiDump PRIVATE Source)
target_compile_features(GoAiDump PRIVATE cxx_std_17)
