#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>

#include "Audio/MonitoringPassthrough.h"

namespace
{
    constexpr float clickFrequencyHz = 1500.0f;
    constexpr float clickAmplitude = 0.6f;
    constexpr float clickDurationSeconds = 0.015f;
}

MidiFunfunAudioProcessor::MidiFunfunAudioProcessor()
    : juce::AudioProcessor(BusesProperties()
                                .withInput("Input", juce::AudioChannelSet::stereo(), true)
                                .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
}

void MidiFunfunAudioProcessor::prepareToPlay(double sampleRate, int)
{
    currentSampleRate = sampleRate;
    peakLevelTracker.prepare(sampleRate);
}

void MidiFunfunAudioProcessor::releaseResources()
{
}

void MidiFunfunAudioProcessor::mixClickAt(juce::AudioBuffer<float>& buffer, int blockOffset, int numSamplesInBlock) const
{
    const int clickLength = std::min(numSamplesInBlock - blockOffset,
                                      static_cast<int>(currentSampleRate * clickDurationSeconds));
    if (clickLength <= 0)
        return;

    const float decayPerSample = std::pow(0.001f, 1.0f / static_cast<float>(clickLength));
    const float phaseIncrement = juce::MathConstants<float>::twoPi * clickFrequencyHz
                                  / static_cast<float>(currentSampleRate);

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* out = buffer.getWritePointer(ch);
        float envelope = clickAmplitude;
        float phase = 0.0f;

        for (int i = 0; i < clickLength; ++i)
        {
            out[blockOffset + i] += envelope * std::sin(phase);
            envelope *= decayPerSample;
            phase += phaseIncrement;
        }
    }
}

void MidiFunfunAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numInputChannels = getTotalNumInputChannels();
    const int numOutputChannels = getTotalNumOutputChannels();

    if (numInputChannels <= 0 || numOutputChannels <= 0)
    {
        buffer.clear();
        return;
    }

    // モノラル前提: 先頭チャンネルをマイク入力として扱う。
    const float* inputData = buffer.getReadPointer(0);

    peakLevelTracker.pushBlock(inputData, numSamples);

    const auto advance = recordingTransport.advance(numSamples);

    if (advance.shouldAppendToTake)
    {
        const bool reachedMaxLength = takeManager.appendToCurrentTake(inputData, numSamples);
        if (reachedMaxLength)
            recordingTransport.stopRecording();
    }

    // モニタリング: 有効時のみ入力(チャンネル0)を全出力チャンネルへパススルーする。
    // 無効時は全チャンネルを無音化する(チャンネル0の入出力バッファエイリアシングによる
    // 暗黙のパススルーも打ち消し、フィードバックループを避けるため)。
    midi_funfun::core::applyMonitoring(buffer.getArrayOfWritePointers(), numOutputChannels, numSamples,
                                        monitoringEnabled.load(std::memory_order_relaxed));

    for (const int offset : advance.clickSampleOffsets)
        if (offset >= 0 && offset < numSamples)
            mixClickAt(buffer, offset, numSamples);
}

bool MidiFunfunAudioProcessor::startRecording()
{
    return recordingTransport.startRecording(metronomeEnabled.load(std::memory_order_relaxed),
                                               countInBeats.load(std::memory_order_relaxed),
                                               bpm.load(std::memory_order_relaxed),
                                               currentSampleRate);
}

void MidiFunfunAudioProcessor::stopRecording()
{
    recordingTransport.stopRecording();
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
