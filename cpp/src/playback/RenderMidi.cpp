#include "playback/RenderMidi.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <set>

#include "playback/Curves.h"
#include "playback/Interpretation.h"
#include "playback/JsMath.h"

namespace msd
{
namespace
{

constexpr double keyswitchMs = 10;  // how long a keyswitch key is held
constexpr double curveSampleMs = 1; // ramps are checked this often to find where the CC value reaches its next step
constexpr int ccStep = 1;           // a ramp sends a new CC value once it has moved this many steps from the last one
constexpr double ccMinGapMs = 10;   // minimum time between two CC messages of one ramp; segment edges are always sent
constexpr double infinity = std::numeric_limits<double>::infinity();

using KitKeys = std::variant<int, std::vector<int>>; // what InstrumentProfile::kit maps a sound to

template <class T>
bool contains(const std::vector<T>& values, const T& value)
{
    return std::find(values.begin(), values.end(), value) != values.end();
}

double byDuration(const ByDuration& range, double ms)
{
    if (range.longMs <= range.shortMs)
        return ms <= range.shortMs ? range.atShort : range.atLong;
    const double t = std::min(1.0, std::max(0.0, (ms - range.shortMs) / (range.longMs - range.shortMs)));
    return range.atShort + (range.atLong - range.atShort) * t;
}

int midi(double value, int min = 0)
{
    return static_cast<int>(std::min(127.0, std::max(static_cast<double>(min), jsRound(value))));
}

double mapRange(const OutputRange& range, double value)
{
    const double t = range.curve ? shape(range.curve->curveType, range.curve->steepness, clamp01(value)) : value;
    return range.low + (range.high - range.low) * t;
}

// A controller number → value list that keeps the order numbers were first set in.
// TS: Map<number, number>
using Controllers = std::vector<std::pair<int, double>>;

void setController(Controllers& controllers, int number, double value)
{
    for (auto& [n, v] : controllers)
        if (n == number)
        {
            v = value;
            return;
        }
    controllers.emplace_back(number, value);
}

// One note on one channel, as it will be played.
struct Sounding
{
    int channel = 1;
    int pitch = 0;
    int velocity = 0;
    double startMs = 0;
    double endMs = 0;
    std::optional<Keyswitch> keyswitch;
    Controllers cc;
};

// A stretch where a note's own controller value replaces the curve's.
struct Hold
{
    int channel;
    int number;
    double startMs;
    double endMs;
};

class TrackRenderer
{
public:
    TrackRenderer(const std::vector<MusicEvent>& events, const InstrumentProfile& profile, const TempoMap& tempo,
                  const std::vector<TechniqueDefinition>& techniques)
        : profile_(profile)
        , tempo_(tempo)
        , techniques_(techniques)
        , lead_(profile.keyswitchLeadMs.value_or(0))
    {
        for (const MusicEvent& event : events)
        {
            if (const auto* note = std::get_if<NoteEvent>(&event))
                notes_.push_back(note);
            else
                curves_.push_back(std::get<CurveEvent>(event));
        }
        allChannels_.push_back(profile.defaultChannel);
        for (const TechniqueMapping& mapping : profile.techniqueMappings)
        {
            for (const int channel : mapping.channels.value_or(std::vector<int> {}))
                if (!contains(allChannels_, channel))
                    allChannels_.push_back(channel);
            for (const NoteMatch& match : mapping.matches)
                if (const auto* technique = std::get_if<TechniqueMatch>(&match))
                    known_.insert(technique->techniqueId);
        }
    }

    TrackRender render()
    {
        playNotes();
        sendNotes();
        extendHolds();
        if (profile_.dampKeyswitch)
            damp(*profile_.dampKeyswitch);
        for (const CurveMapping& mapping : profile_.curveMappings)
            sendCurve(mapping);
        std::stable_sort(messages_.begin(), messages_.end(), [](const MidiMessage& a, const MidiMessage& b) {
            return a.timeMs != b.timeMs ? a.timeMs < b.timeMs : messageOrder(a) < messageOrder(b);
        });
        return { std::move(messages_), std::move(warnings_) };
    }

private:
    // A technique this profile has no mapping for is replaced by its registry fallback.
    std::string resolveTechnique(const std::string& id) const
    {
        std::string current = id;
        for (int guard = 0; guard < 16 && !known_.contains(current); ++guard)
        {
            const auto technique = std::find_if(techniques_.begin(), techniques_.end(),
                                                [&](const TechniqueDefinition& t) { return t.id == current; });
            if (technique == techniques_.end() || !technique->fallback)
                return id;
            current = *technique->fallback;
        }
        return current;
    }

