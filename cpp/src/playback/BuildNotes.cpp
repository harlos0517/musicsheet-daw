#include "playback/BuildNotes.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>

#include "playback/Curves.h"
#include "playback/Interpretation.h"
#include "playback/JsMath.h"

namespace msd
{
namespace
{

const CurveParameter dynamicsCurve = BuiltInCurveParameter::Dynamics;

// Lengths in milliseconds come out of the tempo map with rounding errors
// (125.00000001): a note exactly at a rule's limit must still count as inside it.
constexpr double msTolerance = 0.001;

// ── Small helpers ──────────────────────────────────────────────────────────────

template <class T>
bool contains(const std::vector<T>& values, const T& value)
{
    return std::find(values.begin(), values.end(), value) != values.end();
}

// Keeps the first of each value, in order. TS: [...new Set(values)]
std::vector<std::string> unique(const std::vector<std::string>& values)
{
    std::vector<std::string> result;
    for (const std::string& value : values)
        if (!contains(result, value))
            result.push_back(value);
    return result;
}

// MIDI note number (C4 = 60). The same as in layout/Transpose.cpp, which playback
// does not link.
int midiNumber(const Pitch& pitch)
{
    static constexpr int semitones[] = { 0, 2, 4, 5, 7, 9, 11 }; // C D E F G A B
    return (pitch.octave + 1) * 12 + semitones[static_cast<int>(pitch.step)] + pitch.alter;
}

const NodeId& noteId(const ChordNote& note)
{
    return std::visit([](const auto& n) -> const NodeId& { return n.id; }, note);
}

const std::optional<Tie>& noteTie(const ChordNote& note)
{
    return std::visit([](const auto& n) -> const std::optional<Tie>& { return n.tie; }, note);
}

const NodeId& eventId(const VoiceEvent& event)
{
    return std::visit([](const auto& e) -> const NodeId& { return e.id; },
                      static_cast<const VoiceEvent::variant&>(event));
}

// Ids of the notes a note is tied to. More than one when the tie crosses a repeat: the
// last note before a repeat sign can tie into the first and the second ending.
std::vector<NodeId> tieTargets(const ChordNote& note)
{
    std::vector<NodeId> ids;
    if (const auto& tie = noteTie(note))
        if (const auto* targets = std::get_if<std::vector<TieTarget>>(&*tie))
            for (const TieTarget& target : *targets)
                ids.push_back(target.to);
    return ids;
}

int pitchValue(const ChordNote& note)
{
    const auto* pitched = std::get_if<Note>(&note);
    return pitched ? midiNumber(pitched->pitch) : 0;
}

bool isChord(const Placed& placed) { return std::holds_alternative<Chord>(*placed.event); }

// A tremolo that stays one note: the patch plays it.
bool isRollTechnique(std::string_view id) { return id == "core:tremolo" || id == "core:measured-tremolo"; }

// Techniques the engine turns into notes itself; instruments never see them.
bool isGenerated(std::string_view id) { return id == "core:arpeggio" || id == "core:gliss"; }

// TS: /^core:tremolo-\d$/
bool isMeasuredTremolo(std::string_view id)
{
    constexpr std::string_view prefix = "core:tremolo-";
    return id.size() == prefix.size() + 1 && id.starts_with(prefix) && id.back() >= '0' && id.back() <= '9';
}

// ── Note rules ─────────────────────────────────────────────────────────────────

// What a rule can match on besides the event itself.
struct NoteContext
{
    std::vector<ArticulationType> articulations;
    std::optional<bool> graceSlash;     // set for a grace note: whether it is slashed
    std::optional<double> previousTicks; // notated length of the note right before, if they touch
    double beat = 0;                     // played tick the note is written on, before any rule moves it
    std::vector<DynamicType> dynamics;   // written on the note's chord
    std::vector<HairpinType> hairpins;   // running over the note's written span
};

bool matches(const NoteRule& rule, const NoteEvent& event, const NoteContext& context, int tpq, const TempoMap& tempo)
{
    const double notatedQn = event.notatedTicks / tpq;
    // Real length, for rules about what is fast or slow to play: a sixteenth at 60 and an
    // eighth at 120 are the same thing to a player.
    const double at = context.beat;
    if (rule.when)
    {
        const NoteRuleCondition& when = *rule.when;
        if (when.maxNotatedMs)
        {
            const double length = tempo.tickToMs(at + event.notatedTicks) - tempo.tickToMs(at);
            if (length > *when.maxNotatedMs + msTolerance)
                return false;
        }
        if (when.maxPreviousNotatedMs)
        {
            if (!context.previousTicks)
                return false;
            const double length = tempo.tickToMs(at) - tempo.tickToMs(at - *context.previousTicks);
            if (length > *when.maxPreviousNotatedMs + msTolerance)
                return false;
        }
        if (when.maxNotatedQn && notatedQn > *when.maxNotatedQn)
            return false;
        if (when.minNotatedQn && notatedQn < *when.minNotatedQn)
            return false;
        if (when.maxPreviousNotatedQn
            && (!context.previousTicks || *context.previousTicks / tpq > *when.maxPreviousNotatedQn))
            return false;
        if (when.slurPosition && event.slur != when.slurPosition)
            return false;
        if (when.hairpin && !contains(context.hairpins, *when.hairpin))
            return false;
        if (when.passes && !contains(*when.passes, event.pass))
            return false;
    }
    return std::visit(
        [&](const auto& match) {
            using M = std::decay_t<decltype(match)>;
            if constexpr (std::is_same_v<M, ArticulationMatch>)
                return contains(context.articulations, match.type);
            else if constexpr (std::is_same_v<M, TechniqueMatch>)
                return contains(event.techniques, match.techniqueId);
            else if constexpr (std::is_same_v<M, SlurRuleMatch>)
                return event.slur.has_value() && (!match.position || match.position == event.slur);
            else if constexpr (std::is_same_v<M, GraceMatch>)
                return context.graceSlash.has_value() && (!match.slash || *match.slash == *context.graceSlash);
            else if constexpr (std::is_same_v<M, DynamicMatch>)
                return contains(context.dynamics, match.type);
            else
                return true;
        },
        rule.match);
}

// `fullTicks` is the length the note has before any rule: its notated length, less what
// grace notes took. "Ratio" is measured against it. Lengths in a profile are in quarter
// notes, so `tpq` turns them into ticks.
void act(NoteEvent& event, const NoteRuleAction& action, int tpq, double fullTicks)
{
    if (const auto* offset = std::get_if<StartOffsetAction>(&action))
    {
        const double ticks = std::min(offset->qn * tpq, event.durationTicks - 1);
        event.startTick += ticks;
        event.durationTicks -= ticks;
    }
    else if (const auto* add = std::get_if<AddTechniqueAction>(&action))
    {
        if (!contains(event.techniques, add->techniqueId))
            event.techniques.push_back(add->techniqueId);
    }
    else if (const auto* remove = std::get_if<RemoveTechniqueAction>(&action))
        std::erase(event.techniques, remove->techniqueId);
    else if (const auto* velocity = std::get_if<VelocityAction>(&action))
        event.velocity = clamp01(applyMode(velocity->op, event.velocity, velocity->value));
    else if (const auto* duration = std::get_if<DurationAction>(&action))
    {
        double ticks = event.durationTicks;
        switch (duration->op)
        {
        case DurationOp::SetQn: ticks = duration->value * tpq; break;
        case DurationOp::SetRatio: ticks = fullTicks * duration->value; break;
        case DurationOp::Add: ticks += duration->value * tpq; break;
        case DurationOp::Scale: ticks *= duration->value; break;
        }
        event.durationTicks = std::max(1.0, ticks);
    }
    // RollAttackAction adds a note: done by the caller.
}

void override(NoteEvent& event, const std::vector<const NoteEventOverride*>& overrides)
{
    for (const NoteEventOverride* o : overrides)
    {
        if (!contains(event.sourceIds, o->sourceId))
            continue;
        if (o->passes && !contains(*o->passes, event.pass))
            continue;
        if (o->pitch)
            event.pitch = *o->pitch;
        if (o->startTickOffset)
        {
            event.startTick += *o->startTickOffset;
            event.durationTicks = std::max(1.0, event.durationTicks - *o->startTickOffset);
        }
        if (o->durationTicks)
            event.durationTicks =
                std::max(1.0, applyMode(o->durationTicks->mode, event.durationTicks, o->durationTicks->value));
        if (o->velocity)
            event.velocity = clamp01(applyMode(o->velocity->mode, event.velocity, o->velocity->value));
        if (o->techniques)
        {
            const std::vector<std::string> removed = o->techniques->remove.value_or(std::vector<std::string> {});
            std::vector<std::string> techniques;
            for (const std::string& id : event.techniques)
                if (!contains(removed, id))
                    techniques.push_back(id);
            for (const std::string& id : o->techniques->add.value_or(std::vector<std::string> {}))
                techniques.push_back(id);
            event.techniques = unique(techniques);
        }
        if (o->startOffsetMs)
            event.startOffsetMs = o->startOffsetMs;
    }
}

// ── The track ──────────────────────────────────────────────────────────────────

// Everything buildNotes works out about the track before it plays it, and the steps
// it plays it with. TS keeps these as closures inside buildNotes.
class NoteBuilder
{
public:
    NoteBuilder(const TrackContext& ctx, const std::vector<CurveEvent>& curves)
        : ctx_(ctx)
        , curves_(curves)
        , dynamics_(curves, dynamicsCurve)
        , tpq_(ctx.music.tpq)
        , initialDynamics_(ctx.profile.initialDynamics.value_or(defaultInitialDynamics))
    {
        if (const auto found = ctx.index.chords.find(ctx.track.id); found != ctx.index.chords.end())
            chords_ = &found->second;
        for (const MusicEventOverride& o : ctx.setup.overrides)
            if (const auto* note = std::get_if<NoteEventOverride>(&o))
                overrides_.push_back(note);
        collectTies();
        collectMarkings();
        collectSlurs();
        collectVoices();
        markConnectedRuns();
        writtenPhrases_ = slurPositions_;
        splitAtRepeatedPitches();
        collectGlissandi();
    }

