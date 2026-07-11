#pragma once

#include <algorithm>

namespace midi_funfun::core
{
    /**
     * モニタリング(入力の出力へのパススルー)を出力チャンネル配列に適用する。
     * channel 0 は呼び出し側で既に入力値が入っている前提(入出力同一バッファ)。
     * monitoringEnabled が false の場合は全出力チャンネルを無音化する
     * (channel 0 のバッファエイリアシングによる暗黙のパススルーも打ち消すため)。
     */
    inline void applyMonitoring(float* const* outputChannelData, int numOutputChannels, int numSamples, bool monitoringEnabled)
    {
        if (numOutputChannels <= 0 || numSamples <= 0)
            return;

        if (monitoringEnabled)
        {
            const float* input = outputChannelData[0];
            for (int ch = 1; ch < numOutputChannels; ++ch)
                std::copy(input, input + numSamples, outputChannelData[ch]);
        }
        else
        {
            for (int ch = 0; ch < numOutputChannels; ++ch)
                std::fill(outputChannelData[ch], outputChannelData[ch] + numSamples, 0.0f);
        }
    }
}
