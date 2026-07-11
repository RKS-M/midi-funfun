#pragma once

#include <atomic>

#include <juce_audio_processors/juce_audio_processors.h>

#include "Audio/Metronome.h"
#include "Audio/PeakLevelTracker.h"
#include "Audio/RecordingTransport.h"
#include "Audio/TakeManager.h"

class MidiFunfunAudioProcessor final : public juce::AudioProcessor
{
public:
    MidiFunfunAudioProcessor();
    ~MidiFunfunAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // --- Recording controls, called from the GUI/message thread ---

    /** TakeManagerの予算チェックに失敗、または既に録音中ならfalseを返す。 */
    bool startRecording();
    void stopRecording();

    void setBpm(double newBpm) { bpm.store(newBpm, std::memory_order_relaxed); }
    double getBpm() const { return bpm.load(std::memory_order_relaxed); }

    void setMetronomeEnabled(bool enabled) { metronomeEnabled.store(enabled, std::memory_order_relaxed); }
    bool getMetronomeEnabled() const { return metronomeEnabled.load(std::memory_order_relaxed); }

    void setCountInBeats(int beats) { countInBeats.store(beats, std::memory_order_relaxed); }
    int getCountInBeats() const { return countInBeats.load(std::memory_order_relaxed); }

    midi_funfun::core::RecordingTransport::State getRecordingState() const { return recordingTransport.getState(); }
    const midi_funfun::core::PeakLevelTracker& getPeakLevelTracker() const { return peakLevelTracker; }
    midi_funfun::core::TakeManager& getTakeManager() { return takeManager; }

private:
    midi_funfun::core::PeakLevelTracker peakLevelTracker;
    midi_funfun::core::TakeManager takeManager;
    midi_funfun::core::Metronome metronome;
    midi_funfun::core::RecordingTransport recordingTransport { takeManager, metronome };

    std::atomic<double> bpm { 120.0 };
    std::atomic<bool> metronomeEnabled { true };
    std::atomic<int> countInBeats { 4 };

    double currentSampleRate = 44100.0;

    void mixClickAt(juce::AudioBuffer<float>& buffer, int blockOffset, int numSamplesInBlock) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiFunfunAudioProcessor)
};
