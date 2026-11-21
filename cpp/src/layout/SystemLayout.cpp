#include "layout/SystemLayout.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string_view>

#include "layout/Accidentals.h"
#include "layout/StaffPosition.h"
#include "layout/Transpose.h"

// How this works
// ──────────────
// 1. Plan: walk the measures before the system to know the clef, key and time
//    signature in force, then for each measure in the system and each staff find the
//    events, and the ticks where something starts (onsets) on any staff. Each chord and
//    rest gets its shape — noteheads, accidentals, stem, flag, dots — drawn around
//    x = 0, so its extent to the left and right is known. Each onset gets the space its
//    duration asks for, but at least enough that no staff's shapes touch the next
//    onset's.
// 2. Justify: the fixed parts (header, padding) keep their width; the space between
//    onsets is stretched by one factor, or squeezed towards each onset's minimum, so
//    the system fills `width`. It never squeezes below the minimum.
// 3. Emit: turn every staff line, signature, event shape and barline into LayoutBoxes
//    at their final position. Beams come last: their slope and the stems that reach
//    them depend on how far apart the notes ended up.
//
// Every staff is planned in its own coordinates (y from its top line); emitting moves
// it down to its place in the system.

namespace msd
{
namespace
{

// Lets std::visit take one lambda per alternative.
template <class... Ts>
struct Overloaded : Ts...
{
    using Ts::operator()...;
};

constexpr double stemLengthSp = 3.5; // one octave, the standard unbeamed stem

// ── Time ───────────────────────────────────────────────────────────────────────

Tick nominalTicks(const Duration& duration, int tpq)
{
    const int type = static_cast<int>(duration.type);
    const Tick base = type >= 0 ? Tick(tpq) << type : Tick(tpq) >> -type;
    Tick total = base;
    Tick dot = base;
    for (int i = 0; i < duration.dots; ++i)
    {
        dot /= 2;
        total += dot;
    }
    return total;
}

Tick timeSignatureTicks(const TimeSignature& timeSig, int tpq)
{
    return timeSig.unit > 0 ? Tick(timeSig.beats) * 4 * tpq / timeSig.unit : Tick(4) * tpq;
}

// The beat that beams group by when the data doesn't say: a dotted quarter in 6/8,
// 9/8 and 12/8, a half in x/2, a quarter otherwise. The same rule as the prototype.
Tick beamBeatTicks(const std::optional<TimeSignature>& timeSig, int tpq)
{
    if (timeSig && timeSig->unit == 8 && timeSig->beats % 3 == 0)
        return Tick(tpq) * 3 / 2;
    if (timeSig && timeSig->unit > 0 && timeSig->unit <= 2)
        return Tick(tpq) * 2;
    return tpq;
}

int flagCount(DurationType type) { return type <= DurationType::Eighth ? -static_cast<int>(type) : 0; }

// One glyph or line of an event, before it has a place in the system.
struct ShapeItem
{
    const NodeId* nodeId = nullptr;
    Part part = Part::Notehead;
    std::variant<GlyphShape, LineShape> shape;
};

// The stem of a beamed chord, drawn once the beam has its place.
struct BeamedStem
{
    double x = 0;      // the stem's centre, from the onset
    double startY = 0; // where it leaves the note at its start
    double nearY = 0;  // the note nearest the beam
    int beams = 0;     // 1 for an eighth, 2 for a sixteenth, …
    bool up = true;
};

// Everything one chord or rest draws, around its onset at x = 0.
struct EventShape
{
    std::vector<ShapeItem> items; // in drawing order
    double left = 0;              // the leftmost point (accidentals, ledger lines)
    double right = 0;             // the rightmost point (noteheads, flags, dots)
    std::optional<BeamedStem> beamedStem;
};

// A chord or rest with the tick it starts at, from the start of its measure.
struct PlacedEvent
{
    const Chord* chord = nullptr; // exactly one of chord and rest is set
    const Rest* rest = nullptr;
    int voice = 0;
    Tick tick = 0;
    Tick length = 0; // tuplets included
    EventShape shape {};
};

bool onLine(double y) { return std::abs(y - std::round(y)) < 1e-6; }

bool isFullMeasureRest(const PlacedEvent& event) { return event.rest && event.rest->fullMeasure.value_or(false); }

Rect lineBounds(Point from, Point to, double thickness)
{
    const double half = thickness / 2;
    const bool horizontal = std::abs(to.x - from.x) >= std::abs(to.y - from.y);
    return { std::min(from.x, to.x) - (horizontal ? 0 : half), std::min(from.y, to.y) - (horizontal ? half : 0),
             std::max(from.x, to.x) + (horizontal ? 0 : half), std::max(from.y, to.y) + (horizontal ? half : 0) };
}

// Places events one after another from `start`. Inside tuplets, lengths are scaled by
// num/den (2/3 for a triplet). Returns the events' unscaled total length.
Tick placeEvents(const std::vector<VoiceEvent>& events, Tick start, Tick num, Tick den, int tpq, int voice,
                 std::vector<PlacedEvent>& out)
{
    Tick nominal = 0;
    for (const VoiceEvent& event : events)
    {
        const Tick at = start + nominal * num / den;
        std::visit(Overloaded {
                       [&](const Chord& chord) {
                           const Tick length = nominalTicks(chord.duration, tpq);
                           out.push_back({ &chord, nullptr, voice, at, length * num / den });
                           nominal += length;
                       },
                       [&](const Rest& rest) {
                           const Tick length = nominalTicks(rest.duration, tpq);
                           out.push_back({ nullptr, &rest, voice, at, length * num / den });
                           nominal += length;
                       },
                       [&](const Space& space) { nominal += nominalTicks(space.duration, tpq); },
                       [&](const Tuplet& tuplet) {
                           const Tick normal = tuplet.normal > 0 ? tuplet.normal : 1;
                           const Tick actual = tuplet.actual > 0 ? tuplet.actual : 1;
                           const Tick inner =
                               placeEvents(tuplet.events, at, num * normal, den * actual, tpq, voice, out);
                           nominal += inner * normal / actual;
                       },
                   },
                   event);
    }
    return nominal;
}

// ── Glyph names (SMuFL) ────────────────────────────────────────────────────────

std::string noteheadGlyph(DurationType type)
{
    switch (type)
    {
    case DurationType::Longa:
    case DurationType::Double: return "noteheadDoubleWhole";
    case DurationType::Whole: return "noteheadWhole";
    case DurationType::Half: return "noteheadHalf";
    default: return "noteheadBlack";
    }
}

std::string restGlyph(DurationType type)
{
    switch (type)
    {
    case DurationType::Longa: return "restLonga";
    case DurationType::Double: return "restDoubleWhole";
    case DurationType::Whole: return "restWhole";
    case DurationType::Half: return "restHalf";
    case DurationType::Quarter: return "restQuarter";
    case DurationType::Eighth: return "rest8th";
    case DurationType::Sixteenth: return "rest16th";
    case DurationType::ThirtySecond: return "rest32nd";
    case DurationType::SixtyFourth: return "rest64th";
    case DurationType::OneHundredTwentyEighth: return "rest128th";
    }
    return "restQuarter";
}

// The first name the font has, or the last one if it has none of them.
std::string firstGlyph(const SmuflFont& font, std::initializer_list<std::string> names)
{
    for (const std::string& name : names)
        if (font.hasGlyph(name))
            return name;
    return *(names.end() - 1);
}

std::string kitNoteheadGlyph(Notehead notehead, DurationType type, const SmuflFont& font)
{
    const bool whole = type >= DurationType::Whole;
    const bool half = type == DurationType::Half;
    const std::string fallback = noteheadGlyph(type);
    switch (notehead)
    {
    case Notehead::Normal: return fallback;
    case Notehead::X:
        return firstGlyph(font, { whole  ? "noteheadXWhole"
                                  : half ? "noteheadXHalf"
                                         : "noteheadXBlack",
                                  "noteheadXBlack", fallback });
    case Notehead::CircleX: return firstGlyph(font, { "noteheadCircleX", fallback });
    case Notehead::Triangle:
        return firstGlyph(font, { whole  ? "noteheadTriangleUpWhole"
                                  : half ? "noteheadTriangleUpHalf"
                                         : "noteheadTriangleUpBlack",
                                  "noteheadTriangleUpBlack", fallback });
    case Notehead::Diamond:
        return firstGlyph(font, { whole  ? "noteheadDiamondWhole"
                                  : half ? "noteheadDiamondHalf"
                                         : "noteheadDiamondBlack",
                                  "noteheadDiamondBlack", fallback });
    }
    return fallback;
}

std::string clefGlyph(const Clef& clef, const SmuflFont& font)
{
    std::string base;
    switch (clef.type)
    {
    case ClefType::G: base = "gClef"; break;
    case ClefType::F: base = "fClef"; break;
    case ClefType::C: base = "cClef"; break;
    case ClefType::Percussion: return "unpitchedPercussionClef1";
    }
    const char* suffix = clef.octaveChange == -1   ? "8vb"
                         : clef.octaveChange == 1  ? "8va"
                         : clef.octaveChange == -2 ? "15mb"
                         : clef.octaveChange == 2  ? "15ma"
                                                   : "";
    return firstGlyph(font, { base + suffix, base });
}

std::string timeSigDigits(int value, const char* fallback) { return value > 0 ? std::to_string(value) : fallback; }

// flag8thUp, flag16thDown, … for 1 to 5 flags.
std::string flagGlyph(int flags, bool up)
{
    static constexpr const char* names[] = { "8th", "16th", "32nd", "64th", "128th" };
    return std::string("flag") + names[std::clamp(flags, 1, 5) - 1] + (up ? "Up" : "Down");
}

// ── Staves and plans ───────────────────────────────────────────────────────────

const Clef defaultClef { .id = {}, .type = ClefType::G, .line = 2, .octaveChange = 0 };

// One staff of the system, and what planning needs to remember about it.
struct StaffContext
{
    const Track* track = nullptr;
    const Staff* staff = nullptr;
    int lines = 5;
    double y = 0;            // its top line, in the system
    Clef clef = defaultClef; // in force where planning has got to

