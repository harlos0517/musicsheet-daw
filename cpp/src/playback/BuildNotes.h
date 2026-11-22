#pragma once

// A track's note events: ties joined, techniques, slurs and phrasing resolved, the
// profile's note rules and the setup's overrides applied. Ported from buildNotes in
// prototype-web/src/core/playback/buildEvents.ts.

#include <string>
#include <unordered_map>
#include <vector>

#include "core/types/MusicEvent.h"
#include "playback/TrackContext.h"

namespace msd
{

struct BuiltNotes
{
    std::vector<NoteEvent> notes; // in played order
    // Event ID → its place in the phrase as written: its place in a slur before a
    // repeated pitch split the slur for playing. Swells follow the written phrase.
    std::unordered_map<std::string, SlurPosition> phrases;
};

// `curves` are the track's curves from buildCurves: notes take their velocity from the
// dynamics curve where they start.
BuiltNotes buildNotes(const TrackContext& ctx, const std::vector<CurveEvent>& curves);

} // namespace msd
