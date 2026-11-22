#include "playback/Swells.h"

#include <algorithm>
#include <optional>
#include <set>

#include "playback/JsMath.h"

namespace msd
{
namespace
{

struct Shape
{
    double start = 0;
    double end = 0;
    double rise = 0;
    double fall = 0;
    bool rises = false; // whether the page leaves room for the rise
    bool falls = false; // and for the fall
};

struct Plan
{
    std::size_t first; // note indexes in the voice
    std::size_t last;
    double ratio;
    bool slur;
};

} // namespace

std::vector<CurveEvent> buildSwells(const TrackContext& ctx, const std::vector<NoteEvent>& notes,
                                    const std::vector<CurveEvent>& curves,
                                    const std::unordered_map<std::string, SlurPosition>& phrases)
{
    std::vector<CurveEvent> swells;
    if (!ctx.profile.swell)
        return swells;
    const SwellSettings& settings = *ctx.profile.swell;
    const TempoMap& tempo = ctx.tempo;

    // The phrase as written: a slur that playing splits at a repeated pitch is still one.
    const auto slurOf = [&](const NoteEvent& note) -> std::optional<SlurPosition> {
        const auto found = phrases.find(note.id);
        return found != phrases.end() ? std::optional { found->second } : note.slur;
    };
    // Rounded, so a note exactly at the limit is not counted as longer by a rounding error.
    const auto msBetween = [&](double from, double ticks) {
        return jsRound((tempo.tickToMs(from + ticks) - tempo.tickToMs(from)) * 1000) / 1000;
    };
    const auto lengthMs = [&](const NoteEvent& note) { return msBetween(note.startTick, note.durationTicks); };
    // Written length in real time: what tells a slow slur note from a fast one.
    const auto notatedMs = [&](const NoteEvent& note) { return msBetween(note.startTick, note.notatedTicks); };
    const double slurMinMs = settings.slurMinMs.value_or(settings.minMs);

    // Written dynamics: the base layer of the curve, without accents or other swells.
    std::vector<const CurveEvent*> written;
    for (const CurveEvent& curve : curves)
        if (curve.parameter == CurveParameter { BuiltInCurveParameter::Dynamics }
            && (!curve.combine || curve.combine == CurveCombine::Replace))
            written.push_back(&curve);

    // Whether the page changes this voice's dynamic between `from` and `to`.
    const auto changesWritten = [&](const NoteEvent& note, double from, double to) {
        return std::any_of(written.begin(), written.end(), [&](const CurveEvent* curve) {
            if (curve->appliesTo
                && std::none_of(curve->appliesTo->begin(), curve->appliesTo->end(), [&](const CurveTarget& scope) {
                       return scope.staffId == note.staffId
                              && (!scope.voiceIndex || *scope.voiceIndex == note.voiceIndex);
                   }))
                return false;
            const double end = curve->startTick + curve->durationTicks;
            // A dynamic on the note's own first beat is its level, not a change.
            return curve->durationTicks > 0 ? curve->startTick < to && end > from
                                            : curve->startTick > from && curve->startTick < to;
        });
    };

    const auto shapeOf = [&](const NoteEvent& first, const NoteEvent& last, double ratio, bool slur) {
        Shape shape;
        shape.start = first.startTick;
        shape.end = last.startTick + last.durationTicks;
        shape.rise = first.durationTicks * ratio;
        shape.fall = last.durationTicks * ratio;
        // A slur rises only out of a slow first note and falls only into a slow last one:
        // the same line that sets a fast run (marcato) apart from a slow slur (sustain).
        const auto slow = [&](const NoteEvent& note) { return !slur || notatedMs(note) > slurMinMs; };
        shape.rises = slow(first) && !changesWritten(first, shape.start, shape.start + shape.rise);
        shape.falls = slow(last) && !changesWritten(last, shape.end - shape.fall, shape.end);
        return shape;
    };

    // `resetTick`: where the written level comes back, or none when another swell follows.
    const auto swell = [&](const NoteEvent& first, const Shape& shape, std::optional<double> resetTick) {
        const NodeId& sourceId = first.sourceIds.front();
        const double low = settings.startLevel;
        const auto add = [&](int segmentIndex, double startTick, double durationTicks, double fromValue, double toValue,
                             const std::optional<CurveShape>& curveShape) {
            CurveEvent event {
                .id = sourceId + "@" + std::to_string(first.pass) + "#swell" + std::to_string(segmentIndex),
                .sourceIds = { sourceId },
                .trackId = ctx.track.id,
                .pass = first.pass,
                .segmentIndex = segmentIndex,
                .parameter = BuiltInCurveParameter::Dynamics,
                .appliesTo = std::vector<CurveTarget> { { first.staffId, first.voiceIndex } },
                .combine = CurveCombine::Scale,
                .startTick = startTick,
                .durationTicks = durationTicks,
                .fromValue = fromValue,
                .toValue = toValue,
            };
            if (curveShape)
            {
                event.curveType = curveShape->curveType;
                event.steepness = curveShape->steepness;
            }
            swells.push_back(std::move(event));
        };
        // Indexes stay 0 rise, 1 fall, 2 reset, also when one of them is left out.
        if (shape.rises)
            add(0, shape.start, shape.rise, low, 1, settings.rise);
        if (shape.falls)
            add(1, shape.end - shape.fall, shape.fall, 1, low, settings.fall);
        // Back to the written level at the next note, so that it is not left lowered and
        // the silence before it does not jump up.
        if (shape.falls && resetTick)
            add(2, *resetTick, 0, low, 1, std::nullopt);
    };

    // Notes of each voice in time order. One swell per point in time: the notes of a chord
    // share it. Notes the engine made out of one written note (tremolo strokes, glissando
    // steps) are left out.
    std::vector<std::vector<const NoteEvent*>> voices; // in the order voices first appear
    std::unordered_map<std::string, std::size_t> voiceIndex;
    for (const NoteEvent& note : notes)
    {
        if (note.id.find('~') != std::string::npos)
            continue;
        const auto [found, added] =
            voiceIndex.try_emplace(note.staffId + ":" + std::to_string(note.voiceIndex), voices.size());
        if (added)
            voices.emplace_back();
        std::vector<const NoteEvent*>& list = voices[found->second];
        if (list.empty() || list.back()->startTick != note.startTick)
            list.push_back(&note);
    }

    for (std::vector<const NoteEvent*>& voice : voices)
    {
        std::stable_sort(voice.begin(), voice.end(),
                         [](const NoteEvent* a, const NoteEvent* b) { return a->startTick < b->startTick; });
        std::vector<Plan> planned;
        std::optional<std::size_t> phraseStart;
        for (std::size_t i = 0; i < voice.size(); ++i)
        {
            const std::optional<SlurPosition> slur = slurOf(*voice[i]);
            if (slur == SlurPosition::First)
                phraseStart = i;
            else if (slur == SlurPosition::Last)
            {
                if (phraseStart)
                    planned.push_back({ *phraseStart, i, settings.slurRatio, true });
                phraseStart.reset();
            }
            else if (!slur && lengthMs(*voice[i]) > settings.minMs)
                planned.push_back({ i, i, settings.noteRatio, false });
        }
        std::vector<Shape> shapes;
        for (const Plan& plan : planned)
            shapes.push_back(shapeOf(*voice[plan.first], *voice[plan.last], plan.ratio, plan.slur));
        // Notes that start with a rise from the low level: no reset is needed before them.
        std::set<std::size_t> risesAt;
        for (std::size_t i = 0; i < planned.size(); ++i)
            if (shapes[i].rises)
                risesAt.insert(planned[i].first);
        for (std::size_t i = 0; i < planned.size(); ++i)
        {
            const std::size_t last = planned[i].last;
            const NoteEvent& lastNote = *voice[last];
            std::optional<double> resetTick;
            if (last + 1 >= voice.size())
                resetTick = lastNote.startTick + lastNote.durationTicks;
            else if (!risesAt.contains(last + 1))
                resetTick = voice[last + 1]->startTick;
            swell(*voice[planned[i].first], shapes[i], resetTick);
        }
    }
    return swells;
}

} // namespace msd