    // This staff's part of measure `index`, or null. Staff measures usually line up
    // with the score's measures; only when they don't is an index by ID built.
    const StaffMeasure* measureAt(std::size_t index, const Measure& measure) const
    {
        const std::vector<StaffMeasure>& measures = staff->measures;
        if (index < measures.size() && measures[index].measureId == measure.id)
            return &measures[index];
        if (!byId)
        {
            byId.emplace();
            for (const StaffMeasure& staffMeasure : measures)
                byId->emplace(staffMeasure.measureId, &staffMeasure);
        }
        const auto it = byId->find(measure.id);
        return it == byId->end() ? nullptr : it->second;
    }

    mutable std::optional<std::map<std::string_view, const StaffMeasure*>> byId; // measureAt's index
};

// A tick where something starts on some staff.
struct Onset
{
    Tick tick = 0;
    double spring = 0;  // natural space from here to the next onset
    double minimum = 0; // the least space that keeps its shapes clear of the next
    double left = 0;    // how far its shapes reach left of the onset (≥ 0), on any staff
    double right = 0;   // how far they reach right of it
};

// One measure of one staff.
struct StaffPlan
{
    const StaffMeasure* staffMeasure = nullptr; // null when this staff has nothing here
    Clef clefBefore;                            // in force before this measure's own clef changes
    std::optional<Clef> clef;                   // drawn at the measure's start
    int key = 0;                                // the written key signature in force
    bool drawKey = false;                       // drawn at the measure's start
    std::vector<PlacedEvent> events;            // in time order
    std::vector<std::vector<std::size_t>> beams; // each group's chords, indices into events
    bool severalVoices = false;
};

struct MeasurePlan
{
    const Measure* measure = nullptr;
    Tick length = 0;

    // Signatures drawn at the measure's start (the system header, for the first one),
    // at the same x on every staff.
    std::optional<TimeSignature> time;
    NodeId keyId; // the key signature in force, for the boxes of a drawn one
    double clefX = 0; // from the measure's left edge
    double keyX = 0;
    double timeX = 0;
    double lead = 0; // left edge → first onset: fixed, never stretched

    std::vector<StaffPlan> staves; // one per staff of the system
    std::vector<Onset> onsets;     // on any staff, in time order

    double naturalWidth() const
    {
        double width = lead;
        for (const Onset& onset : onsets)
            width += onset.spring;
        return width;
    }
};

Clef clefAt(const StaffPlan& plan, Tick tick)
{
    Clef clef = plan.clefBefore;
    if (plan.staffMeasure == nullptr)
        return clef;
    Tick latest = -1;
    for (const ClefChange& change : plan.staffMeasure->clefs)
        if (change.tick <= tick && change.tick >= latest)
        {
            clef = change.clef;
            latest = change.tick;
        }
    return clef;
}

// ── The builder ────────────────────────────────────────────────────────────────

class SystemBuilder
{
public:
    explicit SystemBuilder(const SystemInput& input)
        : in(input)
        , font(input.font)
        , engraving(input.font.engravingDefaults())
        , tpq(input.music.tpq > 0 ? input.music.tpq : 480)
    {
        const std::size_t measureTotal = in.music.measures.size();
        if (in.measureCount == 0 || in.firstMeasure >= measureTotal || in.measureCount > measureTotal - in.firstMeasure)
            throw std::invalid_argument("measures " + std::to_string(in.firstMeasure) + " + "
                                        + std::to_string(in.measureCount) + " are outside the score ("
                                        + std::to_string(measureTotal) + " measures)");
        if (in.staves.empty())
            throw std::invalid_argument("a system needs at least one staff");

        // Ties into the system's measures start in them or in the measure before.
        const std::size_t tiesFrom = in.firstMeasure > 0 ? in.firstMeasure - 1 : 0;
        for (const SystemStaff& wanted : in.staves)
        {
            StaffContext context = findStaff(wanted.staffId);
            context.y = wanted.y;
            for (std::size_t i = tiesFrom; i < in.firstMeasure + in.measureCount; ++i)
                if (const StaffMeasure* staffMeasure = context.measureAt(i, in.music.measures[i]))
                    for (const Voice& voice : staffMeasure->voices)
                        collectTieTargets(voice.events);
            staves.push_back(std::move(context));
        }
    }

