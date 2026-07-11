#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace midi_funfun::core
{
    /** モノラル録音1テイク分のデータを保持する値オブジェクト。 */
    struct Take
    {
        juce::AudioBuffer<float> buffer; // capacity may exceed numSamplesRecorded (amortized growth)
        double sampleRate = 0.0;
        int numSamplesRecorded = 0;
    };
}
