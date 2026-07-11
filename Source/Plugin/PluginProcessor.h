#pragma once

#include <atomic>

#include <juce_audio_processors/juce_audio_processors.h>

#include "Audio/Metronome.h"
#include "Audio/PeakLevelTracker.h"
#include "Audio/RecordingTransport.h"
#include "Audio/TakeManager.h"
#include "Model/NoteSequence.h"
#include "Pitch/PitchAnalyzer.h"

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

    /** 入力を出力へパススルーするモニタリング。フィードバックループ防止のため既定でOFF。 */
    void setMonitoringEnabled(bool enabled) { monitoringEnabled.store(enabled, std::memory_order_relaxed); }
    bool getMonitoringEnabled() const { return monitoringEnabled.load(std::memory_order_relaxed); }

    void setCountInBeats(int beats) { countInBeats.store(beats, std::memory_order_relaxed); }
    int getCountInBeats() const { return countInBeats.load(std::memory_order_relaxed); }

    midi_funfun::core::RecordingTransport::State getRecordingState() const { return recordingTransport.getState(); }
    const midi_funfun::core::PeakLevelTracker& getPeakLevelTracker() const { return peakLevelTracker; }
    midi_funfun::core::TakeManager& getTakeManager() { return takeManager; }

    // --- Pitch analysis controls, called from the GUI/message thread ---

    void setNoiseGateSensitivity(double percent) { noiseGateSensitivity.store(percent, std::memory_order_relaxed); }
    double getNoiseGateSensitivity() const { return noiseGateSensitivity.load(std::memory_order_relaxed); }

    void setMinNoteLengthMs(double ms) { minNoteLengthMs.store(ms, std::memory_order_relaxed); }
    double getMinNoteLengthMs() const { return minNoteLengthMs.load(std::memory_order_relaxed); }

    void setDefaultVelocity(int velocity) { defaultVelocity.store(velocity, std::memory_order_relaxed); }
    int getDefaultVelocity() const { return defaultVelocity.load(std::memory_order_relaxed); }

    /** 現在選択中のTakeを解析し、結果をanalyzedNotesへ格納する。選択中テイクが無ければ何もしない。 */
    void analyzeSelectedTake();
    const midi_funfun::core::NoteSequence& getAnalyzedNotes() const { return analyzedNotes; }
    double getAnalyzedNotesBpm() const { return analyzedNotesBpm; }

private:
    midi_funfun::core::PeakLevelTracker peakLevelTracker;
    midi_funfun::core::TakeManager takeManager;
    midi_funfun::core::Metronome metronome;
    midi_funfun::core::RecordingTransport recordingTransport { takeManager, metronome };

    std::atomic<double> bpm { 120.0 };
    std::atomic<bool> metronomeEnabled { true };
    std::atomic<int> countInBeats { 4 };
    std::atomic<bool> monitoringEnabled { false };

    std::atomic<double> noiseGateSensitivity { 50.0 }; // 0-100%
    std::atomic<double> minNoteLengthMs { 60.0 };
    std::atomic<int> defaultVelocity { 90 };

    midi_funfun::core::NoteSequence analyzedNotes;
    double analyzedNotesBpm = 120.0; // analyzeSelectedTake()実行時点のBPM(メッセージスレッド専用、GUIから直接読むだけなのでatomic不要)

    double currentSampleRate = 44100.0;

    void mixClickAt(juce::AudioBuffer<float>& buffer, int blockOffset, int numSamplesInBlock) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiFunfunAudioProcessor)
};
