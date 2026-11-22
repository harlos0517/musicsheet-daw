#include "playback/Tempo.h"

#include <algorithm>
#include <utility>

namespace msd
{
namespace
{

constexpr double defaultQpm = 120;
constexpr int rampStepsPerQuarter = 4; // a rit. / accel. becomes one tempo step per this fraction of a quarter

bool byTick(const TempoPoint& a, const TempoPoint& b) { return a.tick < b.tick; }

// The qpm of the last point at or before `tick`; before the first, the first's.
// TS: points.findLast(point => point.tick <= tick)?.qpm ?? points[0]?.qpm ?? 120
double qpmAt(const std::vector<TempoPoint>& sorted, double tick)
{
    if (sorted.empty())
        return defaultQpm;
    const auto after = std::upper_bound(sorted.begin(), sorted.end(), tick,
                                        [](double t, const TempoPoint& point) { return t < point.tick; });
    return after == sorted.begin() ? sorted.front().qpm : std::prev(after)->qpm;
}

} // namespace

TempoMap::TempoMap(std::vector<TempoPoint> points, int tpq)
    : points_(std::move(points))
    , tpq_(tpq)
{
    if (points_.empty())
        points_.push_back({ 0, defaultQpm });
    starts_.reserve(points_.size());
    starts_.push_back(0);
    for (std::size_t i = 1; i < points_.size(); ++i)
        starts_.push_back(starts_[i - 1] + (points_[i].tick - points_[i - 1].tick) * msPerTick(points_[i - 1].qpm));
}

// Both lookups find the last point at or before the value (TS: findLastIndex, which
// is the same as a binary search since ticks and starts never decrease), or the first.
double TempoMap::tickToMs(double tick) const
{
    const auto after = std::upper_bound(points_.begin(), points_.end(), tick,
                                        [](double t, const TempoPoint& point) { return t < point.tick; });
    const std::size_t i = after == points_.begin() ? 0 : static_cast<std::size_t>(after - points_.begin() - 1);
    return starts_[i] + (tick - points_[i].tick) * msPerTick(points_[i].qpm);
}

double TempoMap::msToTick(double ms) const
{
    const auto after = std::upper_bound(starts_.begin(), starts_.end(), ms);
    const std::size_t i = after == starts_.begin() ? 0 : static_cast<std::size_t>(after - starts_.begin() - 1);
    return points_[i].tick + (ms - starts_[i]) / msPerTick(points_[i].qpm);
}

TempoMap buildTempoMap(const MusicData& music, const std::vector<PlayedMeasure>& played, const ScoreIndex& index,
                       const std::vector<MeasureInfo>& timeline)
{
    const int tpq = music.tpq;

    // Written tempo points.
    std::vector<TempoPoint> written;
    for (const MeasureInfo& info : timeline)
        for (const Tempo& tempo : info.measure->tempos)
            written.push_back({ info.startTick + tempo.tick, tempo.bpm * durationTicks(tempo.unit, tpq) / tpq });
    std::stable_sort(written.begin(), written.end(), byTick);
    const std::vector<TempoPoint> marked = written;

    // Ramp points go in front of the marks, so a tempo mark written where a ramp ends
    // (an "a tempo" after a rit.) is what counts from there on.
    std::vector<TempoPoint> ramps;
    for (const GlobalSpanner& spanner : music.globalSpanners)
    {
        const auto* change = std::get_if<TempoChange>(&spanner);
        if (change == nullptr)
            continue;
        const std::optional<Position> start = resolveAnchor(index, change->start);
        const std::optional<Position> end = resolveAnchor(index, change->end);
        if (!start || !end)
            continue;
        const double startTick = absTick(timeline, *start);
        const double endTick = absTick(timeline, *end);
        const double from = qpmAt(marked, startTick);
        const double to = change->targetTempo.value_or(from * (change->type == TempoChangeType::Rit ? 0.75 : 1.25));
        const double step = static_cast<double>(tpq) / rampStepsPerQuarter;
        for (double tick = startTick; tick < endTick; tick += step)
        {
            const double middle = std::min(endTick, tick + step / 2);
            const double progress = (middle - startTick) / (endTick - startTick);
            ramps.push_back({ tick, from + (to - from) * progress });
        }
        ramps.push_back({ endTick, to });
    }
    written.insert(written.begin(), ramps.begin(), ramps.end());
    std::stable_sort(written.begin(), written.end(), byTick);

    // Played tempo points: each run of measures that follows the page starts with the
    // tempo written at its first barline, then takes the points written inside it.
    std::vector<TempoPoint> points;
    const double scoreEnd = timeline.empty() ? 0 : timeline.back().startTick + timeline.back().lengthTicks;
    std::size_t first = 0;
    while (first < played.size())
    {
        std::size_t last = first;
        while (last + 1 < played.size() && played[last + 1].measureIndex == played[last].measureIndex + 1)
            ++last;
        const MeasureInfo& firstInfo = timeline[played[first].measureIndex];
        const MeasureInfo& lastInfo = timeline[played[last].measureIndex];
        const double runStart = firstInfo.startTick;
        const double runEnd = lastInfo.startTick + lastInfo.lengthTicks;
        const double shift = played[first].startTick - runStart;
        points.push_back({ runStart + shift, qpmAt(written, runStart) });
        for (const TempoPoint& point : written)
        {
            const bool inRun = point.tick > runStart && point.tick < runEnd;
            if (inRun || (point.tick == runEnd && runEnd == scoreEnd))
                points.push_back({ point.tick + shift, point.qpm });
        }
        first = last + 1;
    }

    return TempoMap(std::move(points), tpq);
}

} // namespace msd
