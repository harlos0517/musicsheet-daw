#include "playback/Timeline.h"

#include <cmath>
#include <type_traits>
#include <variant>

namespace msd
{
namespace
{

// How many ticks a tuplet occupies in its parent: `normal` units of `unit`.
double tupletTicks(const Tuplet& tuplet, int tpq) { return durationTicks(tuplet.unit, tpq) * tuplet.normal; }

// Ticks an event occupies in its parent voice (or parent tuplet, before scaling).
double eventTicks(const VoiceEvent& event, int tpq)
{
    return std::visit(
        [tpq](const auto& e) {
            if constexpr (std::is_same_v<std::decay_t<decltype(e)>, Tuplet>)
                return tupletTicks(e, tpq);
            else
                return durationTicks(e.duration, tpq);
        },
        static_cast<const VoiceEvent::variant&>(event));
}

double timeSigTicks(const TimeSignature& timeSig, int tpq)
{
    return static_cast<double>(timeSig.beats) * tpq * 4 / timeSig.unit;
}

double walk(const std::vector<VoiceEvent>& list, double from, double scale, const std::vector<const Tuplet*>& tuplets,
            int tpq, std::vector<TimedEvent>& out)
{
    double tick = from;
    for (const VoiceEvent& event : list)
    {
        const double ticks = eventTicks(event, tpq) * scale;
        out.push_back({ &event, tick, ticks, tuplets });
        if (const auto* tuplet = std::get_if<Tuplet>(&event))
        {
            std::vector<const Tuplet*> inner = tuplets;
            inner.push_back(tuplet);
            walk(tuplet->events, tick, scale * tuplet->normal / tuplet->actual, inner, tpq, out);
        }
        tick += ticks;
    }
    return tick;
}

} // namespace

double durationTicks(const Duration& duration, int tpq)
{
    // TS: tpq * 2 ** type * (2 - 2 ** -dots)
    return tpq * std::exp2(static_cast<int>(duration.type)) * (2 - std::exp2(-duration.dots));
}

std::vector<MeasureInfo> measureTimeline(const MusicData& music)
{
    TimeSignature timeSig { .id = "", .beats = 4, .unit = 4 };
    KeySignature key { .id = "", .accidentals = 0 };
    double startTick = 0;

    std::vector<MeasureInfo> timeline;
    timeline.reserve(music.measures.size());
    for (std::size_t i = 0; i < music.measures.size(); ++i)
    {
        const Measure& measure = music.measures[i];
        if (measure.timeSig)
            timeSig = *measure.timeSig;
        if (measure.key)
            key = *measure.key;
        const double lengthTicks =
            measure.actualTicks ? static_cast<double>(*measure.actualTicks) : timeSigTicks(timeSig, music.tpq);
        timeline.push_back({ &measure, static_cast<int>(i), timeSig, key, startTick, lengthTicks });
        startTick += lengthTicks;
    }
    return timeline;
}

std::vector<TimedEvent> timeVoice(const std::vector<VoiceEvent>& events, int tpq, double start)
{
    std::vector<TimedEvent> result;
    walk(events, start, 1, {}, tpq, result);
    return result;
}

} // namespace msd
