#pragma once

#include "../Audio/Take.h"
#include "../Model/NoteSequence.h"
#include "NoteSegmenter.h"
#include "OctaveErrorCorrector.h"
#include "YinPitchDetector.h"

namespace midi_funfun::core
{
    /**
     * 解析の唯一のエントリポイント。RecordingTransportが録音の状態遷移をまとめる役割に
     * 対応する、解析側のオーケストレータ。
     */
    class PitchAnalyzer
    {
    public:
        struct Settings
        {
            YinPitchDetector::Settings yin;
            NoteSegmenter::Settings segmenter;
            OctaveErrorCorrector::Settings octaveCorrection;
            double bpm = 120.0;
            int defaultVelocity = 90;
        };

        explicit PitchAnalyzer(Settings settingsIn = {});

        /** Take(モノラル録音バッファ)を解析し、tick単位・デフォルトベロシティ適用済みのNoteSequenceを返す。 */
        NoteSequence analyze(const Take& take) const;

    private:
        Settings settings;
    };
}