    SystemLayout build()
    {
        skipTo(in.firstMeasure);
        for (std::size_t i = in.firstMeasure; i < in.firstMeasure + in.measureCount; ++i)
            plans.push_back(planMeasure(i, i == in.firstMeasure));

        double fixed = 0;
        double natural = 0;
        double minimum = 0;
        for (const MeasurePlan& plan : plans)
        {
            fixed += plan.lead;
            for (const Onset& onset : plan.onsets)
            {
                natural += onset.spring;
                minimum += onset.minimum;
            }
        }

        // Wider than natural: every space grows by the same factor. Narrower: every
        // space moves the same share of the way towards its minimum, and no further. A
        // system that doesn't fit even then comes out wider than asked (overfull until
        // the next reflow), rather than with notes on top of each other.
        if (in.width)
        {
            const double available = *in.width - fixed;
            if (available >= natural && natural > 0)
                stretch = available / natural;
            else if (natural > minimum)
                squeeze = std::clamp((available - minimum) / (natural - minimum), 0.0, 1.0);
            else
                squeeze = 0;
        }

        out.width = fixed;
        for (const MeasurePlan& plan : plans)
            for (const Onset& onset : plan.onsets)
                out.width += space(onset);
        for (const StaffContext& staff : staves)
            out.staves.push_back({ staff.staff->id, staff.y, staff.lines });

        emitStaffLines();
        emitSystemStart();
        double x = 0;
        for (const MeasurePlan& plan : plans)
            x = emitMeasure(plan, x);
        return std::move(out);
    }

    std::vector<MeasureWidth> widths()
    {
        skipTo(in.firstMeasure);
        std::vector<MeasureWidth> result;
        for (std::size_t i = in.firstMeasure; i < in.firstMeasure + in.measureCount; ++i)
        {
            // Plan it twice from the same state: as a system's first measure and not.
            const auto savedTime = timeSig;
            const auto savedKey = key;
            std::vector<Clef> savedClefs;
            for (const StaffContext& staff : staves)
                savedClefs.push_back(staff.clef);

            MeasureWidth width;
            width.first = planMeasure(i, true).naturalWidth();
            timeSig = savedTime;
            key = savedKey;
            for (std::size_t s = 0; s < staves.size(); ++s)
                staves[s].clef = savedClefs[s];
            width.other = planMeasure(i, false).naturalWidth();
            result.push_back(width);
        }
        return result;
    }

private:
    StaffContext findStaff(const NodeId& staffId) const
    {
        for (const Track& track : in.music.tracks)
            for (const Staff& staff : track.staves)
                if (staff.id == staffId)
                    return { .track = &track, .staff = &staff, .lines = staff.lines.value_or(5) };
        throw std::invalid_argument("no staff with id \"" + staffId + "\"");
    }

    // Notes that continue a tie: they show no accidental.
    void collectTieTargets(const std::vector<VoiceEvent>& events)
    {
        for (const VoiceEvent& event : events)
        {
            if (const auto* tuplet = std::get_if<Tuplet>(&event))
                collectTieTargets(tuplet->events);
            const auto* chord = std::get_if<Chord>(&event);
            if (chord == nullptr)
                continue;
            for (const ChordNote& chordNote : chord->notes)
                if (const auto* note = std::get_if<Note>(&chordNote); note && note->tie)
                    if (const auto* targets = std::get_if<std::vector<TieTarget>>(&*note->tie))
                        for (const TieTarget& target : *targets)
                            tieTargets.insert(target.to);
        }
    }

    // The clef, key and time signature in force where measure `index` starts.
    void skipTo(std::size_t index)
    {
        for (std::size_t i = 0; i < index; ++i)
        {
            const Measure& measure = in.music.measures[i];
            if (measure.timeSig)
                timeSig = measure.timeSig;
            if (measure.key)
                key = measure.key;
            for (StaffContext& staff : staves)
            {
                StaffPlan before;
                before.clefBefore = staff.clef;
                before.staffMeasure = staff.measureAt(i, measure);
                staff.clef = clefAt(before, std::numeric_limits<Tick>::max());
            }
        }
    }

    double glyphRight(const std::string& glyph) const
    {
        const std::optional<Rect> box = font.bbox(glyph);
        return box ? std::max(0.0, box->right) : 0.0;
    }

    double keyWidth(int accidentals) const
    {
        const int count = std::min(std::abs(accidentals), 7);
        if (count == 0)
            return 0;
        const double each = glyphRight(accidentals > 0 ? "accidentalSharp" : "accidentalFlat");
        return count * each + (count - 1) * in.header.keyAccidentalGapSp;
    }

    double digitsWidth(const std::string& digits) const
    {
        double width = 0;
        for (const char digit : digits)
            width += glyphRight(std::string("timeSig") + digit);
        return width;
    }

    double timeWidth(const TimeSignature& time) const
    {
        return std::max(digitsWidth(timeSigDigits(time.beats, "4")), digitsWidth(timeSigDigits(time.unit, "4")));
    }

    double springFor(Tick gap) const
    {
        const LayoutProfile::Spacing& spacing = in.profile.spacing;
        if (gap <= 0)
            return spacing.minNoteSp;
        const double quarters = static_cast<double>(gap) / tpq;
        return std::max(spacing.minNoteSp, spacing.quarterSp * std::pow(spacing.durationRatio, std::log2(quarters)));
    }

