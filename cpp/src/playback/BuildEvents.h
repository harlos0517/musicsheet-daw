#pragma once

// MusicData + PlaybackSetup → MusicEvents. Ported from buildMusicEvents in
// prototype-web/src/core/playback/buildEvents.ts.

#include <string>
#include <vector>

#include "core/types/MusicData.h"
#include "core/types/MusicEvent.h"
#include "core/types/PlaybackSetup.h"
#include "playback/Repeats.h"
#include "playback/Tempo.h"

namespace msd
{

struct EventResult
{
    std::vector<PlayedMeasure> played;
    TempoMap tempo;
    // Per track, in score order: its curves, then its swells, then its notes.
    std::vector<MusicEvent> events;
    std::vector<std::string> warnings;
};

// Repeats unrolled, profiles and overrides applied. A track the setup assigns no
// profiles to is skipped, with a warning. Throws std::runtime_error when a track names
// an interpretation profile the setup does not have.
EventResult buildMusicEvents(const MusicData& music, const PlaybackSetup& setup);

} // namespace msd
