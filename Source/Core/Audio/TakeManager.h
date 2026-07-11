#pragma once

#include <cstddef>
#include <vector>

#include <juce_core/juce_core.h>

#include "Take.h"

namespace midi_funfun::core
{
    /**
     * 複数テイクの録音・選択・削除とメモリ予算(既定512MB、1テイク既定5分)を管理する。
     * 予算はテストのため注入可能(既定値は要件通り)。
     * オーディオスレッド(appendToCurrentTake等)とGUIスレッド(テイク一覧の選択・削除)の
     * 両方から呼ばれるため、内部で juce::CriticalSection により排他制御する。
     */
    class TakeManager
    {
    public:
        static constexpr size_t defaultMaxTotalBytes = 512ull * 1024ull * 1024ull;
        static constexpr double defaultMaxTakeSeconds = 5.0 * 60.0;

        explicit TakeManager(size_t maxTotalBytesIn = defaultMaxTotalBytes,
                              double maxTakeSecondsIn = defaultMaxTakeSeconds)
            : maxTotalBytes(maxTotalBytesIn), maxTakeSeconds(maxTakeSecondsIn)
        {
        }

        /** 既存テイク実サイズ合計 + このsampleRateでの新規テイク見込み最大サイズが予算を超えないか。 */
        bool canStartNewTake(double sampleRate) const;

        /** 新規テイクを開始する。予算超過ならテイクを追加せずfalseを返す。 */
        bool startNewTake(double sampleRate);

        /** 現在録音中のテイクへサンプルを追記する。1テイクの最大長に達したらtrueを返す。 */
        bool appendToCurrentTake(const float* samples, int numSamples);

        /** 録音中状態を終える(テイク自体は一覧に残る)。 */
        void finishRecording();

        void deleteTake(int index);
        void selectTake(int index);

        int getSelectedTakeIndex() const;
        int getNumTakes() const;

        /** 指定テイクの録音済み長さ(秒)。GUIスレッドからのテイク一覧表示用。 */
        double getTakeLengthSeconds(int index) const;

        size_t getTotalBytesUsed() const;

    private:
        size_t maxTotalBytes;
        double maxTakeSeconds;

        mutable juce::CriticalSection lock;

        std::vector<Take> takes;
        int selectedIndex = -1;
        int recordingIndex = -1;

        size_t estimatedMaxTakeBytes(double sampleRate) const;
        size_t getTotalBytesUsedUnlocked() const;
    };
}