    // Which chords share a beam, per voice: eighths and shorter within one beat, unless
    // Chord::beam says otherwise. A rest ends the group unless it joins the previous
    // event; so does a gap (a Space). A group of one chord gets a flag instead.
    std::vector<std::vector<std::size_t>> beamGroups(const std::vector<PlacedEvent>& events) const
    {
        struct Open
        {
            std::vector<std::size_t> group;
            Tick beat = -1;
            Tick end = 0;
        };
        const Tick beat = beamBeatTicks(timeSig, tpq);
        std::vector<std::vector<std::size_t>> groups;
        std::map<int, Open> open; // per voice
        auto close = [&](Open& o) {
            if (o.group.size() > 1)
                groups.push_back(o.group);
            o.group.clear();
        };
        for (std::size_t i = 0; i < events.size(); ++i)
        {
            const PlacedEvent& event = events[i];
            Open& o = open[event.voice];
            if (event.tick != o.end)
                close(o);
            o.end = event.tick + event.length;
            if (event.rest)
            {
                if (event.rest->beam != BeamMode::JoinPrevious)
                    close(o);
                continue;
            }
            const Chord& chord = *event.chord;
            if (chord.duration.type > DurationType::Eighth || chord.beam == BeamMode::None)
            {
                close(o);
                continue;
            }
            const Tick eventBeat = event.tick / beat;
            if (chord.beam == BeamMode::JoinPrevious && !o.group.empty())
                o.group.push_back(i);
            else if (chord.beam == BeamMode::BreakBefore || eventBeat != o.beat)
            {
                close(o);
                o.group = { i };
            }
            else
                o.group.push_back(i);
            o.beat = eventBeat;
        }
        for (auto& [voice, o] : open)
            close(o);
        std::sort(groups.begin(), groups.end(), [](const auto& a, const auto& b) { return a.front() < b.front(); });
        return groups;
    }

    // A beamed group's stems all point one way: the way a single chord would, judged by
    // the group's note farthest from the middle line. With several voices, by voice.
    bool groupStemUp(const StaffContext& staff, const StaffPlan& plan, const std::vector<std::size_t>& group) const
    {
        if (plan.severalVoices)
            return plan.events[group.front()].voice % 2 == 0;
        const double middle = middleLineY(staff.lines);
        double above = 0;
        double below = 0;
        for (const std::size_t e : group)
        {
            const PlacedEvent& event = plan.events[e];
            const Clef eventClef = clefAt(plan, event.tick);
            for (const ChordNote& chordNote : event.chord->notes)
            {
                const double y = noteY(staff, chordNote, eventClef);
                above = std::max(above, middle - y);
                below = std::max(below, y - middle);
            }
        }
        return below > above;
    }

    // Where a note's head goes: at its written pitch, or its kit entry's line.
    double noteY(const StaffContext& staff, const ChordNote& chordNote, const Clef& noteClef) const
    {
        return std::visit(Overloaded {
                              [&](const Note& note) {
                                  return pitchY(writtenPitch(note.pitch, staff.track->transposition), noteClef,
                                                staff.lines);
                              },
                              [&](const UnpitchedNote& note) {
                                  const KitEntry* entry = kitEntry(staff, note.kitEntryId);
                                  return unpitchedY(entry ? entry->displayPitch : 0, staff.lines);
                              },
                          },
                          chordNote);
    }

    MeasurePlan planMeasure(std::size_t index, bool first)
    {
        const Measure& measure = in.music.measures[index];
        MeasurePlan plan;
        plan.measure = &measure;

        const std::optional<KeySignature> previousKey = key;
        const std::optional<TimeSignature> previousTime = timeSig;
        if (measure.timeSig)
            timeSig = measure.timeSig;
        if (measure.key)
            key = measure.key;
        plan.length = measure.actualTicks.value_or(timeSig ? timeSignatureTicks(*timeSig, tpq) : Tick(4) * tpq);
        if (key)
            plan.keyId = key->id;

        // Which signatures this measure draws at its start. A key signature is drawn on
        // the staves where it has accidentals, as written for the instrument; kits have
        // none.
        const bool keyChanged = measure.key && (!previousKey || previousKey->accidentals != measure.key->accidentals);
        if (first ? measure.timeSig.has_value()
                  : measure.timeSig
                        && (!previousTime || previousTime->beats != measure.timeSig->beats
                            || previousTime->unit != measure.timeSig->unit))
            plan.time = measure.timeSig;

        double clefWidth = 0;
        double keyWidthMax = 0;
        for (StaffContext& staff : staves)
        {
            StaffPlan& staffPlan = plan.staves.emplace_back();
            staffPlan.staffMeasure = staff.measureAt(index, measure);
            staffPlan.clefBefore = staff.clef;
            staffPlan.key = staff.track->kit ? 0 : writtenKey(key ? key->accidentals : 0, staff.track->transposition);
            staffPlan.drawKey = (first || keyChanged) && staffPlan.key != 0;
            if (first)
            {
                staffPlan.clef = clefAt(staffPlan, 0);
                clefWidth = std::max(clefWidth, glyphRight(clefGlyph(*staffPlan.clef, font)));
            }
            if (staffPlan.drawKey)
                keyWidthMax = std::max(keyWidthMax, keyWidth(staffPlan.key));
        }

        // Fixed space before the first note, and where each signature goes.
        const HeaderSpacing& header = in.header;
        double x = first ? 0.0 : in.profile.spacing.measurePaddingSp;
        bool anyItem = false;
        auto placeItem = [&](double width, double& itemX) {
            x += anyItem ? header.betweenItemsSp : (first ? header.clefLeftSp : 0.0);
            itemX = x;
            x += width;
            anyItem = true;
        };
        if (first)
            placeItem(clefWidth, plan.clefX);
        if (keyWidthMax > 0)
            placeItem(keyWidthMax, plan.keyX);
        if (plan.time)
            placeItem(timeWidth(*plan.time), plan.timeX);
        plan.lead = anyItem ? x + header.toFirstNoteSp : x;

        // Events and their shapes, staff by staff.
        for (std::size_t s = 0; s < staves.size(); ++s)
            planEvents(staves[s], plan.staves[s]);

        // Onsets on any staff.
        std::map<Tick, Onset> onsets;
        for (const StaffPlan& staffPlan : plan.staves)
            for (const PlacedEvent& event : staffPlan.events)
            {
                const bool fullMeasure = isFullMeasureRest(event);
                Onset& onset = onsets[fullMeasure ? 0 : event.tick];
                onset.tick = fullMeasure ? 0 : event.tick;
                if (!fullMeasure) // centred in the measure instead
                {
                    onset.left = std::max(onset.left, -event.shape.left);
                    onset.right = std::max(onset.right, event.shape.right);
                }
            }
        if (onsets.empty())
            onsets[0].tick = 0; // an empty measure still takes its length's room

        // Each onset's space: what its duration asks for, but at least enough that its
        // shapes and the next onset's keep collisionSp apart.
        const double padding = in.profile.padding.collisionSp;
        for (auto it = onsets.begin(); it != onsets.end(); ++it)
        {
            const auto next = std::next(it);
            const Tick end = next == onsets.end() ? std::max(plan.length, it->first) : next->first;
            const double nextLeft = next == onsets.end() ? 0.0 : next->second.left;
            Onset& onset = it->second;
            onset.minimum = onset.right + padding + nextLeft;
            onset.spring = std::max(springFor(end - it->first), onset.minimum);
            plan.onsets.push_back(onset);
        }
        plan.lead += plan.onsets.front().left; // room for the first notes' accidentals

        for (std::size_t s = 0; s < staves.size(); ++s)
            staves[s].clef = clefAt(plan.staves[s], std::numeric_limits<Tick>::max());
        return plan;
    }

