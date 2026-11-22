#include "playback/Curves.h"

#include <algorithm>
#include <cmath>

namespace msd
{
namespace
{

// Slow start: t^(1 + a). Used mirrored for a slow end.
double easeIn(double t, double a) { return std::pow(t, 1 + a); }

enum Layer { base, scale, add };

Layer layerOf(const CurveEvent& event)
{
    if (event.combine == CurveCombine::Add)
        return add;
    return event.combine == CurveCombine::Scale ? scale : base;
}

bool covers(const CurveEvent& event, const std::optional<CurveScope>& scope)
{
    if (!scope || !event.appliesTo)
        return true;
    return std::any_of(event.appliesTo->begin(), event.appliesTo->end(), [&](const CurveTarget& target) {
        return target.staffId == scope->staffId && (!target.voiceIndex || *target.voiceIndex == scope->voiceIndex);
    });
}

double combine(const CurveEvent* const (&latest)[3], double tick, double fallback, CurveLayer layer)
{
    const double baseValue = latest[base] ? segmentValue(*latest[base], tick) : fallback;
    if (layer == CurveLayer::Base)
        return baseValue;
    const double scaleValue = latest[scale] ? segmentValue(*latest[scale], tick) : 1;
    const double addValue = latest[add] ? segmentValue(*latest[add], tick) : 0;
    return baseValue * scaleValue + addValue;
}

} // namespace

std::string curveKey(const CurveParameter& parameter)
{
    if (const auto* custom = std::get_if<CustomCurveParameter>(&parameter))
        return "custom:" + custom->name;
    switch (std::get<BuiltInCurveParameter>(parameter))
    {
    case BuiltInCurveParameter::Dynamics: return "dynamics";
    case BuiltInCurveParameter::Sustain: return "sustain";
    case BuiltInCurveParameter::Sostenuto: return "sostenuto";
    case BuiltInCurveParameter::UnaCorda: return "una-corda";
    }
    return {};
}

double clamp01(double value) { return std::min(1.0, std::max(0.0, value)); }

double shape(std::optional<CurveType> type, std::optional<double> a, double t)
{
    switch (type.value_or(CurveType::Linear))
    {
    case CurveType::Hold: return t >= 1 ? 1 : 0;
    case CurveType::Exp: {
        const double k = a.value_or(3);
        return k == 0 ? t : (std::exp(k * t) - 1) / (std::exp(k) - 1);
    }
    case CurveType::Power: {
        const double k = a.value_or(1);
        return k >= 0 ? easeIn(t, k) : 1 - easeIn(1 - t, -k);
    }
    case CurveType::Ease: {
        const double k = a.value_or(1);
        const auto half = [k](double x) { return k >= 0 ? easeIn(x, k) : 1 - easeIn(1 - x, -k); };
        return t < 0.5 ? half(2 * t) / 2 : 1 - half(2 * (1 - t)) / 2;
    }
    case CurveType::Linear: break;
    }
    return t;
}

double segmentValue(const CurveEvent& event, double tick)
{
    const double end = event.startTick + event.durationTicks;
    if (event.durationTicks <= 0 || tick >= end)
        return event.toValue;
    const double t = (tick - event.startTick) / event.durationTicks;
    const double progress = shape(event.curveType, event.steepness, t);
    return event.fromValue + (event.toValue - event.fromValue) * progress;
}

double evaluateCurve(const std::vector<CurveEvent>& events, const CurveParameter& parameter, double tick,
                     double fallback, const std::optional<CurveScope>& scope, CurveLayer layer)
{
    const CurveEvent* latest[3] {};
    for (const CurveEvent& event : events)
    {
        if (!(event.parameter == parameter) || event.startTick > tick || !covers(event, scope))
            continue;
        const CurveEvent*& found = latest[layerOf(event)];
        if (found == nullptr || event.startTick >= found->startTick)
            found = &event;
    }
    return combine(latest, tick, fallback, layer);
}

CurveSampler::CurveSampler(const std::vector<CurveEvent>& events, const CurveParameter& parameter, double fallback)
    : fallback_(fallback)
{
    for (const CurveEvent& event : events)
        if (event.parameter == parameter)
            sorted_.push_back(&event);
    std::stable_sort(sorted_.begin(), sorted_.end(),
                     [](const CurveEvent* a, const CurveEvent* b) { return a->startTick < b->startTick; });
}

double CurveSampler::operator()(double tick)
{
    while (next_ < sorted_.size() && sorted_[next_]->startTick <= tick)
    {
        latest_[layerOf(*sorted_[next_])] = sorted_[next_];
        ++next_;
    }
    return combine(latest_, tick, fallback_, CurveLayer::Total);
}

} // namespace msd
