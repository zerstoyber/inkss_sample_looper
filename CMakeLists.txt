cmake_minimum_required(VERSION 3.22)

project(INKSSSampler VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.4
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

# Mac build: Standalone, AU, and VST3 for Ableton Live.
# An iPhone/iPad app and AUv3 need full Xcode and are a later target
# from this same engine.
juce_add_plugin(INKSSSampler
    COMPANY_NAME "Inkasso"
    BUNDLE_ID com.inkasso.inksssampler
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE Inks
    PLUGIN_CODE IkSm
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "INKSS Sampler"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Source/Icon.png"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "INKSS Sampler nimmt externes Audio über das Mikrofon auf.")

target_sources(INKSSSampler
    PRIVATE
        Source/SampleEngine.h
        Source/SampleEngine.cpp
        Source/PluginProcessor.h
        Source/PluginProcessor.cpp
        Source/PluginEditor.h
        Source/PluginEditor.cpp)

juce_add_binary_data(InkssAssets SOURCES Source/wordmark.png)

juce_generate_juce_header(INKSSSampler)

target_compile_definitions(INKSSSampler
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(INKSSSampler
    PRIVATE
        juce::juce_audio_utils
        InkssAssets
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
