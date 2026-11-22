#pragma once

// Where measures and voice events sit in written time (no repeats). Ported from
// prototype-web/src/core/validation/time.ts.
//
// Ticks in playback are double, as in TS: a tuplet of 7 makes fractional ticks, and
// later steps scale lengths. Doing the same arithmetic in the same order as the TS
// gives the same numbers, so results can be compared with what `pnpm midi` wrote.
//
// The results point into the MusicData they were made from, which must outlive them.

#include <vector>

#include "core/types/MusicData.h"

namespace msd
{

// Nominal length of a duration in ticks, ignoring any enclosing tuplet.
double durationTicks(const Duration& duration, int tpq);

struct MeasureInfo
{
    const Measure* measure = nullptr;
    int index = 0;
    TimeSignature timeSig; // in effect in this measure
    KeySignature key;      // in effect in this measure
    double startTick = 0;  // from the start of the score (written order, no repeats)
    double lengthTicks = 0;
};

// The time signature and key in effect in every measure, and its position. Falls back
// to 4/4 and C major if the first measure has none (the TS validator reports that).
std::vector<MeasureInfo> measureTimeline(const MusicData& music);

struct TimedEvent
{
    const VoiceEvent* event = nullptr;
    double tick = 0;  // start, relative to the start of the walk (usually the measure's)
    double ticks = 0; // actual length, after tuplet scaling
    std::vector<const Tuplet*> tuplets; // enclosing tuplets, outermost first
};

// Every event of a voice with its real start and length, descending into tuplets.
// A tuplet itself is listed before its contents.
std::vector<TimedEvent> timeVoice(const std::vector<VoiceEvent>& events, int tpq, double start = 0);

} // namespace msd