    void planEvents(const StaffContext& staff, StaffPlan& plan) const
    {
        std::set<int> voices;
        if (plan.staffMeasure)
            for (const Voice& voice : plan.staffMeasure->voices)
            {
                const std::size_t before = plan.events.size();
                placeEvents(voice.events, 0, 1, 1, tpq, voice.index, plan.events);
                if (plan.events.size() > before)
                    voices.insert(voice.index);
            }
        plan.severalVoices = voices.size() > 1;

        // In time order, so each note knows the accidentals before it in the measure.
        std::stable_sort(plan.events.begin(), plan.events.end(),
                         [](const PlacedEvent& a, const PlacedEvent& b) { return a.tick < b.tick; });

        // Beam groups, and the stem direction each one shares.
        plan.beams = beamGroups(plan.events);
        std::vector<std::optional<bool>> beamUp(plan.events.size());
        for (const std::vector<std::size_t>& group : plan.beams)
        {
            const bool up = groupStemUp(staff, plan, group);
            for (const std::size_t e : group)
                beamUp[e] = up;
        }

        MeasureAccidentals accidentals(plan.key);
        for (std::size_t e = 0; e < plan.events.size(); ++e)
        {
            PlacedEvent& event = plan.events[e];
            if (event.chord)
            {
                std::vector<std::optional<AccidentalMark>> marks;
                for (const ChordNote& chordNote : event.chord->notes)
                {
                    const auto* note = std::get_if<Note>(&chordNote);
                    marks.push_back(note ? accidentals.next(writtenPitch(note->pitch, staff.track->transposition),
                                                            note->accidentalDisplay, tieTargets.contains(note->id))
                                         : std::nullopt);
                }
                event.shape = chordShape(staff, plan, event, clefAt(plan, event.tick), marks, beamUp[e]);
            }
            else
            {
                event.shape = restShape(staff, plan, event);
            }
        }
    }

    // ── Emitting boxes ─────────────────────────────────────────────────────────

    void addGlyph(const NodeId& nodeId, Part part, std::string glyph, Point origin, double scale = 1)
    {
        const Rect box = font.bbox(glyph).value_or(Rect {}).scaled(scale);
        out.boxes.push_back({ nodeId, part, box.translated(origin), GlyphShape { std::move(glyph), origin, scale } });
    }

    void addLine(const NodeId& nodeId, Part part, Point from, Point to, double thickness)
    {
        out.boxes.push_back({ nodeId, part, lineBounds(from, to, thickness), LineShape { from, to, thickness } });
    }

    void addBeam(const NodeId& nodeId, Point from, Point to, double thickness)
    {
        const double half = thickness / 2;
        const Rect bounds { from.x, std::min(from.y, to.y) - half, to.x, std::max(from.y, to.y) + half };
        out.boxes.push_back({ nodeId, Part::Beam, bounds, BeamShape { from, to, thickness } });
    }

    // An event's shape, moved by `by` into the system.
    void emitShape(const EventShape& shape, Point by)
    {
        for (const ShapeItem& item : shape.items)
        {
            if (const auto* glyph = std::get_if<GlyphShape>(&item.shape))
                addGlyph(*item.nodeId, item.part, glyph->glyph, glyph->origin + by);
            else if (const auto* line = std::get_if<LineShape>(&item.shape))
                addLine(*item.nodeId, item.part, line->from + by, line->to + by, line->thickness);
        }
    }

    void emitStaffLines()
    {
        for (const StaffContext& staff : staves)
            for (int i = 0; i < staff.lines; ++i)
                addLine(staff.staff->id, Part::StaffLine, { 0, staff.y + i }, { out.width, staff.y + i },
                        engraving.staffLineThickness);
    }

    // Where a staff's barlines start and end: its outer lines, or a space above and
    // below a one-line staff.
    static double barTop(const StaffContext& staff) { return staff.y + (staff.lines > 1 ? 0.0 : -1.0); }
    static double barBottom(const StaffContext& staff) { return staff.y + (staff.lines > 1 ? staff.lines - 1.0 : 1.0); }

    // The staves of one track, which barlines and braces join.
    struct TrackSpan
    {
        const Track* track;
        double top;
        double bottom;
        int staves;
    };

    std::vector<TrackSpan> trackSpans() const
    {
        std::vector<TrackSpan> spans;
        for (const StaffContext& staff : staves)
        {
            auto it = std::find_if(spans.begin(), spans.end(),
                                   [&](const TrackSpan& span) { return span.track == staff.track; });
            if (it == spans.end())
                spans.push_back({ staff.track, barTop(staff), barBottom(staff), 1 });
            else
            {
                it->top = std::min(it->top, barTop(staff));
                it->bottom = std::max(it->bottom, barBottom(staff));
                ++it->staves;
            }
        }
        return spans;
    }

    // A line joining every staff at the system's start, and a brace before each
    // instrument with several staves.
    void emitSystemStart()
    {
        const std::vector<TrackSpan> spans = trackSpans();
        if (staves.size() > 1)
        {
            double top = std::numeric_limits<double>::max();
            double bottom = std::numeric_limits<double>::lowest();
            for (const TrackSpan& span : spans)
            {
                top = std::min(top, span.top);
                bottom = std::max(bottom, span.bottom);
            }
            const double thin = engraving.thinBarlineThickness;
            addLine(plans.front().measure->id, Part::Barline, { thin / 2, top }, { thin / 2, bottom }, thin);
        }

        const std::optional<Rect> brace = font.bbox("brace");
        for (const TrackSpan& span : spans)
            if (span.staves > 1 && brace && brace->height() > 0)
            {
                // The brace glyph is one staff tall, its origin at its bottom.
                const double scale = (span.bottom - span.top) / brace->height();
                const Point origin { -in.header.braceGapSp - brace->right * scale,
                                     span.bottom - brace->bottom * scale };
                addGlyph(span.track->id, Part::Brace, "brace", origin, scale);
            }
    }

    // The space an onset gets once the system is justified.
    double space(const Onset& onset) const
    {
        if (squeeze < 1)
            return onset.minimum + squeeze * (onset.spring - onset.minimum);
        return onset.spring * stretch;
    }

    double emitMeasure(const MeasurePlan& plan, double left)
    {
        // Where each onset goes, and the barline.
        std::map<Tick, double> onsetX;
        double x = left + plan.lead;
        for (const Onset& onset : plan.onsets)
        {
            onsetX[onset.tick] = x;
            x += space(onset);
        }
        const double right = x;
        const double contentLeft = left + plan.lead;

        for (std::size_t s = 0; s < staves.size(); ++s)
        {
            const StaffContext& staff = staves[s];
            const StaffPlan& staffPlan = plan.staves[s];
            emitSignatures(staff, staffPlan, plan, left);

            std::vector<double> eventX(staffPlan.events.size());
            for (std::size_t e = 0; e < staffPlan.events.size(); ++e)
            {
                const PlacedEvent& event = staffPlan.events[e];
                if (isFullMeasureRest(event))
                    eventX[e] = (contentLeft + right) / 2 - (event.shape.left + event.shape.right) / 2;
                else
                    eventX[e] = onsetX.at(event.tick);
                emitShape(event.shape, { eventX[e], staff.y });
            }
            for (const std::vector<std::size_t>& group : staffPlan.beams)
                emitBeamGroup(staff, staffPlan, group, eventX);
        }

        for (const TrackSpan& span : trackSpans())
            emitBarline(*plan.measure, right, span.top, span.bottom);
        out.measures.push_back({ plan.measure->id, left, right });
        return right;
    }