    double curveDefault(const CurveParameter& parameter) const
    {
        for (const CurveMapping& mapping : profile_.curveMappings)
            if (mapping.parameter == parameter)
                return mapping.defaultValue;
        return 0;
    }

    bool test(const NoteMatch& match, const NoteEvent& note, const std::vector<std::string>& noteTechniques) const
    {
        if (const auto* technique = std::get_if<TechniqueMatch>(&match))
            return contains(noteTechniques, technique->techniqueId);
        if (const auto* slur = std::get_if<SlurMatch>(&match))
            return note.slur == slur->position;
        const auto& range = std::get<CurveRangeMatch>(match);
        const double value = evaluateCurve(curves_, range.parameter, note.startTick, curveDefault(range.parameter),
                                           CurveScope { note.staffId, note.voiceIndex });
        return range.min <= value && value <= range.max;
    }

    static bool hasKeys(const TechniqueMapping& mapping) { return mapping.notes && !mapping.notes->empty(); }

    void pressKeyswitch(double timeMs, int channel, int key, int velocity)
    {
        messages_.push_back({ timeMs, MidiMessageType::NoteOn, channel, key, velocity, MidiSource::Keyswitch });
        messages_.push_back({ timeMs + keyswitchMs, MidiMessageType::NoteOff, channel, key, 0, MidiSource::Keyswitch });
    }

