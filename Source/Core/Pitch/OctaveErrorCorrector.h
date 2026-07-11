#pragma once

#include <vector>

#include "NoteSegmenter.h"

namespace midi_funfun::core
{
    /**
     * 孤立した単発オクターブジャンプを検出し、周辺音高に合わせて補正する。
     * しきい値は要件通り内部固定。
     */
    class OctaveErrorCorrector
    {
    public:
        struct Settings
        {
            int octaveToleranceSemitones = 1;             // 「ちょうど1オクターブ差」とみなす許容幅(12±1半音)
            int neighborConsistencyToleranceSemitones = 1; // 前後のノートが「安定した文脈」とみなせる一致許容幅
        };

        explicit OctaveErrorCorrector(Settings settingsIn = {});

        /** segments を破壊的に補正する。 */
        void correct(std::vector<RawNoteSegment>& segments) const;

    private:
        Settings settings;
    };
}
