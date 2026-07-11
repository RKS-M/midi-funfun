add_library(midi_funfun_core STATIC
    Version.h
    Version.cpp
    Audio/PeakLevelTracker.h
    Audio/PeakLevelTracker.cpp
    Audio/Take.h
    Audio/TakeManager.h
    Audio/TakeManager.cpp
    Audio/Metronome.h
    Audio/Metronome.cpp
    Audio/RecordingTransport.h
    Audio/RecordingTransport.cpp
    Model/Note.h
    Model/NoteSequence.h
    Model/NoteSequence.cpp
    Pitch/YinPitchDetector.h
    Pitch/YinPitchDetector.cpp
    Pitch/NoteSegmenter.h
    Pitch/NoteSegmenter.cpp
    Pitch/OctaveErrorCorrector.h
    Pitch/OctaveErrorCorrector.cpp
    Pitch/PitchAnalyzer.h
    Pitch/PitchAnalyzer.cpp
)

target_include_directories(midi_funfun_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(midi_funfun_core PUBLIC
    juce::juce_core
    juce::juce_audio_basics
    juce::juce_dsp
)

target_compile_features(midi_funfun_core PUBLIC cxx_std_20)
