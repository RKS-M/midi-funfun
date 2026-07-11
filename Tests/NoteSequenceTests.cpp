#include <catch2/catch_test_macros.hpp>

#include "Model/NoteSequence.h"

using midi_funfun::core::Note;
using midi_funfun::core::NoteSequence;

TEST_CASE("NoteSequence starts empty", "[NoteSequence]")
{
    NoteSequence seq;
    REQUIRE(seq.size() == 0);
    REQUIRE(seq.getNotes().empty());
}

TEST_CASE("NoteSequence add/size/operator[]/getNotes stay consistent", "[NoteSequence]")
{
    NoteSequence seq;

    Note a { 60, 0, 480, 90 };
    Note b { 64, 480, 240, 100 };

    seq.add(a);
    seq.add(b);

    REQUIRE(seq.size() == 2);
    REQUIRE(seq[0].pitch == 60);
    REQUIRE(seq[1].pitch == 64);
    REQUIRE(seq.getNotes().size() == 2);
    REQUIRE(seq.getNotes()[1].startTick == 480);
}

TEST_CASE("NoteSequence clear empties the sequence", "[NoteSequence]")
{
    NoteSequence seq;
    seq.add(Note { 60, 0, 480, 90 });
    REQUIRE(seq.size() == 1);

    seq.clear();
    REQUIRE(seq.size() == 0);
    REQUIRE(seq.getNotes().empty());
}