    BuiltNotes build()
    {
        for (std::size_t playedAt = 0; playedAt < ctx_.played.size(); ++playedAt)
            for (const Placed& placed : chords())
                if (placed.measureIndex == ctx_.played[playedAt].measureIndex)
                    playChord(placed, playedAt);
        shortenBeforeEarlyGraces();
        std::stable_sort(notes_.begin(), notes_.end(),
                         [](const NoteEvent& a, const NoteEvent& b) { return a.startTick < b.startTick; });
        return { std::move(notes_), std::move(phrases_) };
    }

private:
    struct Marking
    {
        const TechniqueMarking* marking;
        double tick;
    };
    struct CustomSpan
    {
        const CustomSpanner* spanner;
        double start;
        double end;
    };
    struct HairpinSpan
    {
        const Hairpin* hairpin;
        double start;
        double end;
    };
    struct DynamicMark
    {
        const Dynamic* dynamic;
        double tick;
    };
    struct TremoloPair
    {
        const Placed* start;
        const Placed* end;
    };
    struct TieChain
    {
        std::vector<NodeId> sourceIds;
        double ticks;
        double rollTicks; // of a tremolo, how long it lasts, up to the first tied note without one
    };
    struct EarlyGrace
    {
        NodeId staffId;
        int voiceIndex;
        double startTick;
        double beat;
    };

    const std::vector<Placed>& chords() const
    {
        static const std::vector<Placed> none;
        return chords_ ? *chords_ : none;
    }

