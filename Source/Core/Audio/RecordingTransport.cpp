#include "RecordingTransport.h"

namespace midi_funfun::core
{
    bool RecordingTransport::startRecording(bool metronomeEnabledIn, int countInBeats, double bpm, double sampleRate)
    {
        if (state != State::Idle)
            return false;

        if (!takeManager.startNewTake(sampleRate))
            return false;

        metronomeEnabled = metronomeEnabledIn;

        if (metronomeEnabled)
        {
            metronome.start(bpm, sampleRate, countInBeats);
            state = countInBeats > 0 ? State::CountIn : State::Recording;
        }
        else
        {
            state = State::Recording;
        }

        return true;
    }

    RecordingTransport::Advance RecordingTransport::advance(int numSamples)
    {
        Advance result;

        if (state == State::Idle)
            return result;

        if (metronomeEnabled)
        {
            const auto metronomeResult = metronome.processBlock(numSamples);
            result.clickSampleOffsets = metronomeResult.clickSampleOffsets;

            if (state == State::CountIn && metronomeResult.countInJustCompleted)
                state = State::Recording;
        }

        result.state = state;
        result.shouldAppendToTake = (state == State::Recording);
        return result;
    }

    void RecordingTransport::stopRecording()
    {
        if (state == State::Idle)
            return;

        takeManager.finishRecording();
        state = State::Idle;
    }
}
