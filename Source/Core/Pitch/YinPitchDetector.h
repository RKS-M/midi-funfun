#pragma once

#include <vector>

namespace midi_funfun::core
{
    struct PitchFrame
    {
        double frequencyHz = 0.0; // 0 = unvoiced(有意なピッチなし)
        double confidence = 0.0;  // 1 - CMNDF最小値(周期性の確からしさ、0〜1)
        double rmsLevel = 0.0;    // フレームのRMS振幅(線形、0〜1程度)。NoteSegmenterのノイズゲートで使用
        bool voiced = false;      // YINの絶対しきい値内で明確な周期が見つかったか
    };

    /**
     * 自前実装のYINアルゴリズム。モノラル音声バッファ全体を固定フレーム長・固定ホップ長で
     * 走査し、フレームごとのピッチ推定値を返す。オフラインバッチ処理専用(リアルタイム制約なし)。
     */
    class YinPitchDetector
    {
    public:
        struct Settings
        {
            int windowSize = 2048;          // 約46ms @ 44.1kHz
            int hopSize = 512;               // 約11.6ms @ 44.1kHz(フレームレート ~86fps)
            double absoluteThreshold = 0.15; // YIN内部のCMNDFディップしきい値(内部固定、ユーザー非公開)
            double minFrequencyHz = 70.0;    // 探索範囲下限(E2 ~82Hzに余裕を持たせる)
            double maxFrequencyHz = 1000.0;  // 探索範囲上限
        };

        explicit YinPitchDetector(Settings settingsIn = {});

        /** monoSamples全体をhopSizeごとに走査し、フレーム単位のPitchFrame列を返す。 */
        std::vector<PitchFrame> analyze(const float* monoSamples, int numSamples, double sampleRate) const;

    private:
        Settings settings;
    };
}
