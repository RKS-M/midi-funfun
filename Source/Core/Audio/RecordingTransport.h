#pragma once

#include <vector>

#include "Metronome.h"
#include "TakeManager.h"

namespace midi_funfun::core
{
    /**
     * Metronome と TakeManager を用いて Idle -> (CountIn ->) Recording -> Idle の
     * 状態遷移を管理する。実際のオーディオサンプルの追記(TakeManager::appendToCurrentTake)は
     * 呼び出し側(PluginProcessor)が advance() の結果を見て行う。
     */
    class RecordingTransport
    {
    public:
        enum class State
        {
            Idle,
            CountIn,
            Recording
        };

        struct Advance
        {
            State state = State::Idle;
            bool shouldAppendToTake = false;
            std::vector<int> clickSampleOffsets;
        };

        RecordingTransport(TakeManager& takeManagerIn, Metronome& metronomeIn)
            : takeManager(takeManagerIn), metronome(metronomeIn)
        {
        }

        /** TakeManagerのメモリ予算チェックに失敗、または既に録音中ならfalseを返す。 */
        bool startRecording(bool metronomeEnabledIn, int countInBeats, double bpm, double sampleRate);

        /** 毎ブロック呼ぶ。 */
        Advance advance(int numSamples);

        /** 手動停止、または呼び出し側が5分到達を検知した際に呼ぶ。 */
        void stopRecording();

        State getState() const { return state; }

    private:
        TakeManager& takeManager;
        Metronome& metronome;
        State state = State::Idle;
        bool metronomeEnabled = false;
    };
}