    std::optional<double> tickOf(const Anchor& anchor, bool atEnd = false) const
    {
        const std::optional<Position> position = resolveAnchor(ctx_.index, anchor, atEnd);
        if (!position)
            return std::nullopt;
        return absTick(ctx_.timeline, *position);
    }

    std::string groupOf(const std::string& id) const
    {
        for (const TechniqueDefinition& technique : ctx_.setup.techniques)
            if (technique.id == id)
                return technique.group.value_or(id);
        return id;
    }

    static bool inScope(const Placed& placed, const std::optional<std::vector<NodeId>>& staffIds,
                        const std::optional<std::vector<int>>& voiceIndexes)
    {
        return (!staffIds || contains(*staffIds, placed.staffId))
               && (!voiceIndexes || contains(*voiceIndexes, placed.voiceIndex));
    }

    static bool sameVoice(const Placed& a, const Placed& b)
    {
        return a.staffId == b.staffId && a.voiceIndex == b.voiceIndex;
    }

    // Notes that continue a tie, with the chord the tie comes from.
    void collectTies()
    {
        for (const Placed& chord : chords())
            for (const ChordNote& note : chord.chord().notes)
                for (const NodeId& target : tieTargets(note))
                    tieSources_.insert_or_assign(target, &chord);
    }

    // Technique markings and spanners, dynamics and hairpins, placed in written order.
    void collectMarkings()
    {
        const Track& track = ctx_.track;
        for (const Attachment& attachment : track.attachments)
        {
            if (const auto* marking = std::get_if<TechniqueMarking>(&attachment))
            {
                if (const auto tick = tickOf(marking->at))
                    markings_.push_back({ marking, *tick });
            }
            // Dynamics by the tick they are written at, to tell a rule which note carries one.
            else if (const auto* dynamic = std::get_if<Dynamic>(&attachment);
                     dynamic && !dynamic->beforeTick.value_or(false))
            {
                if (const auto tick = tickOf(dynamic->at))
                    dynamicMarks_.push_back({ dynamic, *tick });
            }
        }
        std::stable_sort(markings_.begin(), markings_.end(),
                         [](const Marking& a, const Marking& b) { return a.tick < b.tick; });

        for (const TrackSpanner& spanner : track.trackSpanners)
        {
            if (const auto* custom = std::get_if<CustomSpanner>(&spanner))
            {
                const auto start = tickOf(custom->start);
                const auto end = tickOf(custom->end, custom->includeEnd.value_or(false));
                if (start && end)
                    customSpans_.push_back({ custom, *start, *end });
            }
            else if (const auto* hairpin = std::get_if<Hairpin>(&spanner))
            {
                // Hairpins in written order, to tell a rule that a note sits under one.
                const auto start = tickOf(hairpin->start);
                const auto end = tickOf(hairpin->end, hairpin->includeEnd.value_or(false));
                if (start && end)
                    hairpinSpans_.push_back({ hairpin, *start, *end });
            }
        }

        // Two-note tremolos: both chords sound together for the length of the two.
        for (const TrackSpanner& spanner : track.trackSpanners)
        {
            const auto* custom = std::get_if<CustomSpanner>(&spanner);
            if (custom == nullptr || custom->techniqueId != "core:tremolo-pair")
                continue;
            const auto* startAnchor = std::get_if<ElementAnchor>(&custom->start);
            const auto* endAnchor = std::get_if<ElementAnchor>(&custom->end);
            if (startAnchor == nullptr || endAnchor == nullptr)
                continue;
            const auto start = ctx_.index.elements.find(startAnchor->id);
            const auto end = ctx_.index.elements.find(endAnchor->id);
            if (start == ctx_.index.elements.end() || end == ctx_.index.elements.end() || !isChord(start->second)
                || !isChord(end->second))
                continue;
            const TremoloPair pair { &start->second, &end->second };
            tremoloPairs_.insert_or_assign(start->second.chord().id, pair);
            tremoloPairs_.insert_or_assign(end->second.chord().id, pair);
        }
    }

    std::vector<DynamicType> dynamicsFor(const Placed& placed) const
    {
        std::vector<DynamicType> types;
        for (const DynamicMark& mark : dynamicMarks_)
            if (mark.tick == placed.absTick && inScope(placed, mark.dynamic->staffIds, mark.dynamic->voiceIndexes))
                types.push_back(mark.dynamic->type);
        return types;
    }

    std::vector<HairpinType> hairpinsFor(const Placed& placed) const
    {
        std::vector<HairpinType> types;
        for (const HairpinSpan& span : hairpinSpans_)
            if (span.start < placed.absTick + placed.ticks && span.end > placed.absTick
                && inScope(placed, span.hairpin->staffIds, std::nullopt))
                types.push_back(span.hairpin->type);
        return types;
    }

