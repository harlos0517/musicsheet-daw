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

private:
    double msPerTick(double qpm) const { return 60000 / (qpm * tpq_); }

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
