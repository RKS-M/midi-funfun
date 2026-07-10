#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"

class MidiFunfunAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit MidiFunfunAudioProcessorEditor(MidiFunfunAudioProcessor&);
    ~MidiFunfunAudioProcessorEditor() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    MidiFunfunAudioProcessor& processorRef;
    juce::Label titleLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiFunfunAudioProcessorEditor)
};
