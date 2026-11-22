#include "playback/ScoreIndex.h"

#include <algorithm>
#include <set>

namespace msd
{

ScoreIndex indexScore(const MusicData& music, const std::vector<MeasureInfo>& timeline)
{
    ScoreIndex index;
    for (std::size_t i = 0; i < music.measures.size(); ++i)
        index.measureIndex.insert_or_assign(music.measures[i].id, static_cast<int>(i));

    for (const Track& track : music.tracks)
    {
        std::vector<Placed> trackChords;
        for (const Staff& staff : track.staves)
        {
            std::set<int> staffVoices;
            for (const StaffMeasure& staffMeasure : staff.measures)
            {
                const auto found = index.measureIndex.find(staffMeasure.measureId);
                if (found == index.measureIndex.end())
                    continue;
                const int measure = found->second;
                for (const Voice& voice : staffMeasure.voices)
                {
                    staffVoices.insert(voice.index);
                    for (const TimedEvent& timed : timeVoice(voice.events, music.tpq))
                    {
                        const Placed placed {
                            .event = timed.event,
                            .trackId = track.id,
                            .staffId = staff.id,
                            .voiceIndex = voice.index,
                            .measureIndex = measure,
                            .tick = timed.tick,
                            .ticks = timed.ticks,
                            .absTick = timeline[measure].startTick + timed.tick,
                        };
                        const NodeId& id = std::visit([](const auto& e) -> const NodeId& { return e.id; },
                                                      static_cast<const VoiceEvent::variant&>(*timed.event));
                        index.elements.insert_or_assign(id, placed);
                        const auto* chord = std::get_if<Chord>(timed.event);
                        if (chord == nullptr)
                            continue;
                        trackChords.push_back(placed);
                        for (const ChordNote& note : chord->notes)
                            index.elements.insert_or_assign(std::visit([](const auto& n) { return n.id; }, note),
                                                            placed);
                    }
                }
            }
            index.voices[staff.id] = { staffVoices.begin(), staffVoices.end() };
        }
        std::stable_sort(trackChords.begin(), trackChords.end(),
                         [](const Placed& a, const Placed& b) { return a.absTick < b.absTick; });
        index.chords[track.id] = std::move(trackChords);
    }
    return index;
}

std::optional<Position> resolveAnchor(const ScoreIndex& index, const TickAnchor& anchor)
{
    const auto measure = index.measureIndex.find(anchor.measureId);
    if (measure == index.measureIndex.end())
        return std::nullopt;
    return Position { measure->second, static_cast<double>(anchor.tick) };
}

std::optional<Position> resolveAnchor(const ScoreIndex& index, const ElementAnchor& anchor, bool atEnd)
{
    const auto placed = index.elements.find(anchor.id);
    if (placed == index.elements.end())
        return std::nullopt;
    return Position { placed->second.measureIndex, placed->second.tick + (atEnd ? placed->second.ticks : 0) };
}

std::optional<Position> resolveAnchor(const ScoreIndex& index, const Anchor& anchor, bool atEnd)
{
    if (const auto* tick = std::get_if<TickAnchor>(&anchor))
        return resolveAnchor(index, *tick);
    return resolveAnchor(index, std::get<ElementAnchor>(anchor), atEnd);
}

double absTick(const std::vector<MeasureInfo>& timeline, const Position& position)
{
    return timeline[position.measureIndex].startTick + position.tick;
}

} // namespace msd
