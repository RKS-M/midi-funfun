add_executable(MidiFunfunTests
    CoreVersionTests.cpp
    PeakLevelTrackerTests.cpp
    TakeManagerTests.cpp
    MetronomeTests.cpp
    RecordingTransportTests.cpp
    MonitoringPassthroughTests.cpp
    NoteSequenceTests.cpp
    YinPitchDetectorTests.cpp
    NoteSegmenterTests.cpp
    OctaveErrorCorrectorTests.cpp
    PitchAnalyzerTests.cpp
)

target_link_libraries(MidiFunfunTests PRIVATE
    midi_funfun_core
    Catch2::Catch2WithMain
)

include(Catch)
catch_discover_tests(MidiFunfunTests)