    // Techniques in force on a chord. Resolved in written order, so a jump back lands in
    // the state the page shows there, not the state the music was in before the jump.
    std::vector<std::string> techniquesFor(const Placed& placed, int pass) const
    {
        std::vector<std::pair<std::string, std::string>> lasting; // group → technique, in first-set order
        std::vector<std::string> result;
        const Chord& chord = placed.chord();
        const auto onChord = [&](const NodeId& id) {
            if (id == chord.id)
                return true;
            return std::any_of(chord.notes.begin(), chord.notes.end(),
                               [&](const ChordNote& note) { return noteId(note) == id; });
        };
        for (const Marking& entry : markings_)
        {
            const TechniqueMarking& marking = *entry.marking;
            if (marking.passes && !contains(*marking.passes, pass))
                continue;
            if (!inScope(placed, marking.staffIds, marking.voiceIndexes))
                continue;
            if (marking.scope == TechniqueScope::This)
            {
                const auto* element = std::get_if<ElementAnchor>(&marking.at);
                if (element && onChord(element->id))
                    result.push_back(marking.techniqueId);
            }
            else if (entry.tick <= placed.absTick)
            {
                const std::string group = groupOf(marking.techniqueId);
                const auto found =
                    std::find_if(lasting.begin(), lasting.end(), [&](const auto& item) { return item.first == group; });
                if (found != lasting.end())
                    found->second = marking.techniqueId;
                else
                    lasting.emplace_back(group, marking.techniqueId);
            }
        }
        for (const CustomSpan& span : customSpans_)
        {
            const CustomSpanner& spanner = *span.spanner;
            if (spanner.passes && !contains(*spanner.passes, pass))
                continue;
            if (!inScope(placed, spanner.staffIds, spanner.voiceIndexes))
                continue;
            if (span.start <= placed.absTick && placed.absTick < span.end)
                result.push_back(spanner.techniqueId);
        }
        std::vector<std::string> all;
        for (const auto& [group, technique] : lasting)
            all.push_back(technique);
        all.insert(all.end(), result.begin(), result.end());
        return unique(all);
    }

    bool rolls(const Placed& placed, int pass) const
    {
        const std::vector<std::string> techniques = techniquesFor(placed, pass);
        return std::any_of(techniques.begin(), techniques.end(),
                           [](const std::string& id) { return id == "core:tremolo" || isMeasuredTremolo(id); });
    }

    // Slur membership: chords in the voice of either end, between the two ends.
    void collectSlurs()
    {
        for (const TrackSpanner& spanner : ctx_.track.trackSpanners)
        {
            const auto* slur = std::get_if<Slur>(&spanner);
            if (slur == nullptr)
                continue;
            const auto startFound = ctx_.index.elements.find(slur->start.id);
            const auto endFound = ctx_.index.elements.find(slur->end.id);
            if (startFound == ctx_.index.elements.end() || endFound == ctx_.index.elements.end())
                continue;
            const Placed* start = &startFound->second;
            const Placed* end = &endFound->second;
            // A slur that ends on the tied-over part of a note ends on that note: the part it
            // ties into is not played as a note of its own.
            for (int guard = 0; guard < 64 && isChord(*end); ++guard)
            {
                const std::vector<ChordNote>& notes = end->chord().notes;
                if (notes.empty() || !continuesTied(*end))
                    break;
                end = tieSources_.at(noteId(notes.front()));
            }
            if (end->absTick <= start->absTick)
                continue; // one note, held: nothing to slur
            const NodeId& startId = eventId(*start->event);
            const NodeId& endId = eventId(*end->event);
            for (const Placed& chord : chords())
            {
                if (chord.absTick < start->absTick || chord.absTick > end->absTick)
                    continue;
                if (!sameVoice(chord, *start) && !sameVoice(chord, *end))
                    continue;
                SlurPosition position = SlurPosition::Inner;
                if (chord.chord().id == startId)
                    position = SlurPosition::First;
                else if (chord.chord().id == endId)
                    position = SlurPosition::Last;
                slurPositions_.try_emplace(chord.chord().id, position);
            }
        }
    }

    // The chords of each voice in order, to know what comes right before a note and, in
    // connected phrasing, which unslurred notes run into each other.
    void collectVoices()
    {
        std::unordered_map<std::string, std::size_t> voiceIndex;
        for (const Placed& chord : chords())
        {
            const std::string key = chord.staffId + ":" + std::to_string(chord.voiceIndex);
            const auto [found, added] = voiceIndex.try_emplace(key, byVoice_.size());
            if (added)
                byVoice_.emplace_back();
            byVoice_[found->second].push_back(&chord);
        }
    }

    bool continuesTied(const Placed& chord) const
    {
        const std::vector<ChordNote>& notes = chord.chord().notes;
        return std::all_of(notes.begin(), notes.end(),
                           [&](const ChordNote& note) { return tieSources_.contains(noteId(note)); });
    }

    // Runs `visit(chord, previous)` for each chord of each voice that starts a note, with
    // the chord before it (or null); chords that only continue ties are skipped, and
    // `previousEnd` is where the chord before ends, ties included.
    template <class F>
    void forEachVoiceChord(F&& visit) const
    {
        for (const std::vector<const Placed*>& voice : byVoice_)
        {
            const Placed* previous = nullptr;
            double previousEnd = -1;
            for (const Placed* chord : voice)
            {
                // A chord that only continues ties is part of the note before it.
                if (continuesTied(*chord) && previous && chord->absTick == previousEnd)
                {
                    previousEnd = chord->absTick + chord->ticks;
                    continue;
                }
                visit(*chord, previous, previousEnd);
                previous = chord;
                previousEnd = chord->absTick + chord->ticks;
            }
        }
    }

