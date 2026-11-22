#include "playback/BuildCurves.h"

#include <algorithm>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

#include "core/json/EnumNames.h"
#include "playback/Curves.h"
#include "playback/Interpretation.h"
#include "playback/JsMath.h"

namespace msd
{
namespace
{

using Applies = std::vector<CurveTarget>;

const CurveParameter dynamicsCurve = BuiltInCurveParameter::Dynamics;

// A segment in written time (no repeats), before it is copied into played time.
struct Written
{
    CurveEvent event;
    std::optional<std::vector<int>> passes; // passes its source is limited to
};

// Something written at a tick, run in written order (then by `order`).
struct Item
{
    double tick = 0;
    int order = 0;
    std::function<void()> run;
};

bool contains(const std::vector<int>& values, int value)
{
    return std::find(values.begin(), values.end(), value) != values.end();
}

void applyOverride(CurveEvent& event, const CurveEventOverride& override)
{
    if (override.parameter)
        event.parameter = *override.parameter;
    if (override.appliesTo)
        event.appliesTo = override.appliesTo;
    if (override.startTickOffset)
    {
        event.startTick += *override.startTickOffset;
        event.durationTicks = std::max(0.0, event.durationTicks - *override.startTickOffset);
    }
    if (override.durationTicks)
        event.durationTicks =
            std::max(0.0, applyMode(override.durationTicks->mode, event.durationTicks, override.durationTicks->value));
    if (override.fromValue)
        event.fromValue = applyMode(override.fromValue->mode, event.fromValue, override.fromValue->value);
    if (override.toValue)
        event.toValue = applyMode(override.toValue->mode, event.toValue, override.toValue->value);
    if (override.curveType)
        event.curveType = override.curveType;
    if (override.steepness)
        event.steepness = override.steepness;
}

// The curve overrides that are limited to some passes, or those that are not.
std::vector<const CurveEventOverride*> curveOverrides(const TrackContext& ctx, bool withPasses)
{
    std::vector<const CurveEventOverride*> result;
    for (const MusicEventOverride& override : ctx.setup.overrides)
        if (const auto* curve = std::get_if<CurveEventOverride>(&override))
            if (curve->passes.has_value() == withPasses)
                result.push_back(curve);
    return result;
}

// Value a curve has where nothing has been written yet.
double initialValue(const TrackContext& ctx, const CurveParameter& parameter)
{
    return parameter == dynamicsCurve ? ctx.profile.initialDynamics.value_or(defaultInitialDynamics) : 0;
}

std::vector<Written> writeCurves(const TrackContext& ctx)
{
    const Track& track = ctx.track;
    const InterpretationProfile& profile = ctx.profile;
    std::vector<Written> written;
    std::vector<CurveEvent> events;
    std::vector<Item> items;
    const std::vector<const CurveEventOverride*> overrides = curveOverrides(ctx, false);

    const auto scopeOf = [&](const std::optional<std::vector<NodeId>>& staffIds,
                             const std::optional<std::vector<int>>& voiceIndexes) -> std::optional<Applies> {
        if (!staffIds && !voiceIndexes)
            return std::nullopt;
        std::vector<NodeId> staves;
        if (staffIds)
            staves = *staffIds;
        else
            for (const Staff& staff : track.staves)
                staves.push_back(staff.id);
        Applies applies;
        for (const NodeId& staffId : staves)
        {
            if (!voiceIndexes)
                applies.push_back({ staffId, std::nullopt });
            else
                for (const int voiceIndex : *voiceIndexes)
                    applies.push_back({ staffId, voiceIndex });
        }
        return applies;
    };

    // Base level of a curve, without accents added on top.
    const auto current = [&](const CurveParameter& parameter, double tick, const std::optional<Applies>& applies) {
        std::optional<CurveScope> scope;
        if (applies && !applies->empty())
            scope = CurveScope { applies->front().staffId, applies->front().voiceIndex.value_or(0) };
        return evaluateCurve(events, parameter, tick, initialValue(ctx, parameter), scope, CurveLayer::Base);
    };

    const auto emit = [&](const NodeId& sourceId, int segment, CurveEvent event,
                          const std::optional<std::vector<int>>& passes) {
        event.id = "";
        event.sourceIds = { sourceId };
        event.trackId = track.id;
        event.pass = 0;
        event.segmentIndex = segment;
        for (const CurveEventOverride* override : overrides)
            if (override->sourceId == sourceId && override->segmentIndex == segment)
                applyOverride(event, *override);
        events.push_back(event);
        written.push_back({ std::move(event), passes });
    };

    const auto tickOf = [&](const Anchor& anchor, bool atEnd = false) -> std::optional<double> {
        const std::optional<Position> position = resolveAnchor(ctx.index, anchor, atEnd);
        if (!position)
            return std::nullopt;
        return absTick(ctx.timeline, *position);
    };

    // Dynamics
    struct Level
    {
        double tick;
        const DynamicMapping* mapping;
    };
    std::vector<Level> levels;
    for (const Attachment& attachment : track.attachments)
    {
        const auto* dynamic = std::get_if<Dynamic>(&attachment);
        if (dynamic == nullptr)
            continue;
        const std::optional<double> at = tickOf(dynamic->at);
        const DynamicMapping* mapping = nullptr;
        if (profile.dynamics)
            for (const DynamicMapping& d : *profile.dynamics)
                if (d.type == dynamic->type)
                {
                    mapping = &d;
                    break;
                }
        if (!at || mapping == nullptr)
        {
            ctx.warnings.push_back("dynamic " + dynamic->id + ": no mapping for \""
                                   + std::string(enumName(dynamic->type)) + "\"");
            continue;
        }
        const double tick = *at;
        const std::optional<Applies> appliesTo = scopeOf(dynamic->staffIds, dynamic->voiceIndexes);
        const double thenTicks = (mapping->then ? mapping->then->afterQn : 0) * ctx.music.tpq;
        levels.push_back({ tick, mapping });
        items.push_back(
            { tick, dynamic->beforeTick.value_or(false) ? 0 : 1, [&, dynamic, mapping, tick, appliesTo, thenTicks] {
                 const double before = current(dynamicsCurve, tick, appliesTo);
                 const double peak = clamp01(applyMode(mapping->mode, before, mapping->value));
                 const std::optional<DynamicFollowUp>& then = mapping->then;
                 const double after = then ? clamp01(applyMode(then->mode, before, then->value)) : peak;
                             // Add puts the dynamic on top of the level as an offset, so a hairpin
                             // running underneath is not interrupted (an accent such as sfz).
                 const bool added = mapping->combine == DynamicCombine::Add;
                 const double base = added ? before : 0;
                 CurveEvent segment {
                     .parameter = dynamicsCurve,
                     .appliesTo = appliesTo,
                     .combine = added ? std::optional { CurveCombine::Add } : std::nullopt,
                     .startTick = tick,
                     .durationTicks = 0,
                     .fromValue = before - base,
                     .toValue = peak - base,
                 };
                 emit(dynamic->id, 0, segment, std::nullopt);
                 if (!then || thenTicks <= 0)
                     return;
                 segment.durationTicks = thenTicks;
                 segment.fromValue = peak - base;
                 segment.toValue = after - base;
                 segment.curveType = then->curveType;
                 emit(dynamic->id, 1, segment, std::nullopt);
             } });
    }

    // Hairpins
    const HairpinSettings hairpinSetup = profile.hairpin.value_or(HairpinSettings { .defaultChange = 0.2 });
    for (const TrackSpanner& spanner : track.trackSpanners)
    {
        const auto* hairpin = std::get_if<Hairpin>(&spanner);
        if (hairpin == nullptr)
            continue;
        const std::optional<double> start = tickOf(hairpin->start);
        const std::optional<double> end = tickOf(hairpin->end, hairpin->includeEnd.value_or(false));
        if (!start || !end)
            continue;
        const std::optional<Applies> applies = scopeOf(hairpin->staffIds, std::nullopt);
        items.push_back({ *start, 2, [&, hairpin, start = *start, end = *end, applies] {
                             const bool rising = hairpin->type == HairpinType::Cresc;
                             const bool niente = hairpin->niente.value_or(false);
                             const double from = niente && rising ? 0 : current(dynamicsCurve, start, applies);
                             // The target is a fixed-level dynamic sitting exactly where the hairpin ends.
                             const auto target = std::find_if(levels.begin(), levels.end(), [&](const Level& d) {
                                 return d.tick == end && d.mapping->mode == ChangeMode::Set;
                             });
                             double to = target != levels.end() ? target->mapping->value
                                                                : from + (rising ? 1 : -1) * hairpinSetup.defaultChange;
                             if (niente && !rising)
                                 to = 0;
                             CurveEvent segment {
                                 .parameter = dynamicsCurve,
                                 .appliesTo = applies,
                                 .startTick = start,
                                 .durationTicks = end - start,
                                 .fromValue = from,
                                 .toValue = clamp01(to),
                             };
                             const std::optional<CurveShape>& shape = rising ? hairpinSetup.cresc : hairpinSetup.dim;
                             if (shape)
                             {
                                 segment.curveType = shape->curveType;
                                 segment.steepness = shape->steepness;
                             }
                             emit(hairpin->id, 0, segment, std::nullopt);
                         } });
    }

    // Curve rules: pedals, custom spanners and technique markings
    struct Source
    {
        NodeId id;
        const CurveRule* rule;
        double start;
        double end;
        std::optional<Applies> applies;
        std::optional<std::vector<int>> passes;
    };
    std::vector<Source> sources;
    for (const CurveRule& rule : profile.curveRules)
    {
        const auto* pedalMatch = std::get_if<PedalMatch>(&rule.match);
        const auto* techniqueMatch = std::get_if<TechniqueMatch>(&rule.match);
        for (const TrackSpanner& spanner : track.trackSpanners)
        {
            const auto* pedal = std::get_if<Pedal>(&spanner);
            const auto* custom = std::get_if<CustomSpanner>(&spanner);
            const bool isPedal = pedalMatch && pedal && pedal->type == pedalMatch->type;
            const bool isCustom = techniqueMatch && custom && custom->techniqueId == techniqueMatch->techniqueId;
            if (!isPedal && !isCustom)
                continue;
            const std::optional<double> start = isPedal ? tickOf(pedal->start) : tickOf(custom->start);
            const std::optional<double> end = isPedal ? tickOf(pedal->end, pedal->includeEnd.value_or(false))
                                                      : tickOf(custom->end, custom->includeEnd.value_or(false));
            if (!start || !end)
                continue;
            if (isPedal)
                sources.push_back({ pedal->id, &rule, *start, *end, std::nullopt, std::nullopt });
            else
                sources.push_back({ custom->id, &rule, *start, *end, scopeOf(custom->staffIds, custom->voiceIndexes),
                                    custom->passes });
        }
        for (const Attachment& attachment : track.attachments)
        {
            const auto* marking = std::get_if<TechniqueMarking>(&attachment);
            if (!techniqueMatch || marking == nullptr || marking->techniqueId != techniqueMatch->techniqueId)
                continue;
            const std::optional<double> tick = tickOf(marking->at);
            if (!tick)
                continue;
            // A marking has no length: only its time-0 point produces a segment.
            sources.push_back({ marking->id, &rule, *tick, *tick, scopeOf(marking->staffIds, marking->voiceIndexes),
                                marking->passes });
        }
    }
    std::stable_sort(sources.begin(), sources.end(),
                     [](const Source& a, const Source& b) { return a.start < b.start; });

    for (std::size_t s = 0; s < sources.size(); ++s)
    {
        items.push_back({ sources[s].start, 2, [&, s] {
                             const Source& source = sources[s];
                             const CurveRule& rule = *source.rule;
                             const double before = current(rule.parameter, source.start, source.applies);
                             const double low = rule.startValue.value_or(before);
                             // "Next value": the start value the next span on the same curve gives explicitly.
                             const Source* next = nullptr;
                             for (std::size_t o = 0; o < sources.size() && next == nullptr; ++o)
                                 if (o != s && sources[o].start >= source.end
                                     && sources[o].rule->parameter == rule.parameter)
                                     next = &sources[o];
                             const double high = rule.endValue                    ? *rule.endValue
                                                 : next && next->rule->startValue ? *next->rule->startValue
                                                                                  : 1;
                             const double length = source.end - source.start;
                             double previousTime = 0;
                             double previousValue = before;
                             for (std::size_t segment = 0; segment < rule.points.size(); ++segment)
                             {
                                 const CurvePoint& point = rule.points[segment];
                                 if (length == 0 && point.time > 0)
                                     continue;
                                 const double time = std::min(1.0, std::max(previousTime, point.time));
                                 const double durationTicks = jsRound(length * (time - previousTime));
                                 const double value = clamp01(low + (high - low) * point.value);
                                 CurveEvent event {
                                     .parameter = rule.parameter,
                                     .appliesTo = source.applies,
                                     .startTick = source.start + jsRound(length * previousTime),
                                     .durationTicks = durationTicks,
                                     .fromValue = previousValue,
                                     .toValue = value,
                                 };
                                 if (durationTicks > 0)
                                 {
                                     event.curveType = point.curveType;
                                     event.steepness = point.steepness;
                                 }
                                 emit(source.id, static_cast<int>(segment), event, source.passes);
                                 previousTime = time;
                                 previousValue = value;
                             }
                         } });
    }

    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        return a.tick != b.tick ? a.tick < b.tick : a.order < b.order;
    });
    for (const Item& item : items)
        item.run();
    return written;
}

