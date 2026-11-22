#include "playback/Repeats.h"

#include <algorithm>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>

namespace msd
{
namespace
{

struct VoltaRange
{
    int start = -1;
    int end = -1;
    const std::vector<int>* passes = nullptr;
};

struct PendingJump
{
    std::optional<NodeId> until;
    std::optional<NodeId> then;
    bool playRepeats = false;
};

bool contains(const std::vector<int>& values, int value)
{
    return std::find(values.begin(), values.end(), value) != values.end();
}

bool hasMarker(const Measure& measure, const NodeId& id)
{
    return std::any_of(measure.markers.begin(), measure.markers.end(),
                       [&](const Marker& marker) { return marker.id == id; });
}

} // namespace

std::vector<PlayedMeasure> expandRepeats(const MusicData& music, const std::vector<MeasureInfo>& timeline)
{
    const std::vector<Measure>& measures = music.measures;
    const int count = static_cast<int>(measures.size());

    std::unordered_map<NodeId, int> indexOf;
    for (int i = 0; i < count; ++i)
        indexOf.insert_or_assign(measures[i].id, i);
    const auto indexOrNone = [&](const NodeId& id) {
        const auto it = indexOf.find(id);
        return it == indexOf.end() ? -1 : it->second;
    };
    const auto markerMeasure = [&](const NodeId& id) {
        for (int i = 0; i < count; ++i)
            if (hasMarker(measures[i], id))
                return i;
        return -1;
    };

    std::vector<VoltaRange> voltas;
    for (const GlobalSpanner& spanner : music.globalSpanners)
    {
        const auto* volta = std::get_if<Volta>(&spanner);
        if (volta == nullptr)
            continue;
        const VoltaRange range { indexOrNone(volta->startMeasureId), indexOrNone(volta->endMeasureId), &volta->passes };
        if (range.start >= 0 && range.end >= range.start)
            voltas.push_back(range);
    }
    std::stable_sort(voltas.begin(), voltas.end(),
                     [](const VoltaRange& a, const VoltaRange& b) { return a.start < b.start; });

    // The last volta of each run of adjacent voltas (indexes into `voltas`).
    std::set<std::size_t> finalVoltas;
    for (std::size_t v = 0; v < voltas.size(); ++v)
        if (v + 1 == voltas.size() || voltas[v + 1].start != voltas[v].end + 1)
            finalVoltas.insert(v);

    std::vector<PlayedMeasure> played;
    std::vector<int> counts(measures.size(), 0);
    std::set<NodeId> takenJumps;
    std::set<int> finishedRepeats;
    std::optional<PendingJump> jump;
    int sectionStart = 0;
    int iteration = 1;
    double tick = 0;
    int i = 0;

    for (int guard = 0; i >= 0 && i < count && guard < 10000; ++guard)
    {
        const Measure& measure = measures[i];
        const MeasureInfo& info = timeline[i];
        if (measure.repeatStart.value_or(false) && sectionStart != i)
        {
            sectionStart = i;
            iteration = 1;
        }

        const bool skipRepeats = jump.has_value() && !jump->playRepeats;
        std::optional<std::size_t> volta;
        for (std::size_t v = 0; v < voltas.size(); ++v)
            if (voltas[v].start <= i && i <= voltas[v].end)
            {
                volta = v;
                break;
            }
        const bool voltaPlays =
            !volta || (skipRepeats ? finalVoltas.contains(*volta) : contains(*voltas[*volta].passes, iteration));
        if (!voltaPlays)
        {
            ++i;
            continue;
        }

        counts[i] += 1;
        played.push_back({ i, counts[i], tick });
        tick += info.lengthTicks;

        if (jump && jump->until && hasMarker(measure, *jump->until))
        {
            const std::optional<NodeId> then = jump->then;
            jump.reset();
            if (!then)
                break;
            i = markerMeasure(*then);
            continue;
        }

        if (measure.repeatEnd && !skipRepeats && !finishedRepeats.contains(i))
        {
            if (iteration < measure.repeatEnd->times)
            {
                ++iteration;
                i = sectionStart;
                continue;
            }
            finishedRepeats.insert(i);
        }

        const auto next = std::find_if(measure.jumps.begin(), measure.jumps.end(),
                                       [&](const Jump& j) { return !takenJumps.contains(j.id); });
        if (next != measure.jumps.end())
        {
            takenJumps.insert(next->id);
            jump = PendingJump { next->until, next->then, next->playRepeats };
            if (next->playRepeats)
            {
                finishedRepeats.clear();
                iteration = 1;
            }
            const auto* marker = std::get_if<JumpToMarker>(&next->to);
            i = marker ? markerMeasure(marker->id) : 0;
            continue;
        }

        ++i;
    }
    return played;
}

} // namespace msd