    // Connected phrasing: unbroken runs of single notes are played as if slurred.
    void markConnectedRuns()
    {
        const auto breaksRun = [&](const Placed& placed, const std::vector<std::string>& techniques) {
            const Chord& chord = placed.chord();
            return chord.notes.size() != 1 || slurPositions_.contains(chord.id)
                   || std::any_of(chord.articulations.begin(), chord.articulations.end(),
                                  [](const Articulation& a) {
                                      return a.type == ArticulationType::Staccato
                                             || a.type == ArticulationType::Staccatissimo;
                                  })
                   || std::any_of(techniques.begin(), techniques.end(), [](const std::string& id) {
                          return id == "core:tremolo" || id.starts_with("core:tremolo-");
                      });
        };
        // core:connected / core:detached markings switch the profile's default from there.
        const auto phrasingOf = [&](const std::vector<std::string>& techniques) {
            for (auto id = techniques.rbegin(); id != techniques.rend(); ++id)
                if (*id == "core:connected" || *id == "core:detached")
                    return *id == "core:connected" ? Phrasing::Connected : Phrasing::Detached;
            return ctx_.profile.defaultPhrasing.value_or(Phrasing::Detached);
        };
        const auto accented = [](const Placed& placed) {
            const std::vector<Articulation>& articulations = placed.chord().articulations;
            return std::any_of(articulations.begin(), articulations.end(),
                               [](const Articulation& a) { return a.type == ArticulationType::Accent; });
        };
        const auto firstPitch = [](const Placed& placed) -> std::optional<int> {
            const std::vector<ChordNote>& notes = placed.chord().notes;
            return notes.empty() ? std::nullopt : std::optional { pitchValue(notes.front()) };
        };

        for (const std::vector<const Placed*>& voice : byVoice_)
        {
            std::vector<const Placed*> run;
            const auto closeRun = [&] {
                if (run.size() >= 2)
                    for (std::size_t i = 0; i < run.size(); ++i)
                        slurPositions_.insert_or_assign(run[i]->chord().id, i == 0 ? SlurPosition::First
                                                                            : i == run.size() - 1
                                                                                ? SlurPosition::Last
                                                                                : SlurPosition::Inner);
                run.clear();
            };
            const Placed* previous = nullptr;
            double previousEnd = -1;
            for (const Placed* chord : voice)
            {
                if (continuesTied(*chord) && previous && chord->absTick == previousEnd)
                {
                    previousEnd = chord->absTick + chord->ticks;
                    continue;
                }
                const bool touches = previous && chord->absTick == previousEnd;
                if (touches)
                    previousTicks_.insert_or_assign(chord->chord().id, previous->ticks);
                // Phrasing is decided once for the page, so markings limited to some passes
                // count as in the first one.
                const std::vector<std::string> techniques = techniquesFor(*chord, 1);
                if (phrasingOf(techniques) == Phrasing::Connected)
                {
                    const bool samePitch = touches && firstPitch(*previous) == firstPitch(*chord);
                    const bool breaks = breaksRun(*chord, techniques);
                    if (!touches || samePitch || breaks || accented(*chord))
                        closeRun();
                    if (!breaks)
                        run.push_back(chord);
                }
                else
                    closeRun();
                previous = chord;
                previousEnd = chord->absTick + chord->ticks;
            }
            closeRun();
        }
    }

    // A repeated pitch cannot be slurred into: in a slur, the note before it ends the
    // phrase and the repeat starts a new one, so it is played detached, on its beat.
    void splitAtRepeatedPitches()
    {
        const auto pitchesOf = [](const Placed& placed) {
            std::vector<int> pitches;
            for (const ChordNote& note : placed.chord().notes)
                pitches.push_back(pitchValue(note));
            std::sort(pitches.begin(), pitches.end());
            return pitches;
        };
        const auto positionOf = [&](const NodeId& id) -> std::optional<SlurPosition> {
            const auto found = slurPositions_.find(id);
            return found == slurPositions_.end() ? std::nullopt : std::optional { found->second };
        };
        forEachVoiceChord([&](const Placed& chord, const Placed* previous, double previousEnd) {
            if (previous == nullptr || chord.absTick != previousEnd)
                return;
            const NodeId& previousId = previous->chord().id;
            const NodeId& id = chord.chord().id;
            const std::optional<SlurPosition> before = positionOf(previousId);
            const std::optional<SlurPosition> here = positionOf(id);
            const bool joined = (before == SlurPosition::First || before == SlurPosition::Inner)
                                && (here == SlurPosition::Inner || here == SlurPosition::Last);
            if (!joined || pitchesOf(*previous) != pitchesOf(chord))
                return;
            if (before == SlurPosition::First)
                slurPositions_.erase(previousId);
            else
                slurPositions_.insert_or_assign(previousId, SlurPosition::Last);
            if (here == SlurPosition::Last)
                slurPositions_.erase(id);
            else
                slurPositions_.insert_or_assign(id, SlurPosition::First);
        });
    }

    // Glissandi: the chord a line starts on, with the pitches of the chord it leads to.
    void collectGlissandi()
    {
        for (const TrackSpanner& spanner : ctx_.track.trackSpanners)
        {
            const auto* custom = std::get_if<CustomSpanner>(&spanner);
            if (custom == nullptr || custom->techniqueId != "core:gliss")
                continue;
            const auto* start = std::get_if<ElementAnchor>(&custom->start);
            const auto* end = std::get_if<ElementAnchor>(&custom->end);
            if (start == nullptr || end == nullptr)
                continue;
            const auto target = ctx_.index.elements.find(end->id);
            if (target == ctx_.index.elements.end() || !isChord(target->second))
                continue;
            std::vector<int> pitches;
            for (const ChordNote& note : target->second.chord().notes)
                if (const auto* pitched = std::get_if<Note>(&note))
                    pitches.push_back(midiNumber(pitched->pitch));
            if (!pitches.empty())
                glissandi_.insert_or_assign(start->id, std::move(pitches));
        }
    }

    std::string kitSound(const UnpitchedNote& note) const
    {
        if (ctx_.track.kit)
            for (const KitEntry& entry : *ctx_.track.kit)
                if (entry.id == note.kitEntryId)
                    return entry.sound;
        return note.kitEntryId;
    }

    EventPitch pitchOf(const ChordNote& note) const
    {
        if (const auto* pitched = std::get_if<Note>(&note))
            return midiNumber(pitched->pitch);
        return KitSound { kitSound(std::get<UnpitchedNote>(note)) };
    }

    double velocityAt(double tick, const CurveScope& scope) const
    {
        return clamp01(dynamics_(tick, initialDynamics_, scope));
    }