// Segments that can replace each other share a key: same curve, same layer, same voices.
std::string groupKey(const CurveEvent& event)
{
    std::string key = curveKey(event.parameter) + "|";
    if (event.combine)
        key += enumName(*event.combine);
    key += "|";
    if (!event.appliesTo)
        return key + "null";
    for (const CurveTarget& target : *event.appliesTo)
        key += target.staffId + "," + (target.voiceIndex ? std::to_string(*target.voiceIndex) : "-") + ";";
    return key;
}

std::vector<CurveEvent> playCurves(const TrackContext& ctx, const std::vector<Written>& written)
{
    const std::vector<PlayedMeasure>& played = ctx.played;
    const std::vector<MeasureInfo>& timeline = ctx.timeline;
    const std::vector<const CurveEventOverride*> overrides = curveOverrides(ctx, true);
    std::vector<CurveEvent> result;
    std::unordered_map<std::string, int> usedIds;
    if (played.empty())
        return result;

    const auto push = [&](CurveEvent event) {
        const std::string source =
            event.sourceIds.empty() ? ctx.track.id + ":" + curveKey(event.parameter) : event.sourceIds.front();
        const std::string id = source + "@" + std::to_string(event.pass) + "#" + std::to_string(event.segmentIndex);
        // The same segment can be entered twice in one pass of different measures.
        const int count = ++usedIds[id];
        event.id = count == 1 ? id : id + "~" + std::to_string(count);
        result.push_back(std::move(event));
    };

    // In the order their first segment was written.
    std::vector<std::vector<const Written*>> groups;
    std::unordered_map<std::string, std::size_t> groupIndex;
    for (const Written& item : written)
    {
        const auto [found, added] = groupIndex.try_emplace(groupKey(item.event), groups.size());
        if (added)
            groups.emplace_back();
        groups[found->second].push_back(&item);
    }

    const double scoreEnd = timeline.back().startTick + timeline.back().lengthTicks;
    std::size_t from = 0;
    while (from < played.size())
    {
        std::size_t to = from;
        while (to + 1 < played.size() && played[to + 1].measureIndex == played[to].measureIndex + 1)
            ++to;
        const PlayedMeasure& first = played[from];
        const MeasureInfo& lastInfo = timeline[played[to].measureIndex];
        const double runStart = timeline[first.measureIndex].startTick;
        const double runEnd = lastInfo.startTick + lastInfo.lengthTicks;
        const double shift = first.startTick - runStart;
        const auto passAt = [&](double tick) {
            for (std::size_t m = to + 1; m-- > from;)
                if (timeline[played[m].measureIndex].startTick <= tick)
                    return played[m].pass;
            return first.pass;
        };

        // Copy of a written segment from `start`, cut at the end of the run.
        const auto copy = [&](const CurveEvent& event, double start, int pass) {
            const double eventEnd = event.startTick + event.durationTicks;
            const double end = std::min(eventEnd, runEnd);
            CurveEvent copied = event;
            copied.pass = pass;
            copied.startTick = start + shift;
            copied.durationTicks = std::max(0.0, end - start);
            copied.fromValue = start > event.startTick ? segmentValue(event, start) : event.fromValue;
            copied.toValue = end < eventEnd ? segmentValue(event, end) : event.toValue;
            return copied;
        };

        // State at the first barline of the run. Not needed at the very beginning.
        if (from > 0 || runStart > 0)
        {
            struct Entry
            {
                double order;
                CurveEvent event;
            };
            std::vector<Entry> entries;
            for (const std::vector<const Written*>& group : groups)
            {
                const CurveEvent* inForce = nullptr;
                for (auto item = group.rbegin(); item != group.rend() && inForce == nullptr; ++item)
                    if ((*item)->event.startTick < runStart)
                        inForce = &(*item)->event;
                if (inForce == nullptr)
                {
                    const CurveEvent& sample = group.front()->event;
                    const double value = sample.combine == CurveCombine::Add ? 0 : initialValue(ctx, sample.parameter);
                    CurveEvent event = sample;
                    event.sourceIds.clear();
                    event.segmentIndex = 0;
                    event.pass = first.pass;
                    event.startTick = first.startTick;
                    event.durationTicks = 0;
                    event.fromValue = value;
                    event.toValue = value;
                    entries.push_back({ -1, std::move(event) });
                }
                else if (inForce->startTick + inForce->durationTicks > runStart)
                    entries.push_back({ inForce->startTick, copy(*inForce, runStart, first.pass) });
                else
                {
                    CurveEvent event = *inForce;
                    event.pass = first.pass;
                    event.startTick = first.startTick;
                    event.durationTicks = 0;
                    event.fromValue = inForce->toValue;
                    entries.push_back({ inForce->startTick, std::move(event) });
                }
            }
            std::stable_sort(entries.begin(), entries.end(),
                             [](const Entry& a, const Entry& b) { return a.order < b.order; });
            for (Entry& entry : entries)
                push(std::move(entry.event));
        }

        for (const Written& item : written)
        {
            const CurveEvent& event = item.event;
            const bool atScoreEnd = runEnd == scoreEnd && event.startTick == runEnd;
            if (event.startTick < runStart || (event.startTick >= runEnd && !atScoreEnd))
                continue;
            const int pass = passAt(event.startTick);
            if (item.passes && !contains(*item.passes, pass))
                continue;
            CurveEvent copied = copy(event, event.startTick, pass);
            for (const CurveEventOverride* override : overrides)
                if (override->sourceId == event.sourceIds.front() && override->segmentIndex == event.segmentIndex
                    && contains(*override->passes, pass))
                    applyOverride(copied, *override);
            push(std::move(copied));
        }
        from = to + 1;
    }

    std::stable_sort(result.begin(), result.end(),
                     [](const CurveEvent& a, const CurveEvent& b) { return a.startTick < b.startTick; });
    return result;
}

} // namespace

std::vector<CurveEvent> buildCurves(const TrackContext& ctx) { return playCurves(ctx, writeCurves(ctx)); }

} // namespace msd
