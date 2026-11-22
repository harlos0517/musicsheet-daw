#pragma once

// Swells: the small crescendo into and diminuendo out of a held note that players add
// without it being written. Ported from prototype-web/src/core/playback/swells.ts.

#include <string>
#include <unordered_map>
#include <vector>

#include "core/types/MusicEvent.h"
#include "playback/TrackContext.h"

namespace msd
{

// A long single note gets a swell of its own; a slur gets one for the whole phrase,
// rising over its first note and falling over its last. They are Scale segments on the
// dynamics curve, so they shape the level written there and leave velocities alone.
// Where the page itself changes the dynamic (a hairpin, or a new dynamic inside the
// note), the page wins: the rise or the fall that would overlap it is left out.
// Nothing without the profile's swell settings.
//
// `phrases`: the phrase each note is in as written (from buildNotes); a slur that
// playing splits at a repeated pitch is still one.
std::vector<CurveEvent> buildSwells(const TrackContext& ctx, const std::vector<NoteEvent>& notes,
                                    const std::vector<CurveEvent>& curves,
                                    const std::unordered_map<std::string, SlurPosition>& phrases = {});

} // namespace msd
