#include <cmath>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <juce_core/juce_core.h>

#include "Pitch/YinPitchDetector.h"

using midi_funfun::core::PitchFrame;
using midi_funfun::core::YinPitchDetector;

namespace
{
    constexpr double sampleRate = 44100.0;

    std::vector<float> makeSineWave(double frequencyHz, double durationSeconds)
    {
        const int numSamples = (int) (durationSeconds * sampleRate);
        std::vector<float> samples((size_t) numSamples, 0.0f);
        const double phaseIncrement = 2.0 * juce::MathConstants<double>::pi * frequencyHz / sampleRate;
        double phase = 0.0;
        for (int i = 0; i < numSamples; ++i)
        {
            samples[(size_t) i] = (float) (0.5 * std::sin(phase));
            phase += phaseIncrement;
        }
        return samples;
    }

    double centsDifference(double detectedHz, double expectedHz)
    {
        return 1200.0 * std::log2(detectedHz / expectedHz);
    }
}

TEST_CASE("YinPitchDetector detects known frequencies within 10 cents", "[YinPitchDetector]")
{
    const std::vector<double> testFrequencies { 82.41, 220.00, 440.00, 659.25 }; // E2, A3, A4, E5

    for (const double expectedHz : testFrequencies)
    {
        const auto samples = makeSineWave(expectedHz, 1.0);

        YinPitchDetector detector;
        const auto frames = detector.analyze(samples.data(), (int) samples.size(), sampleRate);

        REQUIRE_FALSE(frames.empty());

        bool anyVoiced = false;
        for (const auto& frame : frames)
        {
            if (!frame.voiced)
                continue;
            anyVoiced = true;
            const double cents = std::abs(centsDifference(frame.frequencyHz, expectedHz));
            REQUIRE(cents <= 10.0);
        }
        REQUIRE(anyVoiced);
    }
}

TEST_CASE("YinPitchDetector reports unvoiced for silence", "[YinPitchDetector]")
{
    const std::vector<float> silence((size_t) sampleRate, 0.0f); // 1 second of silence

    YinPitchDetector detector;
    const auto frames = detector.analyze(silence.data(), (int) silence.size(), sampleRate);

    REQUIRE_FALSE(frames.empty());
    for (const auto& frame : frames)
    {
        REQUIRE_FALSE(frame.voiced);
        REQUIRE(frame.frequencyHz == 0.0);
    }
}
