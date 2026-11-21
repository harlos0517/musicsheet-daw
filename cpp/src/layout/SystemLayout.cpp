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

// How this works
// ──────────────
// 1. Plan: walk the measures before the system to know the clef, key and time
//    signature in force, then for each measure in the system find its events and the
//    ticks where something starts (onsets). Each chord and rest gets its shape —
//    noteheads, accidentals, stem, flag, dots — drawn around x = 0, so its extent to
//    the left and right is known. Each onset gets the space its duration asks for, but
//    at least enough that its shapes don't touch the next onset's.
// 2. Justify: the fixed parts (header, padding) keep their width; the space between
//    onsets is stretched by one factor, or squeezed towards each onset's minimum, so
//    the system fills `width`. It never squeezes below the minimum.
// 3. Emit: turn every staff line, signature, event shape and barline into LayoutBoxes
//    at their final position.

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

// One glyph or line of an event, before it has a place in the system.
struct ShapeItem
{
    const NodeId* nodeId = nullptr;
    Part part = Part::Notehead;
    std::variant<GlyphShape, LineShape> shape;
};

// Everything one chord or rest draws, around its onset at x = 0.
struct EventShape
{
    std::vector<ShapeItem> items; // in drawing order
    double left = 0;              // the leftmost point (accidentals, ledger lines)
    double right = 0;             // the rightmost point (noteheads, flags, dots)
};

// A chord or rest with the tick it starts at, from the start of its measure.
struct PlacedEvent
{
    const Chord* chord = nullptr; // exactly one of chord and rest is set
    const Rest* rest = nullptr;
    int voice = 0;
    Tick tick = 0;
    EventShape shape {};
};