    // Each note → a Sounding on every channel it plays on, its mappings merged.
    void playNotes()
    {
        for (const NoteEvent* notePointer : notes_)
        {
            const NoteEvent& note = *notePointer;
            std::optional<KitKeys> pitch;
            const auto* kitSound = std::get_if<KitSound>(&note.pitch);
            if (kitSound == nullptr)
                pitch = std::get<int>(note.pitch) + profile_.pitchOffset.value_or(0);
            else if (profile_.kit)
                if (const auto found = profile_.kit->find(kitSound->sound); found != profile_.kit->end())
                    pitch = found->second;
            std::vector<std::string> noteTechniques;
            for (const std::string& id : note.techniques)
                noteTechniques.push_back(resolveTechnique(id));
            // A mapping with no matches applies to every note: that is the profile's base.
            std::vector<const TechniqueMapping*> active;
            for (const TechniqueMapping& mapping : profile_.techniqueMappings)
                if (std::all_of(mapping.matches.begin(), mapping.matches.end(),
                                [&](const NoteMatch& match) { return test(match, note, noteTechniques); }))
                    active.push_back(&mapping);
            // A mapping may give the key itself; otherwise an unpitched note needs a kit entry.
            if (!pitch && std::none_of(active.begin(), active.end(), [](const auto* m) { return hasKeys(*m); }))
            {
                warnings_.push_back("note " + note.id + ": the profile has no kit entry for this sound");
                continue;
            }

            // First pass: which channels the note sounds on.
            std::vector<int> channels;
            for (const TechniqueMapping* mapping : active)
            {
                if (!mapping->channels || mapping->channels->empty())
                    continue;
                if (mapping->channelMode == ChannelMode::Add)
                {
                    for (const int channel : *mapping->channels)
                        if (!contains(channels, channel))
                            channels.push_back(channel);
                }
                else
                    channels = *mapping->channels;
            }
            if (channels.empty())
                channels = { profile_.defaultChannel };

            // Second pass: merge the mappings separately for each channel.
            const double baseStart = tempo_.tickToMs(note.startTick);
            const double notatedMs = tempo_.tickToMs(note.startTick + note.notatedTicks) - baseStart;
            const double baseEnd = tempo_.tickToMs(note.startTick + note.durationTicks);
            for (const int channel : channels)
            {
                double velocity = note.velocity;
                double duration = baseEnd - baseStart;
                double offset = note.startOffsetMs.value_or(0);
                std::optional<Keyswitch> keyswitch;
                const TechniqueMapping* keys = nullptr;
                bool alignEnd = false;
                Controllers cc;
                for (const TechniqueMapping* mapping : active)
                {
                    if (mapping->channels && !contains(*mapping->channels, channel))
                        continue;
                    if (mapping->keyswitch)
                        keyswitch = mapping->keyswitch;
                    if (hasKeys(*mapping))
                        keys = mapping;
                    if (mapping->alignToEnd)
                        alignEnd = *mapping->alignToEnd;
                    for (const TechniqueCc& controller : mapping->cc.value_or(std::vector<TechniqueCc> {}))
                    {
                        const auto* fixed = std::get_if<int>(&controller.value);
                        setController(cc, controller.number,
                                      fixed ? *fixed : byDuration(std::get<ByDuration>(controller.value), notatedMs));
                    }
                    if (mapping->velocity)
                        velocity = applyMode(mapping->velocity->mode, velocity, mapping->velocity->value);
                    if (mapping->durationMs)
                        duration = applyMode(mapping->durationMs->mode, duration, mapping->durationMs->value);
                    offset += mapping->startOffsetMs.value_or(0);
                }
                // Keys played instead of the pitch take turns, counted per mapping and channel.
                int played = pitch && std::holds_alternative<int>(*pitch) ? std::get<int>(*pitch) : 0;
                if (!keys && pitch && kitSound)
                    if (const auto* turnsOver = std::get_if<std::vector<int>>(&*pitch);
                        turnsOver && !turnsOver->empty())
                    {
                        int& turn = kitTurns_[{ kitSound->sound, channel }];
                        played = (*turnsOver)[static_cast<std::size_t>(turn) % turnsOver->size()];
                        ++turn;
                    }
                if (keys)
                {
                    int& turn = turns_[{ keys, channel }];
                    played = (*keys->notes)[static_cast<std::size_t>(turn) % keys->notes->size()];
                    ++turn;
                }
                double startMs = std::max(0.0, baseStart + offset);
                double endMs = std::max(startMs + 1, baseStart + duration);
                if (alignEnd)
                {
                    // Keeps the note's end, e.g. a crescendo sample that peaks there.
                    endMs = baseEnd;
                    startMs = std::max(0.0, endMs - duration);
                }
                const auto* fixedVelocity = std::get_if<FixedVelocity>(&profile_.velocity);
                const int midiVelocity = fixedVelocity
                                             ? midi(fixedVelocity->fixed, 1)
                                             : midi(mapRange(std::get<MappedVelocity>(profile_.velocity).mapping,
                                                             std::min(1.0, std::max(0.0, velocity))),
                                                    1);
                soundings_.push_back({ channel, played, midiVelocity, startMs, endMs, keyswitch, std::move(cc) });
            }
        }

        std::stable_sort(soundings_.begin(), soundings_.end(),
                         [](const Sounding& a, const Sounding& b) { return a.startMs < b.startMs; });

        // A key cannot be pressed twice: end a note where the same key is struck again.
        std::map<std::pair<int, int>, Sounding*> lastOnKey;
        for (Sounding& sounding : soundings_)
        {
            Sounding*& previous = lastOnKey[{ sounding.channel, sounding.pitch }];
            if (previous && previous->endMs > sounding.startMs)
                previous->endMs = sounding.startMs;
            previous = &sounding;
        }
    }

