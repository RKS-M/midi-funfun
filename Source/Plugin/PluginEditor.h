#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "Audio/PeakLevelTracker.h"
#include "Model/NoteSequence.h"
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
    void analyzeButtonClicked();
    void updateRecordButtonAppearance();

    class LevelMeter final : public juce::Component
    {
    public:
        explicit LevelMeter(const midi_funfun::core::PeakLevelTracker& trackerIn) : tracker(trackerIn) {}
        void paint(juce::Graphics& g) override;

    private:
        const midi_funfun::core::PeakLevelTracker& tracker;
    };

    /** 検出ノートの最小限の一覧表示(読み取り専用)。Milestone 3で本実装のピアノロールに置き換え。 */
    class NoteListBoxModel final : public juce::ListBoxModel
    {
    public:
        explicit NoteListBoxModel(MidiFunfunAudioProcessor& processorIn) : processor(processorIn) {}

        int getNumRows() override;
        void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;

    private:
        MidiFunfunAudioProcessor& processor;
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

    juce::TextButton analyzeButton;

    juce::Label noiseGateLabel;
    juce::Slider noiseGateSlider;

    juce::Label minNoteLengthLabel;
    juce::Slider minNoteLengthSlider;

    juce::Label defaultVelocityLabel;
    juce::Slider defaultVelocitySlider;

    NoteListBoxModel noteListBoxModel { processorRef };
    juce::ListBox noteListBox { "Notes", &noteListBoxModel };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiFunfunAudioProcessorEditor)
};