    // Applies the rules and overrides to a note and adds it.
    void finish(NoteEvent event, const NoteContext& context)
    {
        const double fullTicks = event.durationTicks;
        const RollAttackAction* attack = nullptr;
        for (const NoteRule& rule : ctx_.profile.noteRules)
        {
            if (!matches(rule, event, context, tpq_, ctx_.tempo))
                continue;
            for (const NoteRuleAction& action : rule.actions)
            {
                if (const auto* roll = std::get_if<RollAttackAction>(&action))
                    attack = roll;
                act(event, action, tpq_, fullTicks);
            }
        }
        override(event, overrides_);
        // A roll that starts with a single stroke: the stroke is a plain note of its own.
        if (attack && std::any_of(event.techniques.begin(), event.techniques.end(), isRollTechnique))
        {
            const double ticks =
                attack->qn ? std::min(*attack->qn * tpq_, event.durationTicks / 2) : event.durationTicks;
            NoteEvent stroke = event;
            stroke.id = event.id + "~attack";
            std::erase_if(stroke.techniques, isRollTechnique);
            stroke.notatedTicks = ticks;
            stroke.durationTicks = ticks;
            notes_.push_back(std::move(stroke));
            if (!attack->together && attack->qn)
            {
                // The roll comes in after the stroke and still ends where it was written to.
                event.startTick += ticks;
                event.durationTicks -= ticks;
                event.notatedTicks -= ticks;
            }
        }
        notes_.push_back(std::move(event));
    }

    // Length of a note with everything tied to it, and the ids of the notes it covers.
    TieChain tieChain(const ChordNote& note, const Placed& placed, std::size_t playedAt) const
    {
        const std::vector<PlayedMeasure>& played = ctx_.played;
        TieChain chain { { noteId(note) }, placed.ticks, 0 };
        const bool rolling = rolls(placed, played[playedAt].pass);
        chain.rollTicks = rolling ? chain.ticks : 0;
        const ChordNote* current = &note;
        std::size_t measure = playedAt;
        for (int guard = 0; guard < 100000; ++guard)
        {
            // Follow the tie only into a measure that is played right after this one. With
            // several targets (a tie into both endings) that picks the ending being played;
            // with none, the note ends at this barline.
            const int here = played[measure].measureIndex;
            const std::optional<int> after =
                measure + 1 < played.size() ? std::optional { played[measure + 1].measureIndex } : std::nullopt;
            const Placed* target = nullptr;
            NodeId targetId;
            for (const NodeId& id : tieTargets(*current))
            {
                const auto found = ctx_.index.elements.find(id);
                if (found == ctx_.index.elements.end())
                    continue;
                const int targetMeasure = found->second.measureIndex;
                if (targetMeasure == here || targetMeasure == after)
                {
                    target = &found->second;
                    targetId = id;
                    break;
                }
            }
            if (target == nullptr || !isChord(*target))
                break;
            const std::vector<ChordNote>& targetNotes = target->chord().notes;
            const auto next = std::find_if(targetNotes.begin(), targetNotes.end(),
                                           [&](const ChordNote& n) { return noteId(n) == targetId; });
            if (next == targetNotes.end())
                break;
            if (target->measureIndex != here)
                ++measure;
            chain.sourceIds.push_back(noteId(*next));
            if (rolling && chain.rollTicks == chain.ticks && rolls(*target, played[measure].pass))
                chain.rollTicks += target->ticks;
            chain.ticks += target->ticks;
            current = &*next;
        }
        return chain;
    }

    // Whether a tied-to note is the continuation of a note that was just played. If its
    // tie comes from a measure that was not played right before (after a jump, or in a
    // second ending), it sounds as a new note.
    bool continuesTie(const ChordNote& note, const Placed& placed, std::size_t playedAt) const
    {
        const auto source = tieSources_.find(noteId(note));
        if (source == tieSources_.end())
            return false;
        if (source->second->measureIndex == placed.measureIndex)
            return source->second->absTick < placed.absTick;
        return playedAt > 0 && ctx_.played[playedAt - 1].measureIndex == source->second->measureIndex;
    }

