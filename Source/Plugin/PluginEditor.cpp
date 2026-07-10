#include "PluginEditor.h"

namespace
{
    // 「青空」を意識した水色系の背景色(要件5章 UI/デザイン要件)。Milestone 7で最終調整する。
    const juce::Colour skyBlueBackground { 0xff8ecae6 };
}

MidiFunfunAudioProcessorEditor::MidiFunfunAudioProcessorEditor(MidiFunfunAudioProcessor& p)
    : juce::AudioProcessorEditor(&p), processorRef(p)
{
    titleLabel.setText("MIDI FUNFUN", juce::dontSendNotification);
    titleLabel.setJustificationType(juce::Justification::centred);
    titleLabel.setFont(juce::Font(juce::FontOptions().withHeight(24.0f)));
    addAndMakeVisible(titleLabel);

    setSize(600, 400);
}

void MidiFunfunAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(skyBlueBackground);
}

void MidiFunfunAudioProcessorEditor::resized()
{
    titleLabel.setBounds(getLocalBounds());
}
