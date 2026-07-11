#include <catch2/catch_test_macros.hpp>

#include "Audio/Metronome.h"

using midi_funfun::core::Metronome;

TEST_CASE("Metronome produces clicks at the correct sample offsets and signals count-in completion", "[core][audio][metronome]")
{
    Metronome metronome;
    // bpm=60, sampleRate=100 => samplesPerBeat=100. countInBeats=2.
    metronome.start(60.0, 100.0, 2);

    REQUIRE(metronome.isCountInActive());

    auto result = metronome.processBlock(250);

    REQUIRE(result.clickSampleOffsets.size() == 3);
    REQUIRE(result.clickSampleOffsets[0] == 0);
    REQUIRE(result.clickSampleOffsets[1] == 100);
    REQUIRE(result.clickSampleOffsets[2] == 200);
    REQUIRE(result.countInJustCompleted);
    REQUIRE_FALSE(metronome.isCountInActive());
}

TEST_CASE("Metronome only signals count-in completion once", "[core][audio][metronome]")
{
    Metronome metronome;
    metronome.start(60.0, 100.0, 1); // one count-in beat, fires immediately at sample 0

    auto first = metronome.processBlock(50);
    REQUIRE(first.countInJustCompleted);

    auto second = metronome.processBlock(200);
    REQUIRE_FALSE(second.countInJustCompleted);
    // main clicks keep coming after count-in (next beat boundaries at 100, 200 within [50,250))
    REQUIRE(second.clickSampleOffsets.size() == 2);
}

TEST_CASE("Metronome with zero count-in beats starts active immediately", "[core][audio][metronome]")
{
    Metronome metronome;
    metronome.start(120.0, 100.0, 0);

    REQUIRE_FALSE(metronome.isCountInActive());

    // bpm=120, sampleRate=100 => samplesPerBeat=50; only the sample-0 click falls within [0,40)
    auto result = metronome.processBlock(40);
    REQUIRE_FALSE(result.countInJustCompleted);
    REQUIRE(result.clickSampleOffsets.size() == 1);
    REQUIRE(result.clickSampleOffsets[0] == 0);
}
