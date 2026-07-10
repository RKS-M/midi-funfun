juce_add_plugin(MidiFunfun
    COMPANY_NAME "RKSM"
    PRODUCT_NAME "MIDI FUNFUN"
    BUNDLE_ID "com.rksm.midifunfun"
    PLUGIN_MANUFACTURER_CODE Rksm
    PLUGIN_CODE Mfun
    FORMATS Standalone VST3
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES "Tools"
    COPY_PLUGIN_AFTER_BUILD FALSE
)

target_sources(MidiFunfun PRIVATE
    PluginProcessor.h
    PluginProcessor.cpp
    PluginEditor.h
    PluginEditor.cpp
)

target_include_directories(MidiFunfun PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_definitions(MidiFunfun PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(MidiFunfun PRIVATE
    midi_funfun_core
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
