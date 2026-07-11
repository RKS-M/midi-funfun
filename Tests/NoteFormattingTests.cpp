#include <catch2/catch_test_macros.hpp>

#include "Model/NoteFormatting.h"

using midi_funfun::core::formatNoteListRow;

TEST_CASE("formatNoteListRow does not corrupt the pitch name", "[core][model][formatting]")
{
    const auto text = formatNoteListRow(60, 1.5, 0.25); // MIDI 60 -> "C4" (octaveNumForMiddleC=4)

    REQUIRE(text.contains("C4"));

    for (auto c : text)
        REQUIRE((int) c < 128); // 文字化けした場合はASCII範囲外のワイド文字が混入する
}

TEST_CASE("formatNoteListRow handles sharps and includes the timing columns", "[core][model][formatting]")
{
    const auto text = formatNoteListRow(61, 0.0, 1.0); // MIDI 61 -> "C#4"

    REQUIRE(text.contains("C#4"));
    REQUIRE(text.contains("0.00s"));
    REQUIRE(text.contains("1.00s"));

    for (auto c : text)
        REQUIRE((int) c < 128);
}
