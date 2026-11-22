#pragma once

// The tempo map: played-order ticks ↔ milliseconds. Ported from
// prototype-web/src/core/playback/tempo.ts.

#include <vector>

#include "core/types/MusicData.h"
#include "playback/Repeats.h"
#include "playback/ScoreIndex.h"
#include "playback/Timeline.h"

namespace msd
{

struct TempoPoint
{
    double tick = 0;
    double qpm = 120; // quarter notes per minute
};

class TempoMap
{
public:
    // `points` in played order, ticks never decreasing; at least one.
    TempoMap(std::vector<TempoPoint> points, int tpq);

    const std::vector<TempoPoint>& points() const { return points_; }
    double tickToMs(double tick) const;
    double msToTick(double ms) const;

    // The same conversions, for many values that mostly go up (ramp samples): each call
    // looks for its tempo point from where the last one found its, instead of searching
    // all of them. The results are the same, to the bit.
    class Cursor
    {
    public:
        explicit Cursor(const TempoMap& map)
            : map_(map)
        {}
        double tickToMs(double tick);
        double msToTick(double ms);

    private:
        const TempoMap& map_;
        std::size_t point_ = 0; // for tickToMs
        std::size_t start_ = 0; // for msToTick
    };

private:
    double msPerTick(double qpm) const { return 60000 / (qpm * tpq_); }
    double msAt(std::size_t i, double tick) const
    {
        return starts_[i] + (tick - points_[i].tick) * msPerTick(points_[i].qpm);
    }
    double tickAt(std::size_t i, double ms) const
    {
        return points_[i].tick + (ms - starts_[i]) / msPerTick(points_[i].qpm);
    }

    std::vector<TempoPoint> points_;
    std::vector<double> starts_; // the time each point starts at, in ms
    int tpq_;
};

// The tempo is resolved in written order first: every measure has the tempo the page
// shows there, also when it is reached by a repeat or a jump. Those written tempo
// points are then copied into played time, run by run. A gradual change without
// targetTempo ends at 75 % (rit.) or 125 % (accel.) of the tempo it starts from.
TempoMap buildTempoMap(const MusicData& music, const std::vector<PlayedMeasure>& played, const ScoreIndex& index,
                       const std::vector<MeasureInfo>& timeline);

} // namespace msd
