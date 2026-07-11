#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace midi_funfun::core
{
    /**
     * ノート一覧パネルの1行分のテキストを生成する。
     * juce::String::formatted の "%s" はWindows上ではワイド文字列を期待するため、
     * UTF-8のconst char*(toRawUTF8()など)を直接渡すと2バイトずつワイド文字として
     * 誤解釈され文字化けする。音高名はjuce::Stringのまま連結し、"%s"へは通さない。
     */
    inline juce::String formatNoteListRow(int pitch, double startSeconds, double lengthSeconds)
    {
        const auto pitchName = juce::MidiMessage::getMidiNoteName(pitch, true, true, 4);
        return pitchName.paddedRight(' ', 4) + juce::String::formatted(" %6.2fs %6.2fs", startSeconds, lengthSeconds);
    }
}
