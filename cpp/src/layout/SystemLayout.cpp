#include "layout/SystemLayout.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string_view>

#include "layout/StaffPosition.h"

// How this works
// ──────────────
// 1. Plan: walk the measures before the system to know the clef, key and time
//    signature in force, then for each measure in the system find its events, the
//    ticks where something starts (onsets), and the natural space each onset gets.
// 2. Justify: the fixed parts (header, padding) keep their width; the space between
//    onsets is stretched or squeezed by one factor so the system fills `width`.
// 3. Emit: turn every staff line, signature, notehead, stem, rest and barline into a
//    LayoutBox at its final position.

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

// A chord or rest with the tick it starts at, from the start of its measure.
struct PlacedEvent
{
    const Chord* chord = nullptr; // exactly one of chord and rest is set
    const Rest* rest = nullptr;
    int voice = 0;
    Tick tick = 0;
};

// Places events one after another from `start`. Inside tuplets, lengths are scaled by
// num/den (2/3 for a triplet). Returns the events' unscaled total length.
Tick placeEvents(const std::vector<VoiceEvent>& events, Tick start, Tick num, Tick den, int tpq,
                 int voice, std::vector<PlacedEvent>& out)
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
                           const Tick inner = placeEvents(tuplet.events, at, num * normal, den * actual, tpq, voice, out);
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
        return firstGlyph(font, { whole ? "noteheadXWhole" : half ? "noteheadXHalf" : "noteheadXBlack", "noteheadXBlack", fallback });
    case Notehead::CircleX: return firstGlyph(font, { "noteheadCircleX", fallback });
    case Notehead::Triangle:
        return firstGlyph(font, { whole ? "noteheadTriangleUpWhole" : half ? "noteheadTriangleUpHalf" : "noteheadTriangleUpBlack",
                                  "noteheadTriangleUpBlack", fallback });
    case Notehead::Diamond:
        return firstGlyph(font, { whole ? "noteheadDiamondWhole" : half ? "noteheadDiamondHalf" : "noteheadDiamondBlack",
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
    const char* suffix = clef.octaveChange == -1 ? "8vb"
                         : clef.octaveChange == 1 ? "8va"
                         : clef.octaveChange == -2 ? "15mb"
                         : clef.octaveChange == 2 ? "15ma"
                                                  : "";
    return firstGlyph(font, { base + suffix, base });
}

std::string timeSigDigits(int value, const char* fallback)
{
    return value > 0 ? std::to_string(value) : fallback;
}

// ── Plans ──────────────────────────────────────────────────────────────────────

struct Onset
{
    Tick tick = 0;
    double spring = 0;                // natural space from here to the next onset
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
            staffMeasures.emplace(staffMeasure.measureId, &staffMeasure);
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
        double springs = 0;
        for (const MeasurePlan& plan : plans)
        {
            fixed += plan.lead;
            for (const Onset& onset : plan.onsets)
                springs += onset.spring;
        }
        double factor = 1;
        if (in.width && springs > 0)
            factor = std::max(0.1, (*in.width - fixed) / springs); // never fold notes onto each other

        out.staffId = staff.id;
        out.staffLines = lines;
        out.width = fixed + springs * factor;

        emitStaffLines();
        double x = 0;
        for (const MeasurePlan& plan : plans)
            x = emitMeasure(plan, x, factor);
        return std::move(out);
    }

private:
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

    // The widest glyph an event puts at its onset.
    double eventWidth(const PlacedEvent& event) const
    {
        if (event.rest)
            return glyphRight(restGlyph(event.rest->duration.type));
        return glyphRight(noteheadGlyph(event.chord->duration.type));
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
                && (!previousTime || previousTime->beats != measure.timeSig->beats || previousTime->unit != measure.timeSig->unit))
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

        std::map<Tick, Onset> onsets;
        for (std::size_t i = 0; i < plan.events.size(); ++i)
        {
            const PlacedEvent& event = plan.events[i];
            const Tick tick = event.rest && event.rest->fullMeasure.value_or(false) ? 0 : event.tick;
            Onset& onset = onsets[tick];
            onset.tick = tick;
            onset.events.push_back(i);
        }
        if (onsets.empty())
            onsets[0].tick = 0; // an empty measure still takes its length's room

        for (auto it = onsets.begin(); it != onsets.end(); ++it)
        {
            const auto next = std::next(it);
            const Tick end = next == onsets.end() ? std::max(plan.length, it->first) : next->first;
            Onset& onset = it->second;
            double widest = 0;
            for (const std::size_t i : onset.events)
                widest = std::max(widest, eventWidth(plan.events[i]));
            onset.spring = std::max(springFor(end - it->first), widest + in.profile.padding.collisionSp);
            plan.onsets.push_back(onset);
        }

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
        const double half = thickness / 2;
        const bool horizontal = std::abs(to.x - from.x) >= std::abs(to.y - from.y);
        const Rect bounds { std::min(from.x, to.x) - (horizontal ? 0 : half), std::min(from.y, to.y) - (horizontal ? half : 0),
                            std::max(from.x, to.x) + (horizontal ? 0 : half), std::max(from.y, to.y) + (horizontal ? half : 0) };
        out.boxes.push_back({ nodeId, part, bounds, LineShape { from, to, thickness } });
    }

    void emitStaffLines()
    {
        for (int i = 0; i < lines; ++i)
            addLine(staff.id, Part::StaffLine, { 0, double(i) }, { out.width, double(i) }, engraving.staffLineThickness);
    }

    double emitMeasure(const MeasurePlan& plan, double left, double factor)
    {
        emitSignatures(plan, left);

        // Where each onset goes, and the barline.
        std::vector<double> onsetX;
        double x = left + plan.lead;
        for (const Onset& onset : plan.onsets)
        {
            onsetX.push_back(x);
            x += onset.spring * factor;
        }
        const double right = x;
        const double contentLeft = left + plan.lead;

        for (std::size_t i = 0; i < plan.onsets.size(); ++i)
            for (const std::size_t e : plan.onsets[i].events)
            {
                const PlacedEvent& event = plan.events[e];
                if (event.chord)
                    emitChord(plan, event, onsetX[i]);
                else
                    emitRest(plan, event, onsetX[i], contentLeft, right);
            }

        emitBarline(*plan.measure, right);
        out.measures.push_back({ plan.measure->id, left, right });
        return right;
    }

    void emitSignatures(const MeasurePlan& plan, double left)
    {
        if (plan.clef)
            addGlyph(plan.clef->id, Part::Clef, clefGlyph(*plan.clef, font), { left + plan.clefX, clefY(*plan.clef, lines) });

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
    };

    const KitEntry* kitEntry(const NodeId& id) const
    {
        if (!track.kit)
            return nullptr;
        for (const KitEntry& entry : *track.kit)
            if (entry.id == id)
                return &entry;
        return nullptr;
    }

    void emitChord(const MeasurePlan& plan, const PlacedEvent& event, double x)
    {
        const Chord& chord = *event.chord;
        const Clef noteClef = clefAt(plan, event.tick);
        const DurationType type = chord.duration.type;

        std::vector<Head> heads;
        for (const ChordNote& chordNote : chord.notes)
            std::visit(Overloaded {
                           [&](const Note& note) {
                               heads.push_back({ &note.id, noteheadGlyph(type), pitchY(note.pitch, noteClef, lines) });
                           },
                           [&](const UnpitchedNote& note) {
                               const KitEntry* entry = kitEntry(note.kitEntryId);
                               heads.push_back({ &note.id,
                                                 kitNoteheadGlyph(entry ? entry->notehead : Notehead::Normal, type, font),
                                                 unpitchedY(entry ? entry->displayPitch : 0, lines) });
                           },
                       },
                       chordNote);
        if (heads.empty())
            return;

        const auto [topIt, bottomIt] = std::minmax_element(heads.begin(), heads.end(),
                                                           [](const Head& a, const Head& b) { return a.y < b.y; });
        const Head& top = *topIt;
        const Head& bottom = *bottomIt;

        // Ledger lines first, so noteheads paint over them.
        double headWidth = 0;
        for (const Head& head : heads)
            headWidth = std::max(headWidth, glyphRight(head.glyph));
        const double extension = engraving.legerLineExtension;
        auto ledger = [&](double y) {
            addLine(chord.id, Part::LedgerLine, { x - extension, y }, { x + headWidth + extension, y },
                    engraving.legerLineThickness);
        };
        for (double y = -1; y >= top.y - 1e-9; y -= 1)
            ledger(y);
        for (double y = lines; y <= bottom.y + 1e-9; y += 1)
            ledger(y);

        for (const Head& head : heads)
            addGlyph(*head.nodeId, Part::Notehead, head.glyph, { x, head.y });

        if (type > DurationType::Half)
            return; // whole notes and longer have no stem

        const double middle = middleLineY(lines);
        bool up = false;
        if (plan.severalVoices)
            up = event.voice % 2 == 0;
        else
            up = (bottom.y - middle) > (middle - top.y); // most of the chord is below the middle

        const double thickness = engraving.stemThickness;
        if (up)
        {
            const Rect box = font.bbox(bottom.glyph).value_or(Rect {});
            const Point anchor = font.anchor(bottom.glyph, "stemUpSE").value_or(Point { box.right, 0 });
            const double stemX = x + anchor.x - thickness / 2;
            const double tip = std::min(top.y - stemLengthSp, middle);
            addLine(chord.id, Part::Stem, { stemX, bottom.y + anchor.y }, { stemX, tip }, thickness);
        }
        else
        {
            const Rect box = font.bbox(top.glyph).value_or(Rect {});
            const Point anchor = font.anchor(top.glyph, "stemDownNW").value_or(Point { box.left, 0 });
            const double stemX = x + anchor.x + thickness / 2;
            const double tip = std::max(bottom.y + stemLengthSp, middle);
            addLine(chord.id, Part::Stem, { stemX, top.y + anchor.y }, { stemX, tip }, thickness);
        }
    }

    void emitRest(const MeasurePlan& plan, const PlacedEvent& event, double x, double contentLeft, double right)
    {
        const Rest& rest = *event.rest;
        const bool fullMeasure = rest.fullMeasure.value_or(false);
        const std::string glyph = fullMeasure ? "restWhole" : restGlyph(rest.duration.type);

        // SMuFL rests hang or sit on the line at their origin: a whole rest hangs from
        // the line above the middle, the others centre on the middle line.
        double y = middleLineY(lines);
        if (glyph == "restWhole")
            y -= 1;
        if (plan.severalVoices)
            y += event.voice % 2 == 0 ? -1 : 1;

        if (fullMeasure)
        {
            const Rect box = font.bbox(glyph).value_or(Rect {});
            x = (contentLeft + right) / 2 - (box.left + box.right) / 2;
        }
        addGlyph(rest.id, Part::Rest, glyph, { x, y });
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

    // In force where planning has got to.
    std::optional<TimeSignature> timeSig;
    std::optional<KeySignature> key;
    Clef clef { .id = {}, .type = ClefType::G, .line = 2, .octaveChange = 0 };

    std::vector<MeasurePlan> plans;
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
