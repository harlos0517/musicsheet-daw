#pragma once

// Written pitch for transposing instruments. MusicData keeps sounding (concert) pitch;
// a B♭ clarinet's part is written a major second higher, with its key moved to match.
// Track::transposition goes from written to sounding, so these apply it backwards.

#include <optional>

#include "core/types/MusicData.h"

namespace msd
{

// The pitch as written for an instrument with this transposition.
Pitch writtenPitch(const Pitch& sounding, const std::optional<Transposition>& transposition);

// The key signature (sharps > 0, flats < 0) as written for that instrument; kept
// within 7 sharps or flats by its enharmonic equivalent.
int writtenKey(int soundingAccidentals, const std::optional<Transposition>& transposition);

} // namespace msd
