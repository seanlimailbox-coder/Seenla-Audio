cmake_minimum_required(VERSION 3.22)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
project(SeenlaAudio LANGUAGES C CXX)

if(NOT APPLE)
    message(FATAL_ERROR "This project currently builds macOS Audio Units and VST3 plugins.")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(SEENLA_UNIVERSAL "Build for Apple Silicon and Intel" ON)
if(SEENLA_UNIVERSAL)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
endif()

include(cmake/PrepareJUCE.cmake)
add_subdirectory(third_party/JUCE)

juce_add_plugin(OpenClipper
    VERSION 2.4.23
    COMPANY_NAME "Seenla Audio"
    BUNDLE_ID "com.seenla.openclipper"
    PLUGIN_MANUFACTURER_CODE Sela
    PLUGIN_CODE OpCl
    FORMATS VST3 AU
    PRODUCT_NAME "Open Clipper"
    DESCRIPTION "A streamlined oversampled stereo clipper"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

juce_generate_juce_header(OpenClipper)

target_sources(OpenClipper PRIVATE
    Clipper/ClippingDSP.h
    Clipper/ClipperProcessor.cpp
    Clipper/ClipperProcessor.h
    Clipper/ClipperEditor.cpp
    Clipper/ClipperEditor.h)

target_compile_definitions(OpenClipper PUBLIC
    OPEN_CLIPPER_SAMPLE_ACCURATE=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_include_directories(OpenClipper PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Clipper")

target_link_libraries(OpenClipper PRIVATE
    juce::juce_audio_processors
    juce::juce_gui_extra
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)


juce_add_plugin(OpenCompressor
    VERSION 2.10.15
    COMPANY_NAME "Seenla Audio"
    BUNDLE_ID "com.seenla.opencompressor"
    PLUGIN_MANUFACTURER_CODE Sela
    PLUGIN_CODE OpCo
    FORMATS VST3 AU
    PRODUCT_NAME "Open Compressor"
    DESCRIPTION "A modern, fully featured dynamics processor"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

juce_generate_juce_header(OpenCompressor)

target_sources(OpenCompressor PRIVATE
    Compressor/CompressorProcessor.cpp
    Compressor/CompressorProcessor.h
    Compressor/CompressorEditor.cpp
    Compressor/CompressorSidechain.cpp
    Compressor/CompressorEditor.h)

target_compile_definitions(OpenCompressor PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(OpenCompressor PRIVATE
    juce::juce_audio_processors
    juce::juce_gui_extra
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)


juce_add_plugin(OpenGate
    VERSION 1.8.37
    COMPANY_NAME "Seenla Audio"
    BUNDLE_ID "com.seenla.opengate"
    PLUGIN_MANUFACTURER_CODE Sela
    PLUGIN_CODE OpGt
    FORMATS VST3 AU
    PRODUCT_NAME "Open Gate"
    DESCRIPTION "A modern stereo gate with hysteresis"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

juce_generate_juce_header(OpenGate)
target_sources(OpenGate PRIVATE
    Gate/GateProcessor.cpp
    Gate/GateProcessor.h
    Gate/GateEditor.cpp
    Gate/GateEditor.h)
target_compile_definitions(OpenGate PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(OpenGate PRIVATE
    juce::juce_audio_processors juce::juce_gui_extra juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)


juce_add_binary_data(OpenLimiterArtwork HEADER_NAME LimiterArtwork.h NAMESPACE LimiterArtwork
    SOURCES Limiter/Assets/Wrench.png)

juce_add_plugin(OpenLimiter
    VERSION 7.3.48 COMPANY_NAME "Seenla Audio"
    BUNDLE_ID "com.seenla.openlimiter"
    PLUGIN_MANUFACTURER_CODE Sela PLUGIN_CODE OpLm
    FORMATS VST3 AU PRODUCT_NAME "Open Limiter"
    DESCRIPTION "A stereo lookahead limiter with loudness metering"
    IS_SYNTH FALSE NEEDS_MIDI_INPUT FALSE NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE EDITOR_WANTS_KEYBOARD_FOCUS FALSE COPY_PLUGIN_AFTER_BUILD FALSE)
juce_generate_juce_header(OpenLimiter)
target_sources(OpenLimiter PRIVATE Limiter/Engine/StreamingLimiter.cpp Limiter/LimiterProcessor.cpp Limiter/Presets.cpp Limiter/Presets.h Limiter/LimiterProcessor.h Limiter/LimiterEditor.cpp Limiter/LimiterEditor.h)
target_compile_definitions(OpenLimiter PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(OpenLimiter PRIVATE OpenLimiterArtwork juce::juce_audio_processors juce::juce_gui_extra juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# Preserve the limiter engine's floating-point evaluation boundaries.
set_source_files_properties(Limiter/Engine/StreamingLimiter.cpp
    PROPERTIES COMPILE_OPTIONS "-ffp-contract=off")

# Sign the complete local bundle after JUCE has generated its resources.
find_program(SEENLA_CODESIGN codesign REQUIRED)
foreach(plugin IN ITEMS OpenClipper OpenCompressor OpenGate OpenLimiter)
    foreach(format IN ITEMS AU VST3)
        add_custom_command(TARGET ${plugin}_${format} POST_BUILD
            COMMAND "${SEENLA_CODESIGN}" --force --deep --sign - --timestamp=none
                "$<TARGET_BUNDLE_DIR:${plugin}_${format}>"
            VERBATIM)
    endforeach()
endforeach()
