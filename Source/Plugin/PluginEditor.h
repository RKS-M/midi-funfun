#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "Audio/PeakLevelTracker.h"
#include "PluginProcessor.h"

class MidiFunfunAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                              private juce::Timer,
                                              private juce::ListBoxModel
{
public:
    explicit MidiFunfunAudioProcessorEditor(MidiFunfunAudioProcessor&);
    ~MidiFunfunAudioProcessorEditor() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    // juce::ListBoxModel
    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
    void selectedRowsChanged(int lastRowSelected) override;

    void timerCallback() override;

    void recordButtonClicked();
    void deleteButtonClicked();
    void settingsButtonClicked();
    void updateRecordButtonAppearance();

    class LevelMeter final : public juce::Component
    {
    public:
        explicit LevelMeter(const midi_funfun::core::PeakLevelTracker& trackerIn) : tracker(trackerIn) {}
        void paint(juce::Graphics& g) override;

    private:
        const midi_funfun::core::PeakLevelTracker& tracker;
    };

    MidiFunfunAudioProcessor& processorRef;

    juce::Label titleLabel;

    juce::Label bpmLabel;
    juce::Slider bpmSlider;

    juce::ToggleButton metronomeToggle;
    juce::ToggleButton monitoringToggle;

    juce::Label countInLabel;
    juce::Slider countInSlider;

    juce::TextButton recordButton;

    LevelMeter levelMeter;

    juce::ListBox takeListBox { "Takes", this };
    juce::TextButton deleteTakeButton;
    juce::TextButton settingsButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiFunfunAudioProcessorEditor)
};
