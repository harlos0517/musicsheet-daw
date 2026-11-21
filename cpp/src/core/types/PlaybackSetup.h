#pragma once

// Mirrors prototype-web/src/core/types/PlaybackSetup.ts (conventions: see MusicData.h).
// Everything playback needs besides the score itself.

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/types/EventOverride.h"
#include "core/types/InstrumentProfile.h"
#include "core/types/InterpretationProfile.h"
#include "core/types/MusicData.h"

namespace msd
{

struct TechniqueDefinition
{
    std::string id;
    std::optional<std::string> group;    // techniques in one group replace each other ("pizz." ends at "arco")
    std::optional<std::string> fallback; // used by an instrument that has no mapping for this technique
};

// One DAW track a sheet track plays on. A drum kit written in one part has one output
// per instrument; a snare part can send its rim clicks to a sticks patch.
struct TrackOutput
{
    std::string id; // stays the same, so the DAW track can be found again: 'main', 'toms'
    std::optional<std::string> name; // DAW track name; default: the sheet track's name
    std::string instrumentProfileId;
    // The score's kit sounds it plays → the profile's kit sound each one is
    // ('low-tom' → 'tom-4'). Default: every sound no other output takes, under its own
    // name, and all pitched notes.
    std::optional<std::map<std::string, std::string>> sounds;
};

// How one sheet track is played: how it is read, and the DAW tracks it sounds on.
struct TrackPlayback
{
    std::string interpretationProfileId;
    std::vector<TrackOutput> outputs; // one per DAW track; most sheet tracks have one
};

struct PlaybackSetup
{
    std::vector<TechniqueDefinition> techniques;
    std::vector<InterpretationProfile> interpretationProfiles;
    std::vector<InstrumentProfile> instrumentProfiles;
    std::map<NodeId, TrackPlayback> trackProfiles; // by sheet track
    std::vector<MusicEventOverride> overrides;
};

} // namespace msd
