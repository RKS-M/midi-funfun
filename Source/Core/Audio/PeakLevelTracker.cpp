#include "PeakLevelTracker.h"

#include <algorithm>
#include <cmath>

namespace midi_funfun::core
{
    namespace
    {
        constexpr double holdDurationSeconds = 1.5;
        constexpr double decayDurationSeconds = 0.5;
        constexpr float decayFloorRatio = 0.001f; // 減衰窓の終わりまでにピークをこの比率まで落とす
    }

    void PeakLevelTracker::prepare(double sampleRate)
    {
        holdSamplesTotal = static_cast<int64_t>(sampleRate * holdDurationSeconds);
        holdSamplesRemaining = 0;

        const double decaySamples = std::max(1.0, sampleRate * decayDurationSeconds);
        decayPerSample = std::pow(decayFloorRatio, static_cast<float>(1.0 / decaySamples));
    }

    void PeakLevelTracker::pushBlock(const float* samples, int numSamples)
    {
        float blockPeak = 0.0f;
        for (int i = 0; i < numSamples; ++i)
            blockPeak = std::max(blockPeak, std::abs(samples[i]));

        currentLevel.store(blockPeak, std::memory_order_relaxed);

        const float previousHold = peakHoldLevel.load(std::memory_order_relaxed);

        if (blockPeak >= previousHold)
        {
            peakHoldLevel.store(blockPeak, std::memory_order_relaxed);
            holdSamplesRemaining = holdSamplesTotal;
            return;
        }

        if (holdSamplesRemaining >= numSamples)
        {
            holdSamplesRemaining -= numSamples;
            return;
        }

        // The hold window ends partway through (or before) this block; decay for
        // however many samples remain after it expires.
        const int64_t samplesDecaying = numSamples - holdSamplesRemaining;
        holdSamplesRemaining = 0;

        const float decayed = previousHold * std::pow(decayPerSample, static_cast<float>(samplesDecaying));
        peakHoldLevel.store(std::max(decayed, blockPeak), std::memory_order_relaxed);
    }
}