    // Keyswitches and technique CCs are sent only when they change on a channel.
    void sendNotes()
    {
        std::map<int, std::pair<int, int>> keyswitchState; // channel → note, velocity
        std::map<std::pair<int, int>, double> ccState;     // channel, number → value
        std::set<int> curveControllers;
        for (const CurveMapping& mapping : profile_.curveMappings)
            for (const CurveCc& cc : mapping.cc.value_or(std::vector<CurveCc> {}))
                curveControllers.insert(cc.number);

        for (const Sounding& sounding : soundings_)
        {
            const int channel = sounding.channel;
            const double before = std::max(0.0, sounding.startMs - lead_);
            // Velocity is part of the switch: some keys select an articulation by how hard
            // they are pressed.
            if (sounding.keyswitch)
            {
                const std::pair<int, int> pressed { sounding.keyswitch->note, sounding.keyswitch->velocity };
                const auto state = keyswitchState.find(channel);
                if (state == keyswitchState.end() || state->second != pressed)
                {
                    keyswitchState.insert_or_assign(channel, pressed);
                    pressKeyswitch(before, channel, pressed.first, pressed.second);
                }
            }
            for (const auto& [number, value] : sounding.cc)
            {
                const MidiMessage message { before, MidiMessageType::Cc, channel,
                                            number, midi(value),         MidiSource::TechniqueCc };
                if (curveControllers.contains(number))
                {
                    // A controller a curve also drives: take it over for as long as the note sounds.
                    holds_.push_back({ channel, number, before, sounding.endMs });
                    messages_.push_back(message);
                    continue;
                }
                const auto state = ccState.find({ channel, number });
                if (state != ccState.end() && state->second == value)
                    continue;
                ccState.insert_or_assign({ channel, number }, value);
                messages_.push_back(message);
            }
            messages_.push_back({ sounding.startMs, MidiMessageType::NoteOn, channel, sounding.pitch, sounding.velocity,
                                  MidiSource::Note });
            messages_.push_back(
                { sounding.endMs, MidiMessageType::NoteOff, channel, sounding.pitch, 0, MidiSource::Note });
        }
    }

    // A hold lasts until the next note on its channel, so that the curve does not come
    // back in the silence between two short notes. That note sends its own value or
    // resumes the curve, ahead of its keyswitch lead.
    void extendHolds()
    {
        std::map<int, std::vector<double>> startsOn; // ascending, as the soundings are
        for (const Sounding& sounding : soundings_)
            startsOn[sounding.channel].push_back(std::max(0.0, sounding.startMs - lead_));
        for (Hold& hold : holds_)
        {
            const std::vector<double>& starts = startsOn[hold.channel];
            const auto next = std::upper_bound(starts.begin(), starts.end(), hold.startMs);
            if (next != starts.end())
                hold.endMs = std::max(hold.endMs, *next);
        }
    }

