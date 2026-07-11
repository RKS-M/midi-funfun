#include "NoteSequence.h"

namespace midi_funfun::core
{
    void NoteSequence::clear()
    {
        notes.clear();
    }

    void NoteSequence::add(const Note& note)
    {
        notes.push_back(note);
    }

    int NoteSequence::size() const
    {
        return (int) notes.size();
    }

    const Note& NoteSequence::operator[](int index) const
    {
        return notes[(size_t) index];
    }

    const std::vector<Note>& NoteSequence::getNotes() const
    {
        return notes;
    }
}
