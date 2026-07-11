#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "Audio/RecordingTransport.h"

using midi_funfun::core::Metronome;
using midi_funfun::core::RecordingTransport;
using midi_funfun::core::TakeManager;

TEST_CASE("RecordingTransport rejects startRecording when the take budget is exceeded", "[core][audio][recordingtransport]")
{
    TakeManager takeManager(100, 1.0); // tiny budget: even one take's worst case won't fit
    Metronome metronome;
    RecordingTransport transport(takeManager, metronome);

    REQUIRE_FALSE(transport.startRecording(false, 0, 120.0, 100.0));
    REQUIRE(transport.getState() == RecordingTransport::State::Idle);
}

TEST_CASE("RecordingTransport transitions CountIn -> Recording once the metronome finishes counting in", "[core][audio][recordingtransport]")
{
    TakeManager takeManager;
    Metronome metronome;
    RecordingTransport transport(takeManager, metronome);

    // bpm=60, sampleRate=100 => samplesPerBeat=100, countInBeats=2 (matches MetronomeTests)
    REQUIRE(transport.startRecording(true, 2, 60.0, 100.0));
    REQUIRE(transport.getState() == RecordingTransport::State::CountIn);

    auto advance1 = transport.advance(150); // covers count-in beats at 0 and 100, not yet complete beat count reached? beatsElapsed after: 2 -> complete at sample 100
    REQUIRE(transport.getState() == RecordingTransport::State::Recording);
    REQUIRE(advance1.shouldAppendToTake);
    REQUIRE_FALSE(advance1.clickSampleOffsets.empty());
}

TEST_CASE("RecordingTransport skips CountIn entirely when the metronome is disabled", "[core][audio][recordingtransport]")
{
    TakeManager takeManager;
    Metronome metronome;
    RecordingTransport transport(takeManager, metronome);

    REQUIRE(transport.startRecording(false, 4, 120.0, 100.0));
    REQUIRE(transport.getState() == RecordingTransport::State::Recording);

    auto advance = transport.advance(64);
    REQUIRE(advance.shouldAppendToTake);
    REQUIRE(advance.clickSampleOffsets.empty());
}

TEST_CASE("RecordingTransport skips CountIn when countInBeats is zero even with metronome enabled", "[core][audio][recordingtransport]")
{
    TakeManager takeManager;
    Metronome metronome;
    RecordingTransport transport(takeManager, metronome);

    REQUIRE(transport.startRecording(true, 0, 120.0, 100.0));
    REQUIRE(transport.getState() == RecordingTransport::State::Recording);

    auto advance = transport.advance(64);
    REQUIRE(advance.shouldAppendToTake);
    REQUIRE_FALSE(advance.clickSampleOffsets.empty()); // metronome keeps clicking during recording
}

TEST_CASE("RecordingTransport stopRecording returns to Idle and finalizes the take", "[core][audio][recordingtransport]")
{
    TakeManager takeManager;
    Metronome metronome;
    RecordingTransport transport(takeManager, metronome);

    REQUIRE(transport.startRecording(false, 0, 120.0, 100.0));
    transport.advance(64);
    transport.stopRecording();

    REQUIRE(transport.getState() == RecordingTransport::State::Idle);

    auto advance = transport.advance(64);
    REQUIRE_FALSE(advance.shouldAppendToTake);
    REQUIRE(advance.state == RecordingTransport::State::Idle);

    // Idle again, so a fresh recording can start
    REQUIRE(transport.startRecording(false, 0, 120.0, 100.0));
}
