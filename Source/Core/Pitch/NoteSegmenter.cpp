#include "NoteSegmenter.h"

#include <cmath>

namespace midi_funfun::core
{
    namespace
    {
        int quantizedPitch(double frequencyHz)
        {
            return (int) std::round(69.0 + 12.0 * std::log2(frequencyHz / 440.0));
        }
    }

    NoteSegmenter::NoteSegmenter(Settings settingsIn) : settings(settingsIn)
    {
    }

    std::vector<RawNoteSegment> NoteSegmenter::segment(const std::vector<PitchFrame>& frames, int hopSize, double sampleRate) const
    {
        std::vector<RawNoteSegment> rawSegments;

        bool inSegment = false;
        int currentPitch = 0;
        int segmentStart = 0;

        auto closeSegment = [&](int endFrameExclusive)
        {
            if (!inSegment)
                return;

            RawNoteSegment segment;
            segment.pitch = currentPitch;
            segment.startFrame = segmentStart;
            segment.lengthFrames = endFrameExclusive - segmentStart;
            rawSegments.push_back(segment);
            inSegment = false;
        };

        for (int i = 0; i < (int) frames.size(); ++i)
        {
            const auto& frame = frames[(size_t) i];
            const bool active = frame.voiced && frame.rmsLevel >= settings.noiseGateRmsThreshold;

            if (!active)
            {
                closeSegment(i);
                continue;
            }

            const int pitch = quantizedPitch(frame.frequencyHz);

            if (inSegment && pitch == currentPitch)
                continue; // 区間を延長

            closeSegment(i);
            inSegment = true;
            currentPitch = pitch;
            segmentStart = i;
        }
        closeSegment((int) frames.size());

        std::vector<RawNoteSegment> filtered;
        for (const auto& segment : rawSegments)
        {
            const double lengthSeconds = (double) segment.lengthFrames * (double) hopSize / sampleRate;
            if (lengthSeconds >= settings.minNoteLengthSeconds)
                filtered.push_back(segment);
        }

        return filtered;
    }
}
