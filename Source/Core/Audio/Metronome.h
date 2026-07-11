#pragma once

#include <cstdint>
#include <vector>

namespace midi_funfun::core
{
    /**
     * BPM・サンプルレート・現在の再生サンプル位置から、メトロノームクリックと
     * カウントインのタイミングを計算する純粋ロジック(拍子は4/4固定)。
     */
    class Metronome
    {
    public:
        struct Result
        {
            std::vector<int> clickSampleOffsets; // ブロック先頭からのオフセット(サンプル単位)
            bool countInJustCompleted = false; // このブロックでカウントインが完了し本番へ移行すべきか(一度きり)
        };

        /** カウントイン拍数0なら即座に「本番」扱い(以後 processBlock はcountInJustCompletedを返さない)。 */
        void start(double bpm, double sampleRate, int countInBeats);

        /** 毎ブロック呼ぶ。start()が呼ばれていなければ空の結果を返す。 */
        Result processBlock(int numSamples);

        bool isCountInActive() const { return countInActive; }

    private:
        double samplesPerBeat = 0.0;
        int64_t samplePosition = 0;
        int countInBeatsTotal = 0;
        int beatsElapsed = 0;
        bool countInActive = false;
        bool started = false;
    };
}