    // A beam group's stems and beams. The beam follows the end notes part of the way
    // (flat when an inner note is nearer to it than both ends), then sits as near the
    // notes as the shortest stem allows. Secondary beams join neighbours that both have
    // them; a chord alone at its level gets a hook, pointing into the group.
    void emitBeamGroup(const StaffContext& staff, const StaffPlan& plan, const std::vector<std::size_t>& group,
                       const std::vector<double>& eventX)
    {
        struct StemAt
        {
            const NodeId* chordId;
            BeamedStem stem;
            double x; // in the system
        };
        std::vector<StemAt> stems;
        for (const std::size_t e : group)
        {
            const PlacedEvent& event = plan.events[e];
            if (!event.shape.beamedStem)
                return; // a chord without notes; nothing to beam
            stems.push_back({ &event.chord->id, *event.shape.beamedStem, eventX[e] + event.shape.beamedStem->x });
        }
        const BeamSpacing& style = in.beams;
        const bool up = stems.front().stem.up;
        const double toNotes = up ? 1.0 : -1.0; // y direction from the beam to the notes
        const double thickness = engraving.beamThickness;
        const double distance = engraving.beamSpacing + thickness; // one beam's outer edge to the next's

        const StemAt& first = stems.front();
        const StemAt& last = stems.back();
        const double span = last.x - first.x;
        double rise =
            std::clamp((last.stem.nearY - first.stem.nearY) * style.riseRatio, -style.maxRiseSp, style.maxRiseSp);
        rise = std::clamp(rise, -style.maxSlope * span, style.maxSlope * span);
        const double endsNearest =
            up ? std::min(first.stem.nearY, last.stem.nearY) : std::max(first.stem.nearY, last.stem.nearY);
        for (std::size_t i = 1; i + 1 < stems.size(); ++i)
            if (up ? stems[i].stem.nearY < endsNearest : stems[i].stem.nearY > endsNearest)
                rise = 0;
        const double slope = span > 0 ? rise / span : 0.0;

        // The beam's outer edge is base + slope * (x - first.x), in the staff's own
        // coordinates. Every stem gets at least stemSp, and room for its beams with
        // clearanceSp between them and the note.
        double base = up ? std::numeric_limits<double>::max() : std::numeric_limits<double>::lowest();
        for (const StemAt& s : stems)
        {
            const double need = std::max(style.stemSp, style.clearanceSp + (s.stem.beams - 1) * distance + thickness);
            const double candidate = s.stem.nearY - toNotes * need - slope * (s.x - first.x);
            base = up ? std::min(base, candidate) : std::max(base, candidate);
        }
        // Like an unbeamed stem, the beam reaches at least the middle line.
        const double middle = middleLineY(staff.lines);
        const double nearestEnd = up ? std::min(base, base + slope * span) : std::max(base, base + slope * span);
        if (up ? nearestEnd > middle : nearestEnd < middle)
            base += middle - nearestEnd;
        const double dy = staff.y;
        auto edgeAt = [&](double x) { return dy + base + slope * (x - first.x); };

        const double stemThickness = engraving.stemThickness;
        for (const StemAt& s : stems)
            addLine(*s.chordId, Part::Stem, { s.x, dy + s.stem.startY }, { s.x, edgeAt(s.x) }, stemThickness);

        const double half = stemThickness / 2;
        const double hook = glyphRight("noteheadBlack");
        int levels = 0;
        for (const StemAt& s : stems)
            levels = std::max(levels, s.stem.beams);
        for (int level = 1; level <= levels; ++level)
        {
            const double offset = toNotes * ((level - 1) * distance + thickness / 2); // edge → this beam's centre
            auto beam = [&](const NodeId& nodeId, double fromX, double toX) {
                addBeam(nodeId, { fromX, edgeAt(fromX) + offset }, { toX, edgeAt(toX) + offset }, thickness);
            };
            for (std::size_t i = 0; i < stems.size();)
            {
                if (stems[i].stem.beams < level)
                {
                    ++i;
                    continue;
                }
                std::size_t j = i;
                while (j + 1 < stems.size() && stems[j + 1].stem.beams >= level)
                    ++j;
                const StemAt& s = stems[i];
                if (j > i)
                    beam(*s.chordId, s.x - half, stems[j].x + half);
                else if (i + 1 == stems.size())
                    beam(*s.chordId, s.x - hook, s.x + half); // the last chord: back into the group
                else
                    beam(*s.chordId, s.x - half, s.x + hook);
                i = j + 1;
            }
        }
    }

    void emitSignatures(const StaffContext& staff, const StaffPlan& staffPlan, const MeasurePlan& plan, double left)
    {
        const double dy = staff.y;
        if (staffPlan.clef)
            addGlyph(staffPlan.clef->id, Part::Clef, clefGlyph(*staffPlan.clef, font),
                     { left + plan.clefX, dy + clefY(*staffPlan.clef, staff.lines) });

        if (staffPlan.drawKey)
        {
            const NodeId& keyId = plan.keyId;
            const Clef keyClef = clefAt(staffPlan, 0);
            const std::string glyph = staffPlan.key > 0 ? "accidentalSharp" : "accidentalFlat";
            double x = left + plan.keyX;
            for (const double y : keySignatureYs(staffPlan.key, keyClef, staff.lines))
            {
                addGlyph(keyId, Part::KeySignature, glyph, { x, dy + y });
                x += glyphRight(glyph) + in.header.keyAccidentalGapSp;
            }
        }

        if (plan.time)
        {
            const double width = timeWidth(*plan.time);
            const double middle = dy + middleLineY(staff.lines);
            auto emitDigits = [&](const std::string& digits, double y) {
                double x = left + plan.timeX + (width - digitsWidth(digits)) / 2;
                for (const char digit : digits)
                {
                    const std::string glyph = std::string("timeSig") + digit;
                    addGlyph(plan.time->id, Part::TimeSignature, glyph, { x, y });
                    x += glyphRight(glyph);
                }
            };
            // SMuFL time signature digits are centred on their origin.
            emitDigits(timeSigDigits(plan.time->beats, "4"), middle - 1);
            emitDigits(timeSigDigits(plan.time->unit, "4"), middle + 1);
        }
    }