bool onLine(double y) { return std::abs(y - std::round(y)) < 1e-6; }

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
                           out.push_back({ &chord, nullptr, voice, at });
                           nominal += nominalTicks(chord.duration, tpq);
                       },
                       [&](const Rest& rest) {
                           out.push_back({ nullptr, &rest, voice, at });
                           nominal += nominalTicks(rest.duration, tpq);
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

// ── Plans ──────────────────────────────────────────────────────────────────────

struct Onset
{
    Tick tick = 0;
    double spring = 0;                // natural space from here to the next onset
    double minimum = 0;               // the least space that keeps its shapes clear of the next
    double left = 0;                  // how far its shapes reach left of the onset (≥ 0)
    double right = 0;                 // how far they reach right of it
    std::vector<std::size_t> events;  // indices into MeasurePlan::events
};

struct MeasurePlan
{
    const Measure* measure = nullptr;
    const StaffMeasure* staffMeasure = nullptr; // null when this staff has nothing here
    Tick length = 0;
    Clef clefBefore; // in force before this measure's own clef changes

    // Signatures drawn at the measure's start (the system header, for the first one).
    std::optional<Clef> clef;
    std::optional<KeySignature> key;
    std::optional<TimeSignature> time;
    double clefX = 0; // from the measure's left edge
    double keyX = 0;
    double timeX = 0;
    double lead = 0;  // left edge → first onset: fixed, never stretched

    std::vector<PlacedEvent> events;
    std::vector<Onset> onsets;
    bool severalVoices = false;
};

Clef clefAt(const MeasurePlan& plan, Tick tick)
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
    SystemBuilder(const SystemInput& input, const Track& track, const Staff& staff)
        : in(input)
        , track(track)
        , staff(staff)
        , font(input.font)
        , engraving(input.font.engravingDefaults())
        , lines(staff.lines.value_or(5))
        , tpq(input.music.tpq > 0 ? input.music.tpq : 480)
    {
        for (const StaffMeasure& staffMeasure : staff.measures)
        {
            staffMeasures.emplace(staffMeasure.measureId, &staffMeasure);
            for (const Voice& voice : staffMeasure.voices)
                collectTieTargets(voice.events);
        }
    }

    SystemLayout build()
    {
        // The clef, key and time signature in force where the system starts.
        for (std::size_t i = 0; i < in.firstMeasure; ++i)
        {
            const Measure& measure = in.music.measures[i];
            if (measure.timeSig)
                timeSig = measure.timeSig;
            if (measure.key)
                key = measure.key;
            MeasurePlan before;
            before.clefBefore = clef;
            before.staffMeasure = staffMeasureFor(measure);
            clef = clefAt(before, std::numeric_limits<Tick>::max());
        }

        for (std::size_t i = in.firstMeasure; i < in.firstMeasure + in.measureCount; ++i)
            plans.push_back(planMeasure(in.music.measures[i], i == in.firstMeasure));

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

        out.staffId = staff.id;
        out.staffLines = lines;
        out.width = fixed;
        for (const MeasurePlan& plan : plans)
            for (const Onset& onset : plan.onsets)
                out.width += space(onset);

        emitStaffLines();
        double x = 0;
        for (const MeasurePlan& plan : plans)
            x = emitMeasure(plan, x);
        return std::move(out);
    }

private:
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

    const StaffMeasure* staffMeasureFor(const Measure& measure) const
    {
        const auto it = staffMeasures.find(measure.id);
        return it == staffMeasures.end() ? nullptr : it->second;
    }

    double glyphRight(const std::string& glyph) const
    {
        const std::optional<Rect> box = font.bbox(glyph);
        return box ? std::max(0.0, box->right) : 0.0;
    }

    double keyWidth(const KeySignature& keySig) const
    {
        const int count = std::min(std::abs(keySig.accidentals), 7);
        if (count == 0)
            return 0;
        const double each = glyphRight(keySig.accidentals > 0 ? "accidentalSharp" : "accidentalFlat");
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

    MeasurePlan planMeasure(const Measure& measure, bool first)
    {
        MeasurePlan plan;
        plan.measure = &measure;
        plan.staffMeasure = staffMeasureFor(measure);
        plan.clefBefore = clef;

        const std::optional<KeySignature> previousKey = key;
        const std::optional<TimeSignature> previousTime = timeSig;
        if (measure.timeSig)
            timeSig = measure.timeSig;
        if (measure.key)
            key = measure.key;
        plan.length = measure.actualTicks.value_or(timeSig ? timeSignatureTicks(*timeSig, tpq) : Tick(4) * tpq);

        // Which signatures this measure draws at its start.
        if (first)
        {
            plan.clef = clefAt(plan, 0);
            if (key && key->accidentals != 0)
                plan.key = key;
            if (measure.timeSig)
                plan.time = measure.timeSig;
        }
        else
        {
            if (measure.key && measure.key->accidentals != 0
                && (!previousKey || previousKey->accidentals != measure.key->accidentals))
                plan.key = measure.key;
            if (measure.timeSig
                && (!previousTime || previousTime->beats != measure.timeSig->beats
                    || previousTime->unit != measure.timeSig->unit))
                plan.time = measure.timeSig;
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
        if (plan.clef)
            placeItem(glyphRight(clefGlyph(*plan.clef, font)), plan.clefX);
        if (plan.key)
            placeItem(keyWidth(*plan.key), plan.keyX);
        if (plan.time)
            placeItem(timeWidth(*plan.time), plan.timeX);
        plan.lead = anyItem ? x + header.toFirstNoteSp : x;

        // Events, and the onsets they make.
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
        MeasureAccidentals accidentals(key ? key->accidentals : 0);
        for (PlacedEvent& event : plan.events)
        {
            if (event.chord)
            {
                std::vector<std::optional<AccidentalMark>> marks;
                for (const ChordNote& chordNote : event.chord->notes)
                {
                    const auto* note = std::get_if<Note>(&chordNote);
                    marks.push_back(
                        note ? accidentals.next(note->pitch, note->accidentalDisplay, tieTargets.contains(note->id))
                             : std::nullopt);
                }
                event.shape = chordShape(event, clefAt(plan, event.tick), plan.severalVoices, marks);
            }
            else
            {
                event.shape = restShape(event, plan.severalVoices);
            }
        }

        std::map<Tick, Onset> onsets;
        for (std::size_t i = 0; i < plan.events.size(); ++i)
        {
            const PlacedEvent& event = plan.events[i];
            const bool fullMeasure = event.rest && event.rest->fullMeasure.value_or(false);
            const Tick tick = fullMeasure ? 0 : event.tick;
            Onset& onset = onsets[tick];
            onset.tick = tick;
            onset.events.push_back(i);
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

        clef = clefAt(plan, std::numeric_limits<Tick>::max());
        return plan;
    }

    // ── Emitting boxes ─────────────────────────────────────────────────────────

    void addGlyph(const NodeId& nodeId, Part part, std::string glyph, Point origin)
    {
        const Rect box = font.bbox(glyph).value_or(Rect {});
        out.boxes.push_back({ nodeId, part, box.translated(origin), GlyphShape { std::move(glyph), origin } });
    }

    void addLine(const NodeId& nodeId, Part part, Point from, Point to, double thickness)
    {
        out.boxes.push_back({ nodeId, part, lineBounds(from, to, thickness), LineShape { from, to, thickness } });
    }

    // An event's shape, moved right by dx into the system.
    void emitShape(const EventShape& shape, double dx)
    {
        for (const ShapeItem& item : shape.items)
        {
            if (const auto* glyph = std::get_if<GlyphShape>(&item.shape))
                addGlyph(*item.nodeId, item.part, glyph->glyph, glyph->origin + Point { dx, 0 });
            else if (const auto* line = std::get_if<LineShape>(&item.shape))
                addLine(*item.nodeId, item.part, line->from + Point { dx, 0 }, line->to + Point { dx, 0 },
                        line->thickness);
        }
    }

    void emitStaffLines()
    {
        for (int i = 0; i < lines; ++i)
            addLine(staff.id, Part::StaffLine, { 0, double(i) }, { out.width, double(i) },
                    engraving.staffLineThickness);
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
        emitSignatures(plan, left);

        // Where each onset goes, and the barline.
        std::vector<double> onsetX;
        double x = left + plan.lead;
        for (const Onset& onset : plan.onsets)
        {
            onsetX.push_back(x);
            x += space(onset);
        }
        const double right = x;
        const double contentLeft = left + plan.lead;

        for (std::size_t i = 0; i < plan.onsets.size(); ++i)
            for (const std::size_t e : plan.onsets[i].events)
            {
                const PlacedEvent& event = plan.events[e];
                if (event.rest && event.rest->fullMeasure.value_or(false))
                    emitShape(event.shape, (contentLeft + right) / 2 - (event.shape.left + event.shape.right) / 2);
                else
                    emitShape(event.shape, onsetX[i]);
            }

        emitBarline(*plan.measure, right);
        out.measures.push_back({ plan.measure->id, left, right });
        return right;
    }

    void emitSignatures(const MeasurePlan& plan, double left)
    {
        if (plan.clef)
            addGlyph(plan.clef->id, Part::Clef, clefGlyph(*plan.clef, font),
                     { left + plan.clefX, clefY(*plan.clef, lines) });

        if (plan.key)
        {
            const Clef keyClef = clefAt(plan, 0);
            const std::string glyph = plan.key->accidentals > 0 ? "accidentalSharp" : "accidentalFlat";
            double x = left + plan.keyX;
            for (const double y : keySignatureYs(plan.key->accidentals, keyClef, lines))
            {
                addGlyph(plan.key->id, Part::KeySignature, glyph, { x, y });
                x += glyphRight(glyph) + in.header.keyAccidentalGapSp;
            }
        }

        if (plan.time)
        {
            const double width = timeWidth(*plan.time);
            const double middle = middleLineY(lines);
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

    const KitEntry* kitEntry(const NodeId& id) const
    {
        if (!track.kit)
            return nullptr;
        for (const KitEntry& entry : *track.kit)
            if (entry.id == id)
                return &entry;
        return nullptr;
    }

    // A chord around x = 0: ledger lines, noteheads (seconds beside each other), stem,
    // flag, dots and accidentals. `marks` has one entry per note of the chord.
    EventShape chordShape(const PlacedEvent& event, const Clef& noteClef, bool severalVoices,
                          const std::vector<std::optional<AccidentalMark>>& marks) const
    {
        const Chord& chord = *event.chord;
        const DurationType type = chord.duration.type;
        const double middle = middleLineY(lines);
        const double thickness = engraving.stemThickness;
        EventShape shape;

        std::vector<Head> heads;
        for (std::size_t i = 0; i < chord.notes.size(); ++i)
            std::visit(Overloaded {
                           [&](const Note& note) {
                               heads.push_back({ &note.id, noteheadGlyph(type), pitchY(note.pitch, noteClef, lines) });
                               heads.back().accidental = i < marks.size() ? marks[i] : std::nullopt;
                           },
                           [&](const UnpitchedNote& note) {
                               const KitEntry* entry = kitEntry(note.kitEntryId);
                               heads.push_back(
                                   { &note.id, kitNoteheadGlyph(entry ? entry->notehead : Notehead::Normal, type, font),
                                     unpitchedY(entry ? entry->displayPitch : 0, lines) });
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
        const bool up =
            severalVoices ? event.voice % 2 == 0 : (bottomY - middle) > (middle - topY); // most of it below the middle

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
            const double tip = up ? std::min(topY - stemLengthSp, middle) : std::max(bottomY + stemLengthSp, middle);
            double end = tip;

            const int flags = type <= DurationType::Eighth ? -static_cast<int>(type) : 0;
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

        // Dots: in the space of each note, the one above for a note on a line (below, for
        // the lower voice). Two notes wanting the same space share one dot.
        if (chord.duration.dots > 0)
        {
            const bool lowerVoice = severalVoices && !up;
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
    EventShape restShape(const PlacedEvent& event, bool severalVoices) const
    {
        const Rest& rest = *event.rest;
        const bool fullMeasure = rest.fullMeasure.value_or(false);
        const std::string glyph = fullMeasure ? "restWhole" : restGlyph(rest.duration.type);

        // SMuFL rests hang or sit on the line at their origin: a whole rest hangs from
        // the line above the middle, the others centre on the middle line.
        double y = middleLineY(lines);
        if (glyph == "restWhole")
            y -= 1;
        if (severalVoices)
            y += event.voice % 2 == 0 ? -1 : 1;

        EventShape shape;
        addItem(shape, rest.id, Part::Rest, GlyphShape { glyph, { 0, y } });
        if (!fullMeasure && rest.duration.dots > 0)
            addDots(shape, { { &rest.id, onLine(y) ? y - 0.5 : y } }, glyphRight(glyph) + in.notes.dotGapSp,
                    rest.duration.dots);
        return shape;
    }

    void emitBarline(const Measure& measure, double right)
    {
        const double top = lines > 1 ? 0.0 : -1.0;
        const double bottom = lines > 1 ? lines - 1.0 : 1.0;
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
    const Track& track;
    const Staff& staff;
    const SmuflFont& font;
    const EngravingDefaults& engraving;
    const int lines;
    const int tpq;
    std::map<std::string_view, const StaffMeasure*> staffMeasures;
    std::set<NodeId, std::less<>> tieTargets;

    // In force where planning has got to.
    std::optional<TimeSignature> timeSig;
    std::optional<KeySignature> key;
    Clef clef { .id = {}, .type = ClefType::G, .line = 2, .octaveChange = 0 };

    std::vector<MeasurePlan> plans;
    double stretch = 1; // > 1: every space grows by this factor
    double squeeze = 1; // < 1: every space sits this share of the way from its minimum to its natural size
    SystemLayout out;
};

} // namespace

SystemLayout layoutSystem(const SystemInput& input)
{
    const std::size_t measureTotal = input.music.measures.size();
    if (input.measureCount == 0 || input.firstMeasure >= measureTotal
        || input.measureCount > measureTotal - input.firstMeasure)
        throw std::invalid_argument("measures " + std::to_string(input.firstMeasure) + " + "
                                    + std::to_string(input.measureCount) + " are outside the score ("
                                    + std::to_string(measureTotal) + " measures)");

    for (const Track& track : input.music.tracks)
        for (const Staff& staff : track.staves)
            if (staff.id == input.staffId)
                return SystemBuilder(input, track, staff).build();

    throw std::invalid_argument("no staff with id \"" + input.staffId + "\"");
}

} // namespace msd
