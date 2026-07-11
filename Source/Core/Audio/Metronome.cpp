#include "Metronome.h"

#include <cmath>

namespace midi_funfun::core
{
    void Metronome::start(double bpm, double sampleRate, int countInBeats)
    {
        samplesPerBeat = sampleRate * 60.0 / bpm;
        samplePosition = 0;
        countInBeatsTotal = countInBeats;
        beatsElapsed = 0;
        countInActive = countInBeats > 0;
        started = true;
    }

    Metronome::Result Metronome::processBlock(int numSamples)
    {
        Result result;

        if (!started)
            return result;

        const int64_t blockStart = samplePosition;
        const int64_t blockEnd = samplePosition + numSamples;

        for (;;)
        {
            const double boundary = static_cast<double>(beatsElapsed) * samplesPerBeat;
            if (boundary >= static_cast<double>(blockEnd))
                break;

            if (boundary >= static_cast<double>(blockStart))
            {
                const int offset = static_cast<int>(std::llround(boundary - static_cast<double>(blockStart)));
                result.clickSampleOffsets.push_back(offset);
            }

            ++beatsElapsed;

            if (countInActive && beatsElapsed >= countInBeatsTotal)
            {
                countInActive = false;
                result.countInJustCompleted = true;
            }
        }

        samplePosition = blockEnd;
        return result;
    }
}