    // A damp key, held from where the sound should stop until the next note comes or a
    // beat has passed, whichever is first; letting go switches the patch back. It stops
    // the sound at rests (not after a note left to ring) and right after notes that ask
    // for it, such as staccato ones.
    void damp(const DampKeyswitch& setting)
    {
        const bool atRests = setting.atRests.value_or(true);
        const std::vector<std::string> after = setting.afterTechniques.value_or(std::vector<std::string> {});
        const int channel = profile_.defaultChannel;
        struct Span
        {
            double startMs;
            double endMs; // written end
            double playedEndMs;
            bool rings;
            bool cut;
        };
        std::vector<Span> spans;
        for (const NoteEvent* note : notes_)
            spans.push_back({
                tempo_.tickToMs(note->startTick),
                tempo_.tickToMs(note->startTick + note->notatedTicks),
                tempo_.tickToMs(note->startTick + note->durationTicks),
                contains(note->techniques, std::string("core:let-ring")),
                std::any_of(note->techniques.begin(), note->techniques.end(),
                            [&](const std::string& id) { return contains(after, id); }),
            });
        std::stable_sort(spans.begin(), spans.end(),
                         [](const Span& a, const Span& b) { return a.startMs < b.startMs; });
        const auto startsBefore = [](double ms, const Span& span) { return ms < span.startMs; };
        const auto startsAfter = [](const Span& span, double ms) { return span.startMs < ms; };

        // A beat (quarter note) at the tempo in force at a time.
        const auto beatMs = [&](double ms) {
            const double tick = tempo_.msToTick(ms);
            const std::vector<TempoPoint>& points = tempo_.points();
            const auto next = std::upper_bound(points.begin(), points.end(), tick,
                                               [](double t, const TempoPoint& point) { return t < point.tick; });
            return 60000 / (next == points.begin() ? 120 : std::prev(next)->qpm);
        };
        struct Window
        {
            double from;
            double to;
        };
        std::vector<Window> windows;
        const auto dampFrom = [&](double from) {
            const auto next = std::lower_bound(spans.begin(), spans.end(), from, startsAfter);
            const double to = std::min(from + beatMs(from), next == spans.end() ? infinity : next->startMs - lead_);
            if (to > from)
                windows.push_back({ from, to });
        };
        double end = -infinity;
        bool rings = false;
        for (std::size_t i = 0; i < spans.size(); ++i)
        {
            const Span& span = spans[i];
            if (span.cut)
            {
                // Unless another note comes in before this one's sound ends.
                const auto later = std::upper_bound(spans.begin(), spans.end(), span.startMs, startsBefore);
                if (later == spans.end() || !(later->startMs < span.playedEndMs + 1))
                    dampFrom(span.playedEndMs);
            }
            if (span.endMs >= end)
            {
                end = span.endMs;
                rings = span.rings;
            }
            if (atRests && !rings && (i + 1 == spans.size() || spans[i + 1].startMs > end + 1))
                dampFrom(end);
        }
        // One press for each stretch of silence, however many reasons it has.
        std::stable_sort(windows.begin(), windows.end(),
                         [](const Window& a, const Window& b) { return a.from < b.from; });
        std::vector<Window> merged;
        for (const Window& window : windows)
        {
            if (!merged.empty() && window.from <= merged.back().to)
                merged.back().to = std::max(merged.back().to, window.to);
            else
                merged.push_back(window);
        }
        for (const Window& window : merged)
        {
            messages_.push_back({ window.from, MidiMessageType::NoteOn, channel, setting.note, setting.velocity,
                                  MidiSource::Keyswitch });
            messages_.push_back(
                { window.to, MidiMessageType::NoteOff, channel, setting.note, 0, MidiSource::Keyswitch });
        }
    }

