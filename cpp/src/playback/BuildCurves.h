#pragma once

// A track's curve segments: dynamics, hairpins, pedals and curve rules. Ported from
// prototype-web/src/core/playback/buildCurves.ts.

#include <vector>

#include "core/types/MusicEvent.h"
#include "playback/TrackContext.h"

namespace msd
{

// Every curve segment of one track, in played order. Two steps:
// 1. The segments in written order, as the page reads from left to right. Each starts
//    from what is written before it, never from what happened to be played before it.
//    Overrides without passes apply here, so later segments start from their values.
// 2. Copied into played time, run by run (runs of measures that follow the page
//    without a jump). Each run first restores the state the page has at its first
//    barline, so a repeat or D.S. lands in the right dynamic, pedal, …, even in the
//    middle of a hairpin. Overrides with passes apply here.
std::vector<CurveEvent> buildCurves(const TrackContext& ctx);

} // namespace msd