    void playChord(const Placed& placed, std::size_t playedAt)
    {
        const PlayedMeasure& playedMeasure = ctx_.played[playedAt];
        const int pass = playedMeasure.pass;
        const std::string passText = "@" + std::to_string(pass);
        const Chord& chord = placed.chord();
        const CurveScope scope { placed.staffId, placed.voiceIndex };
        const double beat = playedMeasure.startTick + placed.tick;
        const std::vector<std::string> techniques = techniquesFor(placed, pass);
        const auto slurFound = slurPositions_.find(chord.id);
        const std::optional<SlurPosition> slur =
            slurFound == slurPositions_.end() ? std::nullopt : std::optional { slurFound->second };
        std::vector<ArticulationType> articulations;
        for (const Articulation& a : chord.articulations)
            articulations.push_back(a.type);
        NoteEvent base;
        base.trackId = ctx_.track.id;
        base.staffId = placed.staffId;
        base.voiceIndex = placed.voiceIndex;
        base.pass = pass;
        base.velocity = velocityAt(beat, scope);

        // Grace notes: each takes `ratio` of its notated length, either before the beat or
        // out of the start of the main note. They never take more than half of it.
        const std::optional<GraceChords>& graces = chord.graceNotes;
        const bool slash = graces && graces->slash.value_or(false);
        const GraceNoteSettings& graceNotes = ctx_.profile.graceNotes ? *ctx_.profile.graceNotes : defaultGraceNotes;
        const GraceNoteTiming& graceSetup = slash ? graceNotes.acciaccatura : graceNotes.appoggiatura;
        const bool onBeat = graceSetup.placement.value_or(slash ? BeatPlacement::BeforeBeat : BeatPlacement::OnBeat)
                            == BeatPlacement::OnBeat;
        std::vector<double> graceLengths;
        double graceTotal = 0;
        if (graces)
            for (const GraceChord& graceChord : graces->chords)
                graceLengths.push_back(durationTicks(graceChord.duration, tpq_) * graceSetup.ratio);
        for (const double length : graceLengths)
            graceTotal += length;
        if (onBeat && graceTotal > placed.ticks / 2)
        {
            for (double& length : graceLengths)
                length = length * placed.ticks / 2 / graceTotal;
            graceTotal = placed.ticks / 2;
        }
        const double mainStart = onBeat ? beat + graceTotal : beat;
        double graceStart = onBeat ? beat : beat - graceTotal;
        if (!onBeat && graceTotal > 0)
            earlyGraces_.push_back({ placed.staffId, placed.voiceIndex, graceStart, beat });
        if (graces)
            for (std::size_t i = 0; i < graces->chords.size(); ++i)
            {
                const GraceChord& graceChord = graces->chords[i];
                NoteContext context;
                for (const Articulation& a : graceChord.articulations)
                    context.articulations.push_back(a.type);
                context.graceSlash = slash;
                context.beat = beat;
                for (const ChordNote& note : graceChord.notes)
                {
                    NoteEvent event = base;
                    event.id = noteId(note) + passText;
                    event.sourceIds = { noteId(note) };
                    event.pitch = pitchOf(note);
                    event.startTick = graceStart;
                    event.notatedTicks = durationTicks(graceChord.duration, tpq_);
                    event.durationTicks = graceLengths[i];
                    event.techniques = techniques;
                    finish(std::move(event), context);
                }
                graceStart += graceLengths[i];
            }

        // Marks that are played as several notes. These are fixed behaviours for now: the
        // profile types have no way yet to describe a rule that creates notes. A measured
        // tremolo stays one note of its written length: the sample library plays the
        // repetitions (measured tremolo, measured repetitions, double tongue).
        const bool measured = std::any_of(techniques.begin(), techniques.end(),
                                          [](const std::string& id) { return isMeasuredTremolo(id); });
        std::vector<std::string> sounding;
        for (const std::string& id : techniques)
            if (!isGenerated(id))
                sounding.push_back(id);
        if (measured)
            sounding.push_back("core:measured-tremolo");
        std::optional<std::vector<const ChordNote*>> rolled;
        if (contains(techniques, std::string("core:arpeggio")))
        {
            rolled.emplace();
            for (const ChordNote& note : chord.notes)
                rolled->push_back(&note);
            std::stable_sort(rolled->begin(), rolled->end(),
                             [](const ChordNote* a, const ChordNote* b) { return pitchValue(*a) < pitchValue(*b); });
        }
        const auto glissFound = glissandi_.find(chord.id);
        const std::vector<int>* glissTo = glissFound == glissandi_.end() ? nullptr : &glissFound->second;
        const std::vector<DynamicType> dynamicsOn = dynamicsFor(placed);
        const std::vector<HairpinType> hairpinsOn = hairpinsFor(placed);
        std::optional<int> strokes;
        for (const int n : { 1, 2 })
            if (!strokes && contains(techniques, "core:tremolo-" + std::to_string(n)))
                strokes = n;
        const bool tremolo = measured || contains(techniques, std::string("core:tremolo"));
        const auto pairFound = tremoloPairs_.find(chord.id);
        const TremoloPair* pair = pairFound == tremoloPairs_.end() ? nullptr : &pairFound->second;
        // Trill: the upper note sounds with the top note of the chord, as the library wants.
        std::optional<int> trillTo;
        if (contains(techniques, std::string("core:trill-half")))
            trillTo = 1;
        else if (contains(techniques, std::string("core:trill-whole")))
            trillTo = 2;
        int top = std::numeric_limits<int>::min();
        for (const ChordNote& note : chord.notes)
            top = std::max(top, pitchValue(note));

        for (std::size_t noteIndex = 0; noteIndex < chord.notes.size(); ++noteIndex)
        {
            const ChordNote& note = chord.notes[noteIndex];
            if (continuesTie(note, placed, playedAt))
                continue;
            const TieChain chain = tieChain(note, placed, playedAt);
            const std::optional<Tie>& tie = noteTie(note);
            const bool letRing = tie && std::holds_alternative<LetRing>(*tie);
            double start = mainStart;
            double length = chain.ticks - (mainStart - beat);
            double notated = chain.ticks;
            // A roll tied into notes without one stops where the tremolo stops; the rest of the
            // tie still counts as written, so that a rest is only found after it.
            if (chain.rollTicks > 0 && chain.rollTicks < chain.ticks)
                length = chain.rollTicks - (mainStart - beat);

            // Two-note tremolo: both notes start with the first and end with the second, which
            // is how a sample library is asked for a trill.
            if (pair)
            {
                notated = pair->end->absTick + pair->end->ticks - pair->start->absTick;
                start = beat - (placed.absTick - pair->start->absTick);
                length = notated;
            }

            // Arpeggio: the notes enter one after another, lowest first.
            if (rolled && rolled->size() > 1)
            {
                const double step = std::min(tpq_ / 8.0, length / 2 / static_cast<double>(rolled->size() - 1));
                const auto at = std::find(rolled->begin(), rolled->end(), &note) - rolled->begin();
                const double delay = static_cast<double>(at) * step;
                start += delay;
                length -= delay;
            }

            const auto emit = [&](const std::string& suffix, EventPitch pitch, double from, double ticks,
                                  bool plain = false, std::optional<double> velocity = std::nullopt) {
                std::vector<std::string> kept;
                for (const std::string& id : sounding)
                    if (!plain || (!isRollTechnique(id) && !id.starts_with("core:tremolo")))
                        kept.push_back(id);
                const std::string id = noteId(note) + passText + suffix;
                if (const auto phrase = writtenPhrases_.find(chord.id); phrase != writtenPhrases_.end())
                    phrases_.insert_or_assign(id, phrase->second);
                NoteEvent event = base;
                event.velocity = velocity.value_or(base.velocity);
                event.id = id;
                event.sourceIds = chain.sourceIds;
                event.pitch = std::move(pitch);
                event.startTick = from;
                event.notatedTicks = suffix.empty() ? notated : ticks;
                event.durationTicks = ticks;
                event.techniques = std::move(kept);
                if (letRing)
                    event.techniques.push_back("core:let-ring");
                event.slur = slur;
                NoteContext context;
                context.articulations = articulations;
                context.beat = beat;
                // A note that opens a phrase is not joined to the note before it.
                if (const auto previous = previousTicks_.find(chord.id);
                    previous != previousTicks_.end() && slur != SlurPosition::First)
                    context.previousTicks = previous->second;
                // A dynamic belongs to the first of the notes a written note becomes.
                if (suffix.empty())
                    context.dynamics = dynamicsOn;
                context.hairpins = hairpinsOn;
                finish(std::move(event), context);
            };

            // Tremolo written out, for a profile that asks for it (mallets cannot roll by
            // themselves): measured by its strokes, unmeasured at a fixed speed.
            const std::optional<TremoloExpansion>& expand = ctx_.profile.expandTremolo;
            const bool unmeasuredSpeed = expand && expand->unmeasuredMs && *expand->unmeasuredMs != 0;
            if (tremolo && expand && (strokes || unmeasuredSpeed) && !pair)
            {
                double unit = 0;
                if (strokes)
                {
                    // A stroke counts like a flag: one is eighths on a quarter, half or whole
                    // note, sixteenths on an eighth.
                    const double plain = durationTicks({ chord.duration.type, 0 }, tpq_);
                    const double scale = placed.ticks / durationTicks(chord.duration, tpq_); // inside a tuplet
                    unit = std::min(plain, static_cast<double>(tpq_)) * scale / (1 << *strokes);
                }
                else
                {
                    const double ms = ctx_.tempo.tickToMs(start + length) - ctx_.tempo.tickToMs(start);
                    unit = length / std::max(1.0, jsRound(ms / *expand->unmeasuredMs));
                }
                const double count = jsRound(length / unit);
                if (count > 1)
                {
                    // Plain strokes: the patch is not asked to roll them. Each takes its velocity
                    // from the dynamics where it starts, so the strokes follow a hairpin.
                    for (int i = 0; i < count; ++i)
                    {
                        const double from = start + i * unit;
                        const double velocity = i == 0 ? base.velocity : velocityAt(from, scope);
                        emit(i == 0 ? std::string() : "~" + std::to_string(i), pitchOf(note), from, unit, true,
                             velocity);
                    }
                    continue;
                }
            }

            // Glissando: the second half of the note runs in semitones to the next note.
            const EventPitch own = pitchOf(note);
            const int* ownNumber = std::get_if<int>(&own);
            if (glissTo && ownNumber)
            {
                const int target = (*glissTo)[std::min(noteIndex, glissTo->size() - 1)];
                if (std::abs(target - *ownNumber) > 1)
                {
                    const int direction = target > *ownNumber ? 1 : -1;
                    const int between = std::abs(target - *ownNumber) - 1;
                    // Never faster than a 64th note: skip pitches when the gap is too wide.
                    const double steps =
                        std::max(1.0, std::min(static_cast<double>(between), std::floor(length / 2 / (tpq_ / 16.0))));
                    const double run = length / 2 / steps;
                    emit("", own, start, length / 2);
                    for (int i = 0; i < steps; ++i)
                    {
                        const int pitch =
                            *ownNumber + direction * static_cast<int>(jsRound((i + 1.0) * between / steps));
                        emit("~g" + std::to_string(i + 1), pitch, start + length / 2 + i * run, run);
                    }
                    continue;
                }
            }

            emit("", own, start, length);
            if (trillTo && ownNumber && pitchValue(note) == top)
                emit("~tr", *ownNumber + *trillTo, start, length);
        }
    }

