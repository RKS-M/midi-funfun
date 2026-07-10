add_library(midi_funfun_core STATIC
    Version.h
    Version.cpp
)

target_include_directories(midi_funfun_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(midi_funfun_core PUBLIC
    juce::juce_core
    juce::juce_audio_basics
    juce::juce_dsp
)

target_compile_features(midi_funfun_core PUBLIC cxx_std_20)
