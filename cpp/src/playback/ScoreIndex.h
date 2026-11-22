#pragma once

// Where every voice event sits in the written score, found by ID. Ported from
// prototype-web/src/core/playback/scoreIndex.ts. It points into the MusicData it was
// made from, which must outlive it.

#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

#include "core/types/MusicData.h"
#include "playback/Timeline.h"

namespace msd
{

struct Placed
{
    const VoiceEvent* event = nullptr;
    NodeId trackId;
    NodeId staffId;
    int voiceIndex = 0;
    int measureIndex = 0;
    double tick = 0; // inside the measure
    double ticks = 0;
    double absTick = 0; // written-order tick from the start of the score (no repeats)

    // For an entry of ScoreIndex::chords, whose events are all chords.
    const Chord& chord() const { return std::get<Chord>(*event); }
};

struct Position
{
    int measureIndex = 0;
    double tick = 0;
};

struct ScoreIndex
{
    std::unordered_map<NodeId, Placed> elements; // chords, rests, spaces and tuplets; notes map to their chord
    std::map<NodeId, std::vector<Placed>> chords; // every chord of a track, in written order
    std::map<NodeId, std::vector<int>> voices;    // voice indexes used anywhere on a staff, ascending
    std::unordered_map<NodeId, int> measureIndex;
};

ScoreIndex indexScore(const MusicData& music, const std::vector<MeasureInfo>& timeline);

// `atEnd` puts an element anchor at the end of its element instead of the start.
// Empty when the anchor points nowhere.
std::optional<Position> resolveAnchor(const ScoreIndex& index, const Anchor& anchor, bool atEnd = false);
std::optional<Position> resolveAnchor(const ScoreIndex& index, const TickAnchor& anchor);
std::optional<Position> resolveAnchor(const ScoreIndex& index, const ElementAnchor& anchor, bool atEnd = false);

double absTick(const std::vector<MeasureInfo>& timeline, const Position& position);

} // namespace msd
