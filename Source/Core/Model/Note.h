#pragma once

#include <juce_core/juce_core.h>

namespace midi_funfun::core
{
    // 要件4.8で確定しているPPQ(480)をここで先取りして使用する。
    // MIDIファイル書き出し自体はMilestone 6のスコープだが、ノートの時間表現は
    // 本マイルストーンで tick 単位に統一しておく。
    constexpr int ticksPerQuarterNote = 480;

    struct Note
    {
        int pitch = 0;              // MIDIノート番号(0-127)
        juce::int64 startTick = 0;
        juce::int64 lengthTicks = 0;
        int velocity = 90;           // 要件4.7の既定値
    };
}
