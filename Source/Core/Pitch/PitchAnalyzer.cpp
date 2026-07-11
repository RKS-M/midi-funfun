#include "PitchAnalyzer.h"

#include <cmath>

namespace midi_funfun::core
{
    namespace
    {
        juce::int64 ticksFromSamplePos(juce::int64 samplePos, double sampleRate, double bpm)
        {
            const double seconds = (double) samplePos / sampleRate;
            return (juce::int64) std::round(seconds * (bpm / 60.0) * (double) ticksPerQuarterNote);
        }
    }

    PitchAnalyzer::PitchAnalyzer(Settings settingsIn) : settings(settingsIn)
    {
    }

    NoteSequence PitchAnalyzer::analyze(const Take& take) const
    {
        NoteSequence result;

        if (take.numSamplesRecorded <= 0 || take.sampleRate <= 0.0)
            return result;

        YinPitchDetector yin(settings.yin);
        const auto frames = yin.analyze(take.buffer.getReadPointer(0), take.numSamplesRecorded, take.sampleRate);

        NoteSegmenter segmenter(settings.segmenter);
        auto rawSegments = segmenter.segment(frames, settings.yin.hopSize, take.sampleRate);

        OctaveErrorCorrector corrector(settings.octaveCorrection);
        corrector.correct(rawSegments);

        for (const auto& segment : rawSegments)
        {
            const juce::int64 startSamplePos = (juce::int64) segment.startFrame * (juce::int64) settings.yin.hopSize;
            const juce::int64 endSamplePos = (juce::int64) (segment.startFrame + segment.lengthFrames)
                                              * (juce::int64) settings.yin.hopSize;

            const juce::int64 startTick = ticksFromSamplePos(startSamplePos, take.sampleRate, settings.bpm);
            const juce::int64 endTick = ticksFromSamplePos(endSamplePos, take.sampleRate, settings.bpm);

            Note note;
            note.pitch = segment.pitch;
            note.startTick = startTick;
            note.lengthTicks = endTick - startTick;
            note.velocity = settings.defaultVelocity;

            result.add(note);
        }

        return result;
    }
}
