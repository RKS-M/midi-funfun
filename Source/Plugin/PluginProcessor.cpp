#include "PluginProcessor.h"
#include "PluginEditor.h"

MidiFunfunAudioProcessor::MidiFunfunAudioProcessor()
    : juce::AudioProcessor(BusesProperties()
                                .withInput("Input", juce::AudioChannelSet::stereo(), true)
                                .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
}

void MidiFunfunAudioProcessor::prepareToPlay(double, int)
{
}

void MidiFunfunAudioProcessor::releaseResources()
{
}

void MidiFunfunAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
}

juce::AudioProcessorEditor* MidiFunfunAudioProcessor::createEditor()
{
    return new MidiFunfunAudioProcessorEditor(*this);
}

bool MidiFunfunAudioProcessor::hasEditor() const
{
    return true;
}

const juce::String MidiFunfunAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool MidiFunfunAudioProcessor::acceptsMidi() const
{
    return false;
}

bool MidiFunfunAudioProcessor::producesMidi() const
{
    return false;
}

bool MidiFunfunAudioProcessor::isMidiEffect() const
{
    return false;
}

double MidiFunfunAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int MidiFunfunAudioProcessor::getNumPrograms()
{
    return 1;
}

int MidiFunfunAudioProcessor::getCurrentProgram()
{
    return 0;
}

void MidiFunfunAudioProcessor::setCurrentProgram(int)
{
}

const juce::String MidiFunfunAudioProcessor::getProgramName(int)
{
    return {};
}

void MidiFunfunAudioProcessor::changeProgramName(int, const juce::String&)
{
}

void MidiFunfunAudioProcessor::getStateInformation(juce::MemoryBlock&)
{
    // MVP時点では永続化する状態を持たない。ホストからの保存要求に対して
    // クラッシュしない安全な空実装(要件6.1)。Milestone 7で再確認する。
}

void MidiFunfunAudioProcessor::setStateInformation(const void*, int)
{
    // 復元すべき状態がなくても安全に無視する(要件6.1)。
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MidiFunfunAudioProcessor();
}
