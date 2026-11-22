#pragma once

// Curve shapes and the value of a curve at a tick. Ported from
// prototype-web/src/core/playback/curves.ts.

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/types/MusicEvent.h"

namespace msd
{

struct CurveScope
{
    NodeId staffId;
    int voiceIndex = 0;
};

// One string per parameter: "dynamics", …, or "custom:<name>". Curves with the same
// key are one curve.
std::string curveKey(const CurveParameter& parameter);

double clamp01(double value);

// Progress 0–1 of a segment at time 0–1. `a` is the steepness: 0 is a straight line,
// a positive value starts slowly, a negative one starts fast.
//   exp:   (e^(a·t) − 1) / (e^a − 1)                              default a = 3
//   power: t^(1 + a) for a > 0, 1 − (1 − t)^(1 − a) for a < 0      default a = 1
//   ease:  the power curve on each half, mirrored at the middle   default a = 1
double shape(std::optional<CurveType> type, std::optional<double> a, double t);

// Value of one segment at a tick; its end value holds afterwards.
double segmentValue(const CurveEvent& event, double tick);

enum class CurveLayer { Total, Base };

// Value of a curve at a tick. A curve has three layers, each decided by the segment
// that started last, whose end value then holds:
//   base:  dynamics, hairpins and other written levels
//   scale: Scale segments, which multiply the base (swells)
//   add:   Add segments, added on top (accents)
// CurveLayer::Base gives the base alone. Without `scope`, segments limited to some
// voices count too.
double evaluateCurve(const std::vector<CurveEvent>& events, const CurveParameter& parameter, double tick,
                     double fallback, const std::optional<CurveScope>& scope = std::nullopt,
                     CurveLayer layer = CurveLayer::Total);

// The same as evaluateCurve for every voice at once, for ticks asked in increasing
// order. It walks the segments once instead of scanning them for every tick, which
// matters for curves with thousands of segments. Points into `events`.
class CurveSampler
{
public:
    CurveSampler(const std::vector<CurveEvent>& events, const CurveParameter& parameter, double fallback);

    double operator()(double tick);

private:
    std::vector<const CurveEvent*> sorted_;
    std::size_t next_ = 0;
    const CurveEvent* latest_[3] {}; // by layer: base, scale, add
    double fallback_;
};

// The same as evaluateCurve for one parameter, for many ticks in any order: for each
// scope asked about, each layer's segments are sorted by start once, and the one in
// force is found by a binary search instead of a scan. Points into `events`, which must
// not change while it is used.
class CurveLookup
{
public:
    CurveLookup(const std::vector<CurveEvent>& events, const CurveParameter& parameter);

    double operator()(double tick, double fallback, const std::optional<CurveScope>& scope = std::nullopt,
                      CurveLayer layer = CurveLayer::Total);

private:
    struct Layers
    {
        std::vector<const CurveEvent*> byLayer[3]; // base, scale, add; by start, in event order on ties
    };
    const Layers& layersFor(const std::optional<CurveScope>& scope);

    std::vector<const CurveEvent*> events_; // the parameter's, in their order
    std::optional<Layers> unscoped_;
    std::map<std::pair<NodeId, int>, Layers> scoped_;
};

} // namespace msd
