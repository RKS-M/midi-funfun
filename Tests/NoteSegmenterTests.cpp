#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "Pitch/NoteSegmenter.h"

using midi_funfun::core::NoteSegmenter;
using midi_funfun::core::PitchFrame;
using midi_funfun::core::RawNoteSegment;

namespace
{
    // hopSize=1, sampleRate=10.0 -> 1フレーム = 0.1秒。minNoteLengthSeconds=0.5 -> 5フレーム以上で生存。
    constexpr int hopSize = 1;
    constexpr double sampleRate = 10.0;

    NoteSegmenter::Settings makeSettings()
    {
        NoteSegmenter::Settings s;
        s.noiseGateRmsThreshold = 0.1;
        s.minNoteLengthSeconds = 0.5;
        return s;
    }

    PitchFrame voicedFrame(double freq = 440.0, double rms = 0.5)
    {
        PitchFrame f;
        f.voiced = true;
        f.frequencyHz = freq;
        f.rmsLevel = rms;
        f.confidence = 0.9;
        return f;
    }

    PitchFrame unvoicedFrame()
    {
        PitchFrame f;
        f.voiced = false;
        f.frequencyHz = 0.0;
        f.rmsLevel = 0.0;
        f.confidence = 0.0;
        return f;
    }
}

TEST_CASE("NoteSegmenter merges consecutive frames with the same quantized pitch", "[NoteSegmenter]")
{
    std::vector<PitchFrame> frames(6, voicedFrame());

    NoteSegmenter segmenter(makeSettings());
    const auto segments = segmenter.segment(frames, hopSize, sampleRate);

    REQUIRE(segments.size() == 1);
    REQUIRE(segments[0].pitch == 69); // A4
    REQUIRE(segments[0].startFrame == 0);
    REQUIRE(segments[0].lengthFrames == 6);
}

TEST_CASE("NoteSegmenter splits a segment on a frame below the noise-gate RMS threshold", "[NoteSegmenter]")
{
    std::vector<PitchFrame> frames;
    for (int i = 0; i < 5; ++i)
        frames.push_back(voicedFrame(440.0, 0.5));
    frames.push_back(voicedFrame(440.0, 0.01)); // below threshold: breaks the segment
    for (int i = 0; i < 5; ++i)
        frames.push_back(voicedFrame(440.0, 0.5));

    NoteSegmenter segmenter(makeSettings());
    const auto segments = segmenter.segment(frames, hopSize, sampleRate);

    REQUIRE(segments.size() == 2);
    REQUIRE(segments[0].startFrame == 0);
    REQUIRE(segments[0].lengthFrames == 5);
    REQUIRE(segments[1].startFrame == 6);
    REQUIRE(segments[1].lengthFrames == 5);
}

TEST_CASE("NoteSegmenter splits a segment on an unvoiced frame", "[NoteSegmenter]")
{
    std::vector<PitchFrame> frames;
    for (int i = 0; i < 5; ++i)
        frames.push_back(voicedFrame());
    frames.push_back(unvoicedFrame());
    for (int i = 0; i < 5; ++i)
        frames.push_back(voicedFrame());

    NoteSegmenter segmenter(makeSettings());
    const auto segments = segmenter.segment(frames, hopSize, sampleRate);

    REQUIRE(segments.size() == 2);
    REQUIRE(segments[0].lengthFrames == 5);
    REQUIRE(segments[1].lengthFrames == 5);
}

TEST_CASE("NoteSegmenter removes segments shorter than the minimum note length", "[NoteSegmenter]")
{
    std::vector<PitchFrame> frames;
    for (int i = 0; i < 4; ++i) // 4 frames = 0.4s < 0.5s -> should be filtered out
        frames.push_back(voicedFrame());
    frames.push_back(unvoicedFrame()); // gap to force a segment boundary
    for (int i = 0; i < 5; ++i) // 5 frames = 0.5s -> should survive (boundary, inclusive)
        frames.push_back(voicedFrame());

    NoteSegmenter segmenter(makeSettings());
    const auto segments = segmenter.segment(frames, hopSize, sampleRate);

    REQUIRE(segments.size() == 1);
    REQUIRE(segments[0].startFrame == 5);
    REQUIRE(segments[0].lengthFrames == 5);
}