    struct Head
    {
        const NodeId* nodeId;
        std::string glyph;
        double y;
        double width = 0;
        double x = 0; // 0, or beside the stem when a second away from its neighbour
        std::optional<AccidentalMark> accidental;
    };

    // Adds an item to a shape and widens the shape's extent to cover it.
    void addItem(EventShape& shape, const NodeId& nodeId, Part part, std::variant<GlyphShape, LineShape> what) const
    {
        Rect bounds;
        if (const auto* glyph = std::get_if<GlyphShape>(&what))
            bounds = font.bbox(glyph->glyph).value_or(Rect {}).translated(glyph->origin);
        else if (const auto* line = std::get_if<LineShape>(&what))
            bounds = lineBounds(line->from, line->to, line->thickness);
        if (shape.items.empty())
        {
            shape.left = bounds.left;
            shape.right = bounds.right;
        }
        else
        {
            shape.left = std::min(shape.left, bounds.left);
            shape.right = std::max(shape.right, bounds.right);
        }
        shape.items.push_back({ &nodeId, part, std::move(what) });
    }

    // Augmentation dots right of `x`, one row per y.
    void addDots(EventShape& shape, const std::vector<std::pair<const NodeId*, double>>& rows, double x, int dots) const
    {
        const double dotWidth = glyphRight("augmentationDot");
        for (const auto& [nodeId, y] : rows)
            for (int i = 0; i < dots; ++i)
                addItem(shape, *nodeId, Part::Dot,
                        GlyphShape { "augmentationDot", { x + i * (dotWidth + in.notes.dotDotGapSp), y } });
    }

    static const KitEntry* kitEntry(const StaffContext& staff, const NodeId& id)
    {
        if (!staff.track->kit)
            return nullptr;
        for (const KitEntry& entry : *staff.track->kit)
            if (entry.id == id)
                return &entry;
        return nullptr;
    }

    // A chord around x = 0: ledger lines, noteheads (seconds beside each other), stem,
    // flag, dots and accidentals. `marks` has one entry per note of the chord. A beamed
    // chord (beamUp set) takes its stem direction from its group, and has no stem or
    // flag yet: the stem waits for the beam.
    EventShape chordShape(const StaffContext& staff, const StaffPlan& plan, const PlacedEvent& event,
                          const Clef& noteClef, const std::vector<std::optional<AccidentalMark>>& marks,
                          std::optional<bool> beamUp) const
    {
        const Chord& chord = *event.chord;
        const DurationType type = chord.duration.type;
        const int lines = staff.lines;
        const double middle = middleLineY(lines);
        const double thickness = engraving.stemThickness;
        EventShape shape;

        std::vector<Head> heads;
        for (std::size_t i = 0; i < chord.notes.size(); ++i)
            std::visit(
                Overloaded {
                    [&](const Note& note) {
                        heads.push_back({ &note.id, noteheadGlyph(type), noteY(staff, chord.notes[i], noteClef) });
                        heads.back().accidental = i < marks.size() ? marks[i] : std::nullopt;
                    },
                    [&](const UnpitchedNote& note) {
                        const KitEntry* entry = kitEntry(staff, note.kitEntryId);
                        heads.push_back({ &note.id,
                                          kitNoteheadGlyph(entry ? entry->notehead : Notehead::Normal, type, font),
                                          noteY(staff, chord.notes[i], noteClef) });
                    },
                },
                chord.notes[i]);
        if (heads.empty())
            return shape;
        for (Head& head : heads)
            head.width = glyphRight(head.glyph);

        const auto [topIt, bottomIt] =
            std::minmax_element(heads.begin(), heads.end(), [](const Head& a, const Head& b) { return a.y < b.y; });
        const double topY = topIt->y;
        const double bottomY = bottomIt->y;

        const bool hasStem = type <= DurationType::Half; // whole notes and longer have none
        const bool up = beamUp               ? *beamUp
                        : plan.severalVoices ? event.voice % 2 == 0
                                             : (bottomY - middle) > (middle - topY); // most of it below the middle

        // Seconds: two notes a step apart can't share a column. Going away from the stem's
        // start (from the bottom for an up stem), a note a second from one in the normal
        // column moves to the other side of the stem, then the next one comes back.
        std::vector<Head*> order;
        for (Head& head : heads)
            order.push_back(&head);
        const bool fromBottom = up || !hasStem;
        std::sort(order.begin(), order.end(),
                  [&](const Head* a, const Head* b) { return fromBottom ? a->y > b->y : a->y < b->y; });
        const double headWidth = std::max_element(heads.begin(), heads.end(), [](const Head& a, const Head& b) {
                                     return a.width < b.width;
                                 })->width;
        const double displacement = !hasStem ? headWidth : up ? headWidth - thickness : -(headWidth - thickness);
        const Head* previous = nullptr;
        for (Head* head : order)
        {
            const bool second = previous && previous->x == 0 && std::abs(std::abs(head->y - previous->y) - 0.5) < 1e-6;
            head->x = second ? displacement : 0;
            previous = head;
        }

        double headsLeft = 0;
        double headsRight = 0;
        for (const Head& head : heads)
        {
            headsLeft = std::min(headsLeft, head.x);
            headsRight = std::max(headsRight, head.x + head.width);
        }

        // Ledger lines first, so noteheads paint over them.
        const double extension = engraving.legerLineExtension;
        auto ledger = [&](double y) {
            addItem(shape, chord.id, Part::LedgerLine,
                    LineShape {
                        { headsLeft - extension, y }, { headsRight + extension, y }, engraving.legerLineThickness });
        };
        for (double y = -1; y >= topY - 1e-9; y -= 1)
            ledger(y);
        for (double y = lines; y <= bottomY + 1e-9; y += 1)
            ledger(y);

        for (const Head& head : heads)
            addItem(shape, *head.nodeId, Part::Notehead, GlyphShape { head.glyph, { head.x, head.y } });

        // Stem, from the note at its start (always in the normal column), and the flag at
        // its tip. The flag's anchor says where the stem must end, which lengthens it for
        // 32nds and shorter.
        if (hasStem)
        {
            const Head& start = up ? *bottomIt : *topIt;
            const Rect box = font.bbox(start.glyph).value_or(Rect {});
            const Point anchor = up ? font.anchor(start.glyph, "stemUpSE").value_or(Point { box.right, 0 })
                                    : font.anchor(start.glyph, "stemDownNW").value_or(Point { box.left, 0 });
            const double stemX = anchor.x + (up ? -thickness / 2 : thickness / 2);
            const int flags = flagCount(type);
            if (beamUp)
            {
                shape.beamedStem = BeamedStem { stemX, start.y + anchor.y, up ? topY : bottomY, flags, up };
            }
            else
            {
                const double tip =
                    up ? std::min(topY - stemLengthSp, middle) : std::max(bottomY + stemLengthSp, middle);
                double end = tip;
                std::optional<GlyphShape> flag;
                if (flags > 0)
                {
                    const std::string glyph = flagGlyph(flags, up);
                    const Point flagAnchor = font.anchor(glyph, up ? "stemUpNW" : "stemDownSW").value_or(Point {});
                    const Point origin { stemX - thickness / 2 - flagAnchor.x, tip };
                    end = tip + flagAnchor.y;
                    flag = GlyphShape { glyph, origin };
                }
                addItem(shape, chord.id, Part::Stem,
                        LineShape { { stemX, start.y + anchor.y }, { stemX, end }, thickness });
                if (flag)
                    addItem(shape, chord.id, Part::Flag, *flag);
            }
        }

        // Dots: in the space of each note, the one above for a note on a line (below, for
        // the lower voice). Two notes wanting the same space share one dot.
        if (chord.duration.dots > 0)
        {
            const bool lowerVoice = plan.severalVoices && !up;
            std::vector<std::pair<const NodeId*, double>> rows;
            for (const Head& head : heads)
            {
                const double y = onLine(head.y) ? head.y + (lowerVoice ? 0.5 : -0.5) : head.y;
                const bool taken = std::any_of(rows.begin(), rows.end(),
                                               [&](const auto& row) { return std::abs(row.second - y) < 1e-6; });
                if (!taken)
                    rows.push_back({ head.nodeId, y });
            }
            addDots(shape, rows, headsRight + in.notes.dotGapSp, chord.duration.dots);
        }

        addAccidentals(shape, heads, headsLeft);
        return shape;
    }

