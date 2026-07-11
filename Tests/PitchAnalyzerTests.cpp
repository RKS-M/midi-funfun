#include <cmath>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <juce_core/juce_core.h>

#include "Audio/Take.h"
#include "Pitch/PitchAnalyzer.h"
#include "Pitch/YinPitchDetector.h"

using midi_funfun::core::Note;
using midi_funfun::core::PitchAnalyzer;
using midi_funfun::core::Take;
using midi_funfun::core::ticksPerQuarterNote;
using midi_funfun::core::YinPitchDetector;

TEST_CASE("PitchAnalyzer converts a synthetic sine-wave take into a single expected note", "[PitchAnalyzer]")
{
    constexpr double sampleRate = 44100.0;
    constexpr double frequencyHz = 440.0; // A4 -> MIDI note 69
    constexpr double durationSeconds = 1.0;
    constexpr double bpm = 120.0;

    const int numSamples = (int) (durationSeconds * sampleRate);

    Take take;
    take.sampleRate = sampleRate;
    take.numSamplesRecorded = numSamples;
    take.buffer.setSize(1, numSamples);

    auto* writePtr = take.buffer.getWritePointer(0);
    const double phaseIncrement = 2.0 * juce::MathConstants<double>::pi * frequencyHz / sampleRate;
    double phase = 0.0;
    for (int i = 0; i < numSamples; ++i)
    {
        writePtr[i] = (float) (0.5 * std::sin(phase));
        phase += phaseIncrement;
    }

    PitchAnalyzer::Settings settings;
    settings.bpm = bpm;
    settings.defaultVelocity = 100;

    PitchAnalyzer analyzer(settings);
    const auto notes = analyzer.analyze(take);

    REQUIRE(notes.size() == 1);

    const Note& note = notes[0];
    REQUIRE(note.pitch == 69); // A4
    REQUIRE(note.velocity == 100);
    REQUIRE(note.startTick == 0);

    // 期待されるフレーム数から、既知のBPM/サンプルレートで長さ(tick)を逆算する。
    const YinPitchDetector::Settings yinDefaults;
    const int expectedFrameCount = (numSamples - yinDefaults.windowSize) / yinDefaults.hopSize + 1;
    const juce::int64 expectedEndSample = (juce::int64) expectedFrameCount * (juce::int64) yinDefaults.hopSize;
    const double expectedLengthSeconds = (double) expectedEndSample / sampleRate;
    const auto expectedLengthTicks = (juce::int64) std::round(expectedLengthSeconds * (bpm / 60.0) * (double) ticksPerQuarterNote);

    REQUIRE(note.lengthTicks == expectedLengthTicks);
}

TEST_CASE("PitchAnalyzer returns an empty sequence for an empty take", "[PitchAnalyzer]")
{
    Take take;
    take.sampleRate = 44100.0;
    take.numSamplesRecorded = 0;
    take.buffer.setSize(1, 0);

    PitchAnalyzer analyzer;
    const auto notes = analyzer.analyze(take);

    REQUIRE(notes.size() == 0);
}
