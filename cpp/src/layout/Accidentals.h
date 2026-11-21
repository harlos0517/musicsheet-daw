#pragma once

// Which accidental a note shows. Common-practice rules:
//  - the key signature sets each step's alteration in every octave;
//  - an accidental lasts until the end of the measure, for that step in that octave;
//  - a note that continues a tie shows none, and doesn't change what later notes need;
//  - Note::accidentalDisplay overrides the result: show, hide, or cautionary (shown in
//    parentheses).

#include <map>
#include <optional>
#include <string>

#include "core/types/MusicData.h"

namespace msd
{

struct AccidentalMark
{
    int alter = 0; // -2 double flat … 2 double sharp; 0 is a natural sign
    bool parentheses = false;
};

// The alteration a key signature gives a step: 1 for F in G major, -1 for B in F major.
int keyAlter(Step step, int keyAccidentals);

// The SMuFL glyph for an alteration, e.g. "accidentalSharp".
std::string accidentalGlyph(int alter);

// Remembers the accidentals of one measure on one staff. Feed it the notes in time
// order; start a new one for each measure.
class MeasureAccidentals
{
public:
    explicit MeasureAccidentals(int keyAccidentals)
        : key(keyAccidentals)
    {}

    // What this note shows, if anything, and remembers its alteration.
    // continuesTie: the note is the target of a tie from an earlier note.
    std::optional<AccidentalMark> next(const Pitch& pitch, std::optional<AccidentalDisplay> display, bool continuesTie);

private:
    int key;
    std::map<int, int> altered; // diatonic step (octave * 7 + step) → alteration in force
};

} // namespace msd
