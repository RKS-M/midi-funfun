#pragma once

#include <atomic>
#include <cstdint>

namespace midi_funfun::core
{
    /**
     * ロックフリーなピークレベルトラッカー。オーディオスレッドから pushBlock() を、
     * GUIスレッドから getCurrentLevel()/getPeakHoldLevel() を呼ぶ想定。
     * ピークホールドは prepare() で設定したサンプルレートから逆算した
     * 一定時間(既定1.5秒相当のサンプル数)は保持し、その後緩やかに減衰する。
     */
    class PeakLevelTracker
    {
    public:
        PeakLevelTracker() { prepare(48000.0); }

        /** サンプルレートに応じてホールド時間・減衰係数を再計算する。オーディオスレッド開始前に呼ぶ。 */
        void prepare(double sampleRate);

        /** オーディオスレッドから呼ぶ。ブロック内の最大絶対値でcurrent/peakHoldを更新する。 */
        void pushBlock(const float* samples, int numSamples);

        /** GUIスレッドから呼ぶ。直近ブロックの絶対値ピーク。 */
        float getCurrentLevel() const { return currentLevel.load(std::memory_order_relaxed); }

        /** GUIスレッドから呼ぶ。ホールド中/減衰中のピーク値。 */
        float getPeakHoldLevel() const { return peakHoldLevel.load(std::memory_order_relaxed); }

    private:
        std::atomic<float> currentLevel { 0.0f };
        std::atomic<float> peakHoldLevel { 0.0f };

        int64_t holdSamplesRemaining = 0;
        int64_t holdSamplesTotal = 0;
        float decayPerSample = 1.0f;
    };
}
