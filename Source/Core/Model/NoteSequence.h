#pragma once

#include <vector>

#include "Note.h"

namespace midi_funfun::core
{
    /** std::vector<Note> の薄いラッパー。Milestone 3でUndo/Redo対応の編集操作が
     *  この上に載る想定だが、本マイルストーンでは読み取り専用の入れ物として使う。 */
    class NoteSequence
    {
    public:
        void clear();
        void add(const Note& note);
        int size() const;
        const Note& operator[](int index) const;
        const std::vector<Note>& getNotes() const;

    private:
        std::vector<Note> notes;
    };
}
