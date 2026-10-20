cmake_minimum_required(VERSION 3.22)

project(Dropout VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# JUCE is not vendored. Point DROPPER_JUCE_PATH at a checkout, or let CMake fetch 8.0.6.
set(DROPPER_JUCE_PATH "" CACHE PATH "Path to an existing JUCE source tree")

if(DROPPER_JUCE_PATH)
    add_subdirectory(${DROPPER_JUCE_PATH} ${CMAKE_BINARY_DIR}/juce EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.6
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(JUCE)
endif()

# User-level VST3 folder so the build does not need administrator rights.
if(WIN32)
    set_directory_properties(PROPERTIES JUCE_VST3_COPY_DIR "$ENV{APPDATA}/VST3")
endif()

juce_add_plugin(Dropper
    COMPANY_NAME "Antho"
    BUNDLE_ID com.antho.dropper
    PLUGIN_MANUFACTURER_CODE Anth
    PLUGIN_CODE Drpr
    FORMATS VST3 Standalone
    PRODUCT_NAME "dropout"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    VST3_CATEGORIES Fx Modulation
)

juce_generate_juce_header(Dropper)

juce_add_binary_data(DropperBinaryData
    HEADER_NAME BinaryData.h
    NAMESPACE BinaryData
    SOURCES
        Source/Fonts/Inter_Bold.ttf
)

target_sources(Dropper
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginProcessor.h
        Source/PluginEditor.cpp
        Source/PluginEditor.h
        Source/DSP/DropperEngine.cpp
        Source/DSP/DropperEngine.h
        Source/DSP/SeededRng.h
        Source/UI/DropperLookAndFeel.cpp
        Source/UI/DropperLookAndFeel.h
        Source/UI/ScheduleView.cpp
        Source/UI/ScheduleView.h
)

target_compile_definitions(Dropper
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(Dropper
    PRIVATE
        DropperBinaryData
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

target_include_directories(Dropper PRIVATE Source)

add_executable(DropperTests
    Tests/DropperEngineTests.cpp
    Source/DSP/DropperEngine.cpp
)

target_include_directories(DropperTests PRIVATE Source)
target_compile_features(DropperTests PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(DropperTests PRIVATE /W4 /permissive-)
else()
    target_compile_options(DropperTests PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()
add_test(NAME DropperEngine COMMAND DropperTests ${CMAKE_BINARY_DIR}/verify-audio)