    // A curve → controllers and range keyswitches.
    void sendCurve(const CurveMapping& mapping)
    {
        std::vector<CurveEvent> own;
        for (const CurveEvent& curve : curves_)
            if (curve.parameter == mapping.parameter)
                own.push_back(curve);
        // Tick → whether it must be sent (a segment edge) rather than being a ramp sample.
        // TS keeps these in a Map: ramp samples never replace a tick already there, edges
        // always mark theirs. So a tick is an edge if anything made it one; of equal ticks
        // the first one stays. Collected in a vector and merged, for speed (a ramp gives a
        // sample every millisecond).
        std::vector<std::pair<double, bool>> ticks { { 0.0, true } };
        TempoMap::Cursor cursor(tempo_);
        for (const CurveEvent& curve : own)
        {
            const double end = curve.startTick + curve.durationTicks;
            if (curve.durationTicks > 0 && curve.curveType != CurveType::Hold)
            {
                const double endMs = cursor.tickToMs(end);
                for (double ms = cursor.tickToMs(curve.startTick) + curveSampleMs; ms < endMs; ms += curveSampleMs)
                    ticks.emplace_back(cursor.msToTick(ms), false);
            }
            ticks.emplace_back(curve.startTick, true);
            ticks.emplace_back(end, true);
        }
        std::stable_sort(ticks.begin(), ticks.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        std::size_t kept = 0;
        for (std::size_t i = 0; i < ticks.size(); ++i)
        {
            if (kept > 0 && !(ticks[kept - 1].first < ticks[i].first))
                ticks[kept - 1].second = ticks[kept - 1].second || ticks[i].second;
            else
                ticks[kept++] = ticks[i];
        }
        ticks.resize(kept);
        const std::vector<int>& channels = mapping.channels ? *mapping.channels : allChannels_;

        struct Point
        {
            double timeMs;
            double tick;
            bool resume; // where a hold ends, to pick the curve up again
            bool edge;
        };
        // Already in time order: tickToMs never goes back.
        std::vector<Point> curvePoints;
        curvePoints.reserve(ticks.size());
        for (const auto& [tick, edge] : ticks)
            curvePoints.push_back({ std::max(0.0, cursor.tickToMs(tick)), tick, false, edge });
        for (const int channel : channels)
        {
            for (const CurveCc& cc : mapping.cc.value_or(std::vector<CurveCc> {}))
            {
                std::vector<Hold> taken;
                for (const Hold& hold : holds_)
                    if (hold.channel == channel && hold.number == cc.number)
                        taken.push_back(hold);
                std::stable_sort(taken.begin(), taken.end(),
                                 [](const Hold& a, const Hold& b) { return a.startMs < b.startMs; });
                std::vector<Point> holdPoints;
                for (const Hold& hold : taken)
                    holdPoints.push_back({ hold.endMs, tempo_.msToTick(hold.endMs), true, true });
                std::stable_sort(holdPoints.begin(), holdPoints.end(),
                                 [](const Point& a, const Point& b) { return a.timeMs < b.timeMs; });
                // As a stable sort of the curve's points followed by the holds' would: on
                // equal times, the curve's first.
                std::vector<Point> points;
                points.reserve(curvePoints.size() + holdPoints.size());
                std::merge(curvePoints.begin(), curvePoints.end(), holdPoints.begin(), holdPoints.end(),
                           std::back_inserter(points),
                           [](const Point& a, const Point& b) { return a.timeMs < b.timeMs; });

                std::optional<int> last;
                double lastMs = -infinity;
                std::size_t h = 0;
                // A controller is per channel, so segments limited to some voices are not kept apart.
                CurveSampler valueAt(own, mapping.parameter, mapping.defaultValue);
                for (const Point& point : points)
                {
                    while (h < taken.size() && taken[h].endMs <= point.timeMs)
                        ++h;
                    if (h < taken.size() && taken[h].startMs <= point.timeMs)
                        continue; // held
                    const int data2 = midi(mapRange(cc.mapping, valueAt(point.tick)));
                    if (!point.resume && last == data2)
                        continue;
                    const bool small = last && std::abs(data2 - *last) < ccStep;
                    const bool early = point.timeMs - lastMs < ccMinGapMs;
                    if (!point.edge && (small || early))
                        continue;
                    last = data2;
                    lastMs = point.timeMs;
                    messages_.push_back(
                        { point.timeMs, MidiMessageType::Cc, channel, cc.number, data2, MidiSource::Curve });
                }
            }
            if (!mapping.keyswitch)
                continue;
            std::optional<int> pressed;
            CurveSampler valueAt(own, mapping.parameter, mapping.defaultValue);
            for (const auto& [tick, edge] : ticks)
            {
                const double value = valueAt(tick);
                const KeyswitchRange* range = nullptr;
                for (const KeyswitchRange& r : *mapping.keyswitch)
                    if (r.min <= value && value <= r.max)
                        range = &r; // later ones win
                if (range == nullptr || pressed == range->note)
                    continue;
                pressed = range->note;
                pressKeyswitch(std::max(0.0, tempo_.tickToMs(tick)), channel, range->note, range->velocity);
            }
        }
    }

    const InstrumentProfile& profile_;
    const TempoMap& tempo_;
    const std::vector<TechniqueDefinition>& techniques_;
    const double lead_; // how early keyswitches and technique CCs go out

    std::vector<const NoteEvent*> notes_;
    std::vector<CurveEvent> curves_;
    std::vector<int> allChannels_;
    std::set<std::string> known_; // techniques some mapping matches
    std::vector<Sounding> soundings_;
    std::vector<Hold> holds_;
    // Turns taken so far by each mapping with several keys, per channel; and by each kit
    // sound with several keys.
    std::map<std::pair<const TechniqueMapping*, int>, int> turns_;
    std::map<std::pair<std::string, int>, int> kitTurns_;
    std::vector<MidiMessage> messages_;
    std::vector<std::string> warnings_;
};

const std::string* soundOf(const MusicEvent& event)
{
    if (const auto* note = std::get_if<NoteEvent>(&event))
        if (const auto* kit = std::get_if<KitSound>(&note->pitch))
            return &kit->sound;
    return nullptr;
}

} // namespace

int messageOrder(const MidiMessage& message)
{
    switch (message.type)
    {
    case MidiMessageType::NoteOff: return 0;
    case MidiMessageType::Cc: return 1;
    case MidiMessageType::NoteOn: return message.source == MidiSource::Keyswitch ? 2 : 3;
    }
    return 3;
}

TrackRender renderTrack(const std::vector<MusicEvent>& events, const InstrumentProfile& profile, const TempoMap& tempo,
                        const std::vector<TechniqueDefinition>& techniques)
{
    return TrackRenderer(events, profile, tempo, techniques).render();
}

std::vector<RenderedTrack> renderTracks(const MusicData& music, const PlaybackSetup& setup,
                                        const std::vector<MusicEvent>& events, const TempoMap& tempo,
                                        std::vector<std::string>& warnings)
{
    std::vector<RenderedTrack> tracks;
    for (const Track& track : music.tracks)
    {
        const auto assignment = setup.trackProfiles.find(track.id);
        const std::vector<TrackOutput> noOutputs;
        const std::vector<TrackOutput>& outputs =
            assignment == setup.trackProfiles.end() ? noOutputs : assignment->second.outputs;
        if (outputs.empty())
            warnings.push_back("track " + track.id + ": no outputs, skipped");
        std::vector<const MusicEvent*> own;
        for (const MusicEvent& event : events)
            if (std::visit([](const auto& e) -> const NodeId& { return e.trackId; }, event) == track.id)
                own.push_back(&event);

        // Sounds an output names are its own; the output without a map plays the rest.
        std::set<std::string> taken;
        for (const TrackOutput& output : outputs)
            if (output.sounds)
                for (const auto& [sound, kitSound] : *output.sounds)
                    taken.insert(sound);
        if (!outputs.empty()
            && std::all_of(outputs.begin(), outputs.end(), [](const TrackOutput& o) { return o.sounds.has_value(); }))
        {
            std::vector<std::string> unplayed;
            for (const MusicEvent* event : own)
                if (const std::string* sound = soundOf(*event);
                    sound && !taken.contains(*sound) && !contains(unplayed, *sound))
                    unplayed.push_back(*sound);
            for (const std::string& sound : unplayed)
                warnings.push_back("track " + track.id + ": no output plays the sound \"" + sound + "\"");
        }

        for (const TrackOutput& output : outputs)
        {
            const auto profile =
                std::find_if(setup.instrumentProfiles.begin(), setup.instrumentProfiles.end(),
                             [&](const InstrumentProfile& p) { return p.id == output.instrumentProfileId; });
            if (profile == setup.instrumentProfiles.end())
            {
                warnings.push_back("track " + track.id + ", output " + output.id + ": no instrument profile, skipped");
                continue;
            }
            std::vector<MusicEvent> mine;
            bool hasNotes = false;
            for (const MusicEvent* event : own)
            {
                if (!std::holds_alternative<NoteEvent>(*event))
                {
                    mine.push_back(*event); // the curves go with every output
                    continue;
                }
                const std::string* sound = soundOf(*event);
                if (output.sounds)
                {
                    // The score's sound, under the name the patch knows it by.
                    const auto kitSound = sound ? output.sounds->find(*sound) : output.sounds->end();
                    if (kitSound == output.sounds->end())
                        continue;
                    NoteEvent renamed = std::get<NoteEvent>(*event);
                    renamed.pitch = KitSound { kitSound->second };
                    mine.emplace_back(std::move(renamed));
                }
                else if (sound == nullptr || !taken.contains(*sound))
                    mine.push_back(*event);
                else
                    continue;
                hasNotes = true;
            }
            // Of several outputs, one with nothing to play makes no empty track.
            if (outputs.size() > 1 && !hasNotes)
                continue;
            TrackRender rendered = renderTrack(mine, *profile, tempo, setup.techniques);
            warnings.insert(warnings.end(), rendered.warnings.begin(), rendered.warnings.end());
            tracks.push_back(
                { track.id, output.id, output.name.value_or(track.instrumentName), std::move(rendered.messages) });
        }
    }
    return tracks;
}

} // namespace msd
