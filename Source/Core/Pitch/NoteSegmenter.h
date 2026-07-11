#pragma once

#include <vector>

#include "YinPitchDetector.h"

namespace midi_funfun::core
{
    struct RawNoteSegment
    {
        int pitch = 0;       // MIDIノート番号(半音量子化済み)
        int startFrame = 0;  // PitchFrame列内のインデックス
        int lengthFrames = 0;
    };

    /**
     * フレーム単位のピッチ曲線を、半音量子化・ノイズゲート・最小ノート長フィルタを
     * 経てノート区間列に変換する。
     */
    class NoteSegmenter
    {
    public:
        struct Settings
        {
            double noiseGateRmsThreshold = 0.02; // これ未満のRMSは無音/ノイズ扱い(UIの「ノイズゲート感度」スライダーから設定)
            double minNoteLengthSeconds = 0.06;  // これ未満の長さのノート区間は除去(UIの「最小ノート長」スライダーから設定)
        };

        explicit NoteSegmenter(Settings settingsIn = {});

        std::vector<RawNoteSegment> segment(const std::vector<PitchFrame>& frames, int hopSize, double sampleRate) const;

    private:
        Settings settings;
    };
}
