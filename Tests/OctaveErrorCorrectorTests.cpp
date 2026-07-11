#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "Pitch/OctaveErrorCorrector.h"

using midi_funfun::core::OctaveErrorCorrector;
using midi_funfun::core::RawNoteSegment;

namespace
{
    RawNoteSegment makeSegment(int pitch, int startFrame = 0, int lengthFrames = 10)
    {
        RawNoteSegment s;
        s.pitch = pitch;
        s.startFrame = startFrame;
        s.lengthFrames = lengthFrames;
        return s;
    }
}

TEST_CASE("OctaveErrorCorrector corrects an isolated +12 semitone jump back to the surrounding pitch", "[OctaveErrorCorrector]")
{
    std::vector<RawNoteSegment> segments {
        makeSegment(60, 0, 10),
        makeSegment(72, 10, 10), // isolated octave-up error
        makeSegment(60, 20, 10),
    };

    OctaveErrorCorrector corrector;
    corrector.correct(segments);

    REQUIRE(segments[1].pitch == 60);
    // timing/length unaffected
    REQUIRE(segments[1].startFrame == 10);
    REQUIRE(segments[1].lengthFrames == 10);
}

TEST_CASE("OctaveErrorCorrector corrects an isolated -12 semitone jump back to the surrounding pitch", "[OctaveErrorCorrector]")
{
    std::vector<RawNoteSegment> segments {
        makeSegment(60, 0, 10),
        makeSegment(48, 10, 10), // isolated octave-down error
        makeSegment(60, 20, 10),
    };

    OctaveErrorCorrector corrector;
    corrector.correct(segments);

    REQUIRE(segments[1].pitch == 60);
}

TEST_CASE("OctaveErrorCorrector does not correct when the surrounding context itself is unstable", "[OctaveErrorCorrector]")
{
    // 前後(prev=60, next=67)がneighborConsistencyToleranceSemitones(既定1)を超えて異なる
    // -> 「安定した文脈」を形成していないので補正しない
    std::vector<RawNoteSegment> segments {
        makeSegment(60, 0, 10),
        makeSegment(72, 10, 10),
        makeSegment(67, 20, 10),
    };

    OctaveErrorCorrector corrector;
    corrector.correct(segments);

    REQUIRE(segments[1].pitch == 72); // unchanged
}

TEST_CASE("OctaveErrorCorrector leaves the first and last segment untouched (no context on one side)", "[OctaveErrorCorrector]")
{
    std::vector<RawNoteSegment> segments {
        makeSegment(72, 0, 10),  // first: no preceding context, must not be "corrected"
        makeSegment(60, 10, 10),
        makeSegment(60, 20, 10),
        makeSegment(72, 30, 10), // last: no following context, must not be "corrected"
    };

    OctaveErrorCorrector corrector;
    corrector.correct(segments);

    REQUIRE(segments.front().pitch == 72);
    REQUIRE(segments.back().pitch == 72);
}
