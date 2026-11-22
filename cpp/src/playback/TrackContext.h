#pragma once

// What every step of building one track's events works from. TS: TrackContext in
// prototype-web/src/core/playback/buildEvents.ts. Everything is borrowed: the caller
// keeps the score, setup and the rest alive while the track is built.

#include <string>
#include <vector>

#include "core/types/InterpretationProfile.h"
#include "core/types/MusicData.h"
#include "core/types/PlaybackSetup.h"
#include "playback/Repeats.h"
#include "playback/ScoreIndex.h"
#include "playback/Tempo.h"
#include "playback/Timeline.h"

namespace msd
{

struct TrackContext
{
    const MusicData& music;
    const PlaybackSetup& setup; // TS: profiles
    const std::vector<MeasureInfo>& timeline;
    const std::vector<PlayedMeasure>& played;
    const ScoreIndex& index;
    const Track& track;
    const InterpretationProfile& profile; // resolved: no `extends` left
    const TempoMap& tempo; // the score's tempo, which is part of the music and so known to the rules
    std::vector<std::string>& warnings;
};

} // namespace msd
