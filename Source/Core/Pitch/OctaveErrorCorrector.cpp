#include "OctaveErrorCorrector.h"

#include <cstdlib>

namespace midi_funfun::core
{
    OctaveErrorCorrector::OctaveErrorCorrector(Settings settingsIn) : settings(settingsIn)
    {
    }

    void OctaveErrorCorrector::correct(std::vector<RawNoteSegment>& segments) const
    {
        if (segments.size() < 3)
            return;

        for (size_t i = 1; i + 1 < segments.size(); ++i)
        {
            const int prevPitch = segments[i - 1].pitch;
            const int nextPitch = segments[i + 1].pitch;

            // 前後(prev/next)が安定した文脈を形成しているか。
            if (std::abs(nextPitch - prevPitch) > settings.neighborConsistencyToleranceSemitones)
                continue;

            const int contextPitch = prevPitch;
            const int diff = segments[i].pitch - contextPitch;
            const int absDiff = std::abs(diff);

            const int lower = 12 - settings.octaveToleranceSemitones;
            const int upper = 12 + settings.octaveToleranceSemitones;

            if (absDiff >= lower && absDiff <= upper)
            {
                const int sign = (diff > 0) ? 1 : -1;
                segments[i].pitch -= sign * 12;
            }
        }
    }
}
