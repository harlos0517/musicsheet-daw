#pragma once

// Where things sit vertically on a staff. Every y is in sp from the top staff line,
// down positive: on a 5-line staff the lines are at y = 0, 1, 2, 3, 4.

#include <vector>

#include "core/types/MusicData.h"

namespace msd
{

// The middle line (or the middle space, for an even number of lines).
double middleLineY(int lines);

// Where the clef glyph's origin goes: on its line (Clef::line counts from the
// bottom line, 1), or the middle for a percussion clef.
double clefY(const Clef& clef, int lines);

// The notehead of a written pitch under a clef. A percussion clef reads pitches
// like a treble clef.
double pitchY(const Pitch& pitch, const Clef& clef, int lines);

// The notehead of an unpitched note: displayPitch steps above the middle line.
double unpitchedY(int displayPitch, int lines);

// One y per accidental of a key signature, in order (F C G D A E B for sharps,
// B E A D G C F for flats). Standard positions for treble, bass, alto and tenor
// clefs; other clefs get the same pattern moved to their lines.
std::vector<double> keySignatureYs(int accidentals, const Clef& clef, int lines);

} // namespace msd
