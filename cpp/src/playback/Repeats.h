#pragma once

// The order measures are played in. Ported from
// prototype-web/src/core/validation/repeats.ts.

#include <vector>

#include "core/types/MusicData.h"
#include "playback/Timeline.h"

namespace msd
{

struct PlayedMeasure
{
    int measureIndex = 0; // into the score's measures and the timeline (TS: info)
    int pass = 1;         // how many times this measure has been played so far, from 1
    double startTick = 0; // played-order tick of the measure's start
};

// Unrolls repeats, voltas and jumps into the order the measures are played.
// Assumed here, because the data does not say: "to-coda" and "fine" act at the end of
// their measure; after a jump with playRepeats false only the last volta is played.
std::vector<PlayedMeasure> expandRepeats(const MusicData& music, const std::vector<MeasureInfo>& timeline);

} // namespace msd