    // Accidentals left of the noteheads, top to bottom. Each goes in the column nearest
    // the notes where it doesn't overlap another vertically; columns step to the left.
    void addAccidentals(EventShape& shape, const std::vector<Head>& heads, double headsLeft) const
    {
        struct Placed
        {
            const Head* head;
            std::vector<std::string> glyphs; // left to right: ( accidental )
            double width = 0;
            double top = 0;
            double bottom = 0;
            std::size_t column = 0;
        };
        std::vector<Placed> placed;
        for (const Head& head : heads)
        {
            if (!head.accidental)
                continue;
            Placed p { &head, {} };
            const std::string glyph = accidentalGlyph(head.accidental->alter);
            if (head.accidental->parentheses)
                p.glyphs = { "accidentalParensLeft", glyph, "accidentalParensRight" };
            else
                p.glyphs = { glyph };
            const Rect box = font.bbox(glyph).value_or(Rect {});
            p.top = head.y + box.top;
            p.bottom = head.y + box.bottom;
            for (const std::string& g : p.glyphs)
                p.width += glyphRight(g);
            placed.push_back(std::move(p));
        }
        std::sort(placed.begin(), placed.end(), [](const Placed& a, const Placed& b) { return a.head->y < b.head->y; });

        std::vector<std::vector<const Placed*>> columns;
        std::vector<double> columnWidths;
        const double overlap = in.notes.accidentalOverlapSp;
        for (Placed& p : placed)
        {
            std::size_t c = 0;
            for (; c < columns.size(); ++c)
            {
                const bool clash = std::any_of(columns[c].begin(), columns[c].end(), [&](const Placed* other) {
                    return other->top < p.bottom - overlap && other->bottom > p.top + overlap;
                });
                if (!clash)
                    break;
            }
            if (c == columns.size())
            {
                columns.emplace_back();
                columnWidths.push_back(0);
            }
            p.column = c;
            columns[c].push_back(&p);
            columnWidths[c] = std::max(columnWidths[c], p.width);
        }

        // Each column's right edge; accidentals sit against it.
        std::vector<double> columnRight;
        double right = headsLeft - in.notes.accidentalGapSp;
        for (const double width : columnWidths)
        {
            columnRight.push_back(right);
            right -= width + in.notes.accidentalColumnGapSp;
        }

        for (const Placed& p : placed)
        {
            double x = columnRight[p.column] - p.width;
            for (const std::string& glyph : p.glyphs)
            {
                addItem(shape, *p.head->nodeId, Part::Accidental, GlyphShape { glyph, { x, p.head->y } });
                x += glyphRight(glyph);
            }
        }
    }

    // A rest around x = 0, with its dots. A full-measure rest is centred when emitted.
    EventShape restShape(const StaffContext& staff, const StaffPlan& plan, const PlacedEvent& event) const
    {
        const Rest& rest = *event.rest;
        const bool fullMeasure = rest.fullMeasure.value_or(false);
        const std::string glyph = fullMeasure ? "restWhole" : restGlyph(rest.duration.type);

        // SMuFL rests hang or sit on the line at their origin: a whole rest hangs from
        // the line above the middle, the others centre on the middle line.
        double y = middleLineY(staff.lines);
        if (glyph == "restWhole")
            y -= 1;
        if (plan.severalVoices)
            y += event.voice % 2 == 0 ? -1 : 1;

        EventShape shape;
        addItem(shape, rest.id, Part::Rest, GlyphShape { glyph, { 0, y } });
        if (!fullMeasure && rest.duration.dots > 0)
            addDots(shape, { { &rest.id, onLine(y) ? y - 0.5 : y } }, glyphRight(glyph) + in.notes.dotGapSp,
                    rest.duration.dots);
        return shape;
    }

    void emitBarline(const Measure& measure, double right, double top, double bottom)
    {
        auto line = [&](double centreX, double thickness) {
            addLine(measure.id, Part::Barline, { centreX, top }, { centreX, bottom }, thickness);
        };
        const double thin = engraving.thinBarlineThickness;
        const double thick = engraving.thickBarlineThickness;
        switch (measure.barline.value_or(BarlineStyle::Single))
        {
        case BarlineStyle::Single:
        case BarlineStyle::Dashed: // drawn solid for now
            line(right - thin / 2, thin);
            break;
        case BarlineStyle::Double:
            line(right - thin / 2, thin);
            line(right - thin - engraving.barlineSeparation - thin / 2, thin);
            break;
        case BarlineStyle::Final:
            line(right - thick - engraving.thinThickBarlineSeparation - thin / 2, thin);
            line(right - thick / 2, thick);
            break;
        }
    }

    const SystemInput& in;
    const SmuflFont& font;
    const EngravingDefaults& engraving;
    const int tpq;
    std::vector<StaffContext> staves;
    std::set<NodeId, std::less<>> tieTargets;

    // In force where planning has got to.
    std::optional<TimeSignature> timeSig;
    std::optional<KeySignature> key;

    std::vector<MeasurePlan> plans;
    double stretch = 1; // > 1: every space grows by this factor
    double squeeze = 1; // < 1: every space sits this share of the way from its minimum to its natural size
    SystemLayout out;
};

} // namespace

SystemLayout layoutSystem(const SystemInput& input) { return SystemBuilder(input).build(); }

std::vector<MeasureWidth> measureWidths(const SystemInput& input) { return SystemBuilder(input).widths(); }

} // namespace msd