    // Grace notes before the beat take their time from the note before them: in the same
    // voice, whatever is still sounding when they begin ends there.
    void shortenBeforeEarlyGraces()
    {
        for (const EarlyGrace& grace : earlyGraces_)
            for (NoteEvent& note : notes_)
            {
                if (note.staffId != grace.staffId || note.voiceIndex != grace.voiceIndex)
                    continue;
                const double end = note.startTick + note.durationTicks;
                if (note.startTick < grace.startTick && end > grace.startTick && end <= grace.beat)
                    note.durationTicks = grace.startTick - note.startTick;
            }
    }

    const TrackContext& ctx_;
    const std::vector<CurveEvent>& curves_;
    mutable CurveLookup dynamics_; // evaluateCurve on curves_, without scanning them for every note
    const int tpq_;
    const double initialDynamics_;
    const std::vector<Placed>* chords_ = nullptr;
    std::vector<const NoteEventOverride*> overrides_;

    std::unordered_map<NodeId, const Placed*> tieSources_;
    std::vector<Marking> markings_;
    std::vector<CustomSpan> customSpans_;
    std::vector<DynamicMark> dynamicMarks_;
    std::vector<HairpinSpan> hairpinSpans_;
    std::unordered_map<NodeId, TremoloPair> tremoloPairs_;
    std::unordered_map<NodeId, SlurPosition> slurPositions_;  // chord ID → place in a slur, as played
    std::unordered_map<NodeId, SlurPosition> writtenPhrases_; // the same before repeated pitches split slurs
    std::vector<std::vector<const Placed*>> byVoice_;         // in the order voices first appear
    std::unordered_map<NodeId, double> previousTicks_;
    std::unordered_map<NodeId, std::vector<int>> glissandi_;

    std::vector<NoteEvent> notes_;
    std::unordered_map<std::string, SlurPosition> phrases_;
    std::vector<EarlyGrace> earlyGraces_; // where grace notes begin ahead of a beat, per voice
};

} // namespace

BuiltNotes buildNotes(const TrackContext& ctx, const std::vector<CurveEvent>& curves)
{
    return NoteBuilder(ctx, curves).build();
}

} // namespace msd
