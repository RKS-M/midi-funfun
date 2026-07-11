#pragma once

namespace midi_funfun::core
{
    // ノイズゲート感度(UIスライダー0-100%)をNoteSegmenterのRMSしきい値へ線形マッピングする。
    // 0%->0.0(ゲートしない)、100%->0.2(YinPitchDetectorのRMSは概ね0-1程度のオーディオ
    // 振幅を想定しており、0.2は十分強いゲート)。
    constexpr double noiseGateSensitivityMaxRmsThreshold = 0.2;

    inline double noiseGateSensitivityPercentToThreshold(double percent)
    {
        return (percent / 100.0) * noiseGateSensitivityMaxRmsThreshold;
    }

    // NoteSegmenter::Settingsの既定しきい値(0.02、設計スペック記載値)に一致する既定感度(%)。
    constexpr double defaultNoiseGateSensitivityPercent = 10.0;
}
