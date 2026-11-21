#include "core/json/Load.h"

#include <cstdint>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/json/EnumNames.h"

// How this file works
// ───────────────────
// There is one `read(json, path, out)` function per C++ type. Each one checks the JSON
// value and fills `out`; structs call `field(...)` for each member, which looks the
// member up and calls `read` for its type. C++ picks the right `read` from the type of
// `out` (overloading), so adding a struct means adding one `read` for it here.
//
// Variants are read by looking at the JSON's "kind" (or "type") and filling the
// matching alternative with `emplace<T>()`.

namespace msd
{
namespace
{

using Json = nlohmann::json;

// ── Where we are, for error messages ───────────────────────────────────────────
// Each level of the document gets a Path on the stack that points to its parent.
// It costs almost nothing, and only becomes text when an error is thrown.

struct Path
{
    const Path* parent = nullptr; // nullptr: the root
    std::string_view key;         // an object member, or empty for an array element
    std::size_t index = 0;        // the array index when `key` is empty

    std::string str() const
    {
        if (parent == nullptr)
            return {};
        std::string text = parent->str();
        if (!key.empty())
            return text.empty() ? std::string(key) : text + "." + std::string(key);
        return text + "[" + std::to_string(index) + "]";
    }
};

Path at(const Path& parent, std::string_view key) { return { &parent, key, 0 }; }
Path at(const Path& parent, std::size_t index) { return { &parent, {}, index }; }

[[noreturn]] void fail(const Path& path, const std::string& message)
{
    const std::string where = path.str();
    throw LoadError((where.empty() ? std::string("(root)") : where) + ": " + message);
}

void expectObject(const Json& j, const Path& p)
{
    if (!j.is_object())
        fail(p, std::string("expected an object, got ") + j.type_name());
}

// Reads a string member that tells variants apart ("kind" or "type").
std::string_view tagOf(const Json& j, const Path& p, const char* tag)
{
    expectObject(j, p);
    const auto it = j.find(tag);
    if (it == j.end() || !it->is_string())
        fail(p, std::string("missing string field \"") + tag + "\"");
    return it->get_ref<const std::string&>();
}

void expectTag(const Json& j, const Path& p, const char* tag, std::string_view expected)
{
    const std::string_view actual = tagOf(j, p, tag);
    if (actual != expected)
        fail(p, std::string(tag) + " is \"" + std::string(actual) + "\", expected \"" + std::string(expected) + "\"");
}

void expectKind(const Json& j, const Path& p, std::string_view kind) { expectTag(j, p, "kind", kind); }

// ── Declarations ───────────────────────────────────────────────────────────────
// Every read() is declared first, so the templates below and the struct readers can
// call each other in any order.

void read(const Json&, const Path&, bool&);
void read(const Json&, const Path&, int&);
void read(const Json&, const Path&, std::int64_t&);
void read(const Json&, const Path&, double&);
void read(const Json&, const Path&, std::string&);
void read(const Json&, const Path&, DurationType&);
template <NamedEnum E>
void read(const Json&, const Path&, E&);
template <class T>
void read(const Json&, const Path&, std::vector<T>&);
void read(const Json&, const Path&, std::map<NodeId, double>&);
void read(const Json&, const Path&, std::map<PlacedKind, Side>&);

void read(const Json&, const Path&, Duration&);
void read(const Json&, const Path&, Pitch&);
void read(const Json&, const Path&, ElementAnchor&);
void read(const Json&, const Path&, TickAnchor&);
void read(const Json&, const Path&, Anchor&);
void read(const Json&, const Path&, TimeSignature&);
void read(const Json&, const Path&, KeySignature&);
void read(const Json&, const Path&, Tempo&);
void read(const Json&, const Path&, Clef&);
void read(const Json&, const Path&, Marker&);
void read(const Json&, const Path&, JumpTarget&);
void read(const Json&, const Path&, Jump&);
void read(const Json&, const Path&, RepeatEnd&);
void read(const Json&, const Path&, Measure&);
void read(const Json&, const Path&, TieTarget&);
void read(const Json&, const Path&, Tie&);
void read(const Json&, const Path&, Note&);
void read(const Json&, const Path&, UnpitchedNote&);
void read(const Json&, const Path&, ChordNote&);
void read(const Json&, const Path&, Articulation&);
void read(const Json&, const Path&, GraceChord&);
void read(const Json&, const Path&, GraceChords&);
void read(const Json&, const Path&, Chord&);
void read(const Json&, const Path&, Rest&);
void read(const Json&, const Path&, Space&);
void read(const Json&, const Path&, Tuplet&);
void read(const Json&, const Path&, VoiceEvent&);
void read(const Json&, const Path&, Voice&);
void read(const Json&, const Path&, ClefChange&);
void read(const Json&, const Path&, StaffMeasure&);
void read(const Json&, const Path&, Slur&);
void read(const Json&, const Path&, Hairpin&);
void read(const Json&, const Path&, Ottava&);
void read(const Json&, const Path&, Pedal&);
void read(const Json&, const Path&, CustomSpanner&);
void read(const Json&, const Path&, TrackSpanner&);
void read(const Json&, const Path&, Volta&);
void read(const Json&, const Path&, TempoChange&);
void read(const Json&, const Path&, GlobalSpanner&);
void read(const Json&, const Path&, Dynamic&);
void read(const Json&, const Path&, TechniqueMarking&);
void read(const Json&, const Path&, TextMarking&);
void read(const Json&, const Path&, Attachment&);
void read(const Json&, const Path&, KitEntry&);
void read(const Json&, const Path&, Staff&);
void read(const Json&, const Path&, Transposition&);
void read(const Json&, const Path&, Track&);
void read(const Json&, const Path&, MusicData&);

void read(const Json&, const Path&, LayoutProfile::Margins&);
void read(const Json&, const Path&, LayoutProfile::Page&);
void read(const Json&, const Path&, LayoutProfile::Spacing&);
void read(const Json&, const Path&, LayoutProfile::Distances&);
void read(const Json&, const Path&, LayoutProfile::Breaking&);
void read(const Json&, const Path&, LayoutProfile::Fonts&);
void read(const Json&, const Path&, LayoutProfile::Padding&);
void read(const Json&, const Path&, LayoutProfile&);
void read(const Json&, const Path&, FlowSystem&);
void read(const Json&, const Path&, FlowPage&);
void read(const Json&, const Path&, Flow&);
void read(const Json&, const Path&, LayoutElement&);
void read(const Json&, const Path&, Layout&);
void read(const Json&, const Path&, LayoutSetup&);

// ── Fields ─────────────────────────────────────────────────────────────────────

// A required member.
template <class T>
void field(const Json& obj, const Path& p, const char* key, T& out)
{
    const auto it = obj.find(key);
    if (it == obj.end())
        fail(p, std::string("missing field \"") + key + "\"");
    read(*it, at(p, key), out);
}

// An optional member (`key?: T` in TS). Absent or null leaves it empty.
template <class T>
void field(const Json& obj, const Path& p, const char* key, std::optional<T>& out)
{
    const auto it = obj.find(key);
    if (it == obj.end() || it->is_null())
    {
        out.reset();
        return;
    }
    read(*it, at(p, key), out.emplace());
}

// Fills alternative T of a variant.
template <class T, class Variant>
void readAs(const Json& j, const Path& p, Variant& out)
{
    read(j, p, out.template emplace<T>());
}

// ── Plain values ───────────────────────────────────────────────────────────────

void read(const Json& j, const Path& p, bool& out)
{
    if (!j.is_boolean())
        fail(p, std::string("expected a boolean, got ") + j.type_name());
    out = j.get<bool>();
}

void read(const Json& j, const Path& p, std::int64_t& out)
{
    if (!j.is_number_integer())
        fail(p, std::string("expected an integer, got ") + (j.is_number() ? j.dump() : j.type_name()));
    out = j.is_number_unsigned() ? static_cast<std::int64_t>(j.get<std::uint64_t>()) : j.get<std::int64_t>();
}

void read(const Json& j, const Path& p, int& out)
{
    std::int64_t wide = 0;
    read(j, p, wide);
    if (wide < std::numeric_limits<int>::min() || wide > std::numeric_limits<int>::max())
        fail(p, "integer out of range: " + std::to_string(wide));
    out = static_cast<int>(wide);
}

void read(const Json& j, const Path& p, double& out)
{
    if (!j.is_number())
        fail(p, std::string("expected a number, got ") + j.type_name());
    out = j.get<double>();
}

void read(const Json& j, const Path& p, std::string& out)
{
    if (!j.is_string())
        fail(p, std::string("expected a string, got ") + j.type_name());
    out = j.get_ref<const std::string&>();
}

void read(const Json& j, const Path& p, DurationType& out)
{
    int value = 0;
    read(j, p, value);
    if (value < static_cast<int>(DurationType::OneHundredTwentyEighth) || value > static_cast<int>(DurationType::Longa))
        fail(p, "duration type out of range: " + std::to_string(value));
    out = static_cast<DurationType>(value);
}

template <NamedEnum E>
void read(const Json& j, const Path& p, E& out)
{
    std::string name;
    read(j, p, name);
    const std::optional<E> value = enumFromName<E>(name);
    if (!value)
        fail(p, "unknown name \"" + name + "\"");
    out = *value;
}

template <class T>
void read(const Json& j, const Path& p, std::vector<T>& out)
{
    if (!j.is_array())
        fail(p, std::string("expected an array, got ") + j.type_name());
    out.clear();
    out.resize(j.size());
    for (std::size_t i = 0; i < j.size(); ++i)
        read(j[i], at(p, i), out[i]);
}

void read(const Json& j, const Path& p, std::map<NodeId, double>& out)
{
    expectObject(j, p);
    out.clear();
    for (const auto& [key, value] : j.items())
        read(value, at(p, key), out[key]);
}

void read(const Json& j, const Path& p, std::map<PlacedKind, Side>& out)
{
    expectObject(j, p);
    out.clear();
    for (const auto& [key, value] : j.items())
    {
        const Path here = at(p, key);
        const std::optional<PlacedKind> kind = enumFromName<PlacedKind>(key);
        if (!kind)
            fail(here, "unknown placed kind");
        read(value, here, out[*kind]);
    }
}

// ── Small values and anchors ───────────────────────────────────────────────────

void read(const Json& j, const Path& p, Duration& out)
{
    expectObject(j, p);
    field(j, p, "type", out.type);
    field(j, p, "dots", out.dots);
}

void read(const Json& j, const Path& p, Pitch& out)
{
    expectObject(j, p);
    field(j, p, "step", out.step);
    field(j, p, "alter", out.alter);
    field(j, p, "octave", out.octave);
}

void read(const Json& j, const Path& p, ElementAnchor& out)
{
    expectTag(j, p, "type", "element");
    field(j, p, "id", out.id);
}

void read(const Json& j, const Path& p, TickAnchor& out)
{
    expectTag(j, p, "type", "tick");
    field(j, p, "measureId", out.measureId);
    field(j, p, "tick", out.tick);
}

void read(const Json& j, const Path& p, Anchor& out)
{
    const std::string_view type = tagOf(j, p, "type");
    if (type == "element")
        readAs<ElementAnchor>(j, p, out);
    else if (type == "tick")
        readAs<TickAnchor>(j, p, out);
    else
        fail(p, "unknown anchor type \"" + std::string(type) + "\"");
}

// ── Measure-level items ────────────────────────────────────────────────────────

void read(const Json& j, const Path& p, TimeSignature& out)
{
    expectKind(j, p, "time-signature");
    field(j, p, "id", out.id);
    field(j, p, "beats", out.beats);
    field(j, p, "unit", out.unit);
}

void read(const Json& j, const Path& p, KeySignature& out)
{
    expectKind(j, p, "key-signature");
    field(j, p, "id", out.id);
    field(j, p, "accidentals", out.accidentals);
}

void read(const Json& j, const Path& p, Tempo& out)
{
    expectKind(j, p, "tempo");
    field(j, p, "id", out.id);
    field(j, p, "unit", out.unit);
    field(j, p, "bpm", out.bpm);
    field(j, p, "tick", out.tick);
    field(j, p, "text", out.text);
}

void read(const Json& j, const Path& p, Clef& out)
{
    expectKind(j, p, "clef");
    field(j, p, "id", out.id);
    field(j, p, "type", out.type);
    field(j, p, "line", out.line);
    field(j, p, "octaveChange", out.octaveChange);
}

void read(const Json& j, const Path& p, Marker& out)
{
    expectKind(j, p, "marker");
    field(j, p, "id", out.id);
    field(j, p, "type", out.type);
    field(j, p, "label", out.label);
}

void read(const Json& j, const Path& p, JumpTarget& out)
{
    const std::string_view type = tagOf(j, p, "type");
    if (type == "start")
        out.emplace<JumpToStart>();
    else if (type == "marker")
        field(j, p, "id", out.emplace<JumpToMarker>().id);
    else
        fail(p, "unknown jump target \"" + std::string(type) + "\"");
}

void read(const Json& j, const Path& p, Jump& out)
{
    expectKind(j, p, "jump");
    field(j, p, "id", out.id);
    field(j, p, "to", out.to);
    field(j, p, "until", out.until);
    field(j, p, "then", out.then);
    field(j, p, "playRepeats", out.playRepeats);
    field(j, p, "displayText", out.displayText);
}

void read(const Json& j, const Path& p, RepeatEnd& out)
{
    expectObject(j, p);
    field(j, p, "times", out.times);
}

void read(const Json& j, const Path& p, Measure& out)
{
    expectKind(j, p, "measure");
    field(j, p, "id", out.id);
    field(j, p, "timeSig", out.timeSig);
    field(j, p, "key", out.key);
    field(j, p, "tempos", out.tempos);
    field(j, p, "actualTicks", out.actualTicks);
    field(j, p, "repeatStart", out.repeatStart);
    field(j, p, "repeatEnd", out.repeatEnd);
    field(j, p, "barline", out.barline);
    field(j, p, "markers", out.markers);
    field(j, p, "jumps", out.jumps);
}

// ── Notes and chords ───────────────────────────────────────────────────────────

void read(const Json& j, const Path& p, TieTarget& out)
{
    expectObject(j, p);
    field(j, p, "to", out.to);
}

void read(const Json& j, const Path& p, Tie& out)
{
    if (j.is_array())
    {
        read(j, p, out.emplace<std::vector<TieTarget>>());
        return;
    }
    expectObject(j, p);
    bool letRing = false;
    field(j, p, "letRing", letRing);
    if (!letRing)
        fail(p, "letRing must be true");
    out.emplace<LetRing>();
}

void read(const Json& j, const Path& p, Note& out)
{
    expectKind(j, p, "note");
    field(j, p, "id", out.id);
    field(j, p, "pitch", out.pitch);
    field(j, p, "tie", out.tie);
    field(j, p, "accidentalDisplay", out.accidentalDisplay);
}

void read(const Json& j, const Path& p, UnpitchedNote& out)
{
    expectKind(j, p, "unpitched-note");
    field(j, p, "id", out.id);
    field(j, p, "kitEntryId", out.kitEntryId);
    field(j, p, "tie", out.tie);
}

void read(const Json& j, const Path& p, ChordNote& out)
{
    const std::string_view kind = tagOf(j, p, "kind");
    if (kind == "note")
        readAs<Note>(j, p, out);
    else if (kind == "unpitched-note")
        readAs<UnpitchedNote>(j, p, out);
    else
        fail(p, "unknown kind \"" + std::string(kind) + "\" in a chord");
}

void read(const Json& j, const Path& p, Articulation& out)
{
    expectKind(j, p, "articulation");
    field(j, p, "id", out.id);
    field(j, p, "type", out.type);
}

void read(const Json& j, const Path& p, GraceChord& out)
{
    expectKind(j, p, "chord");
    if (j.contains("graceNotes"))
        fail(p, "a grace chord can't have grace notes of its own");
    field(j, p, "id", out.id);
    field(j, p, "duration", out.duration);
    field(j, p, "notes", out.notes);
    field(j, p, "articulations", out.articulations);
    field(j, p, "beam", out.beam);
}

void read(const Json& j, const Path& p, GraceChords& out)
{
    expectKind(j, p, "grace-chords");
    field(j, p, "id", out.id);
    field(j, p, "slash", out.slash);
    field(j, p, "chords", out.chords);
    field(j, p, "placement", out.placement);
}

void read(const Json& j, const Path& p, Chord& out)
{
    expectKind(j, p, "chord");
    field(j, p, "id", out.id);
    field(j, p, "duration", out.duration);
    field(j, p, "notes", out.notes);
    field(j, p, "articulations", out.articulations);
    field(j, p, "graceNotes", out.graceNotes);
    field(j, p, "beam", out.beam);
}

void read(const Json& j, const Path& p, Rest& out)
{
    expectKind(j, p, "rest");
    field(j, p, "id", out.id);
    field(j, p, "duration", out.duration);
    field(j, p, "fullMeasure", out.fullMeasure);
    field(j, p, "beam", out.beam);
}

void read(const Json& j, const Path& p, Space& out)
{
    expectKind(j, p, "space");
    field(j, p, "id", out.id);
    field(j, p, "duration", out.duration);
}

void read(const Json& j, const Path& p, Tuplet& out)
{
    expectKind(j, p, "tuplet");
    field(j, p, "id", out.id);
    field(j, p, "unit", out.unit);
    field(j, p, "normal", out.normal);
    field(j, p, "actual", out.actual);
    field(j, p, "events", out.events);
}

void read(const Json& j, const Path& p, VoiceEvent& out)
{
    const std::string_view kind = tagOf(j, p, "kind");
    if (kind == "chord")
        readAs<Chord>(j, p, out);
    else if (kind == "rest")
        readAs<Rest>(j, p, out);
    else if (kind == "space")
        readAs<Space>(j, p, out);
    else if (kind == "tuplet")
        readAs<Tuplet>(j, p, out);
    else
        fail(p, "unknown kind \"" + std::string(kind) + "\" in a voice");
}

void read(const Json& j, const Path& p, Voice& out)
{
    expectKind(j, p, "voice");
    field(j, p, "id", out.id);
    field(j, p, "index", out.index);
    field(j, p, "events", out.events);
}

void read(const Json& j, const Path& p, ClefChange& out)
{
    expectObject(j, p);
    field(j, p, "tick", out.tick);
    field(j, p, "clef", out.clef);
}

void read(const Json& j, const Path& p, StaffMeasure& out)
{
    expectKind(j, p, "staff-measure");
    field(j, p, "id", out.id);
    field(j, p, "measureId", out.measureId);
    field(j, p, "clefs", out.clefs);
    field(j, p, "voices", out.voices);
}

// ── Spanners ───────────────────────────────────────────────────────────────────

void read(const Json& j, const Path& p, Slur& out)
{
    expectKind(j, p, "slur");
    field(j, p, "id", out.id);
    field(j, p, "start", out.start);
    field(j, p, "end", out.end);
}

void read(const Json& j, const Path& p, Hairpin& out)
{
    expectKind(j, p, "hairpin");
    field(j, p, "id", out.id);
    field(j, p, "start", out.start);
    field(j, p, "end", out.end);
    field(j, p, "includeEnd", out.includeEnd);
    field(j, p, "staffIds", out.staffIds);
    field(j, p, "type", out.type);
    field(j, p, "niente", out.niente);
}

void read(const Json& j, const Path& p, Ottava& out)
{
    expectKind(j, p, "ottava");
    field(j, p, "id", out.id);
    field(j, p, "start", out.start);
    field(j, p, "end", out.end);
    field(j, p, "shift", out.shift);
    if (out.shift != -2 && out.shift != -1 && out.shift != 1 && out.shift != 2)
        fail(at(p, "shift"), "must be -2, -1, 1 or 2");
}

void read(const Json& j, const Path& p, Pedal& out)
{
    expectKind(j, p, "pedal");
    field(j, p, "id", out.id);
    field(j, p, "type", out.type);
    field(j, p, "start", out.start);
    field(j, p, "end", out.end);
    field(j, p, "includeEnd", out.includeEnd);
}

void read(const Json& j, const Path& p, CustomSpanner& out)
{
    expectKind(j, p, "custom-spanner");
    field(j, p, "id", out.id);
    field(j, p, "staffIds", out.staffIds);
    field(j, p, "voiceIndexes", out.voiceIndexes);
    field(j, p, "start", out.start);
    field(j, p, "end", out.end);
    field(j, p, "includeEnd", out.includeEnd);
    field(j, p, "techniqueId", out.techniqueId);
    field(j, p, "text", out.text);
    field(j, p, "passes", out.passes);
}

void read(const Json& j, const Path& p, TrackSpanner& out)
{
    const std::string_view kind = tagOf(j, p, "kind");
    if (kind == "slur")
        readAs<Slur>(j, p, out);
    else if (kind == "hairpin")
        readAs<Hairpin>(j, p, out);
    else if (kind == "pedal")
        readAs<Pedal>(j, p, out);
    else if (kind == "custom-spanner")
        readAs<CustomSpanner>(j, p, out);
    else
        fail(p, "unknown track spanner kind \"" + std::string(kind) + "\"");
}

void read(const Json& j, const Path& p, Volta& out)
{
    expectKind(j, p, "volta");
    field(j, p, "id", out.id);
    field(j, p, "startMeasureId", out.startMeasureId);
    field(j, p, "endMeasureId", out.endMeasureId);
    field(j, p, "passes", out.passes);
    field(j, p, "openEnd", out.openEnd);
}

void read(const Json& j, const Path& p, TempoChange& out)
{
    expectKind(j, p, "tempo-change");
    field(j, p, "id", out.id);
    field(j, p, "start", out.start);
    field(j, p, "end", out.end);
    field(j, p, "type", out.type);
    field(j, p, "targetTempo", out.targetTempo);
}

void read(const Json& j, const Path& p, GlobalSpanner& out)
{
    const std::string_view kind = tagOf(j, p, "kind");
    if (kind == "volta")
        readAs<Volta>(j, p, out);
    else if (kind == "tempo-change")
        readAs<TempoChange>(j, p, out);
    else
        fail(p, "unknown global spanner kind \"" + std::string(kind) + "\"");
}

// ── Dynamics and text ──────────────────────────────────────────────────────────

// The fields TS shares through AttachmentBase.
template <class A>
void readAttachmentBase(const Json& j, const Path& p, A& out)
{
    field(j, p, "id", out.id);
    field(j, p, "staffIds", out.staffIds);
    field(j, p, "voiceIndexes", out.voiceIndexes);
    field(j, p, "beforeTick", out.beforeTick);
}

void read(const Json& j, const Path& p, Dynamic& out)
{
    expectKind(j, p, "dynamic");
    readAttachmentBase(j, p, out);
    field(j, p, "at", out.at);
    field(j, p, "type", out.type);
    field(j, p, "text", out.text);
}

void read(const Json& j, const Path& p, TechniqueMarking& out)
{
    expectKind(j, p, "technique");
    readAttachmentBase(j, p, out);
    field(j, p, "techniqueId", out.techniqueId);
    field(j, p, "text", out.text);
    field(j, p, "passes", out.passes);
    field(j, p, "at", out.at);
    field(j, p, "scope", out.scope);
    if (std::holds_alternative<TickAnchor>(out.at) && out.scope != TechniqueScope::UntilNext)
        fail(at(p, "scope"), "a technique at a tick must have scope \"until-next\"");
}

void read(const Json& j, const Path& p, TextMarking& out)
{
    expectKind(j, p, "text");
    readAttachmentBase(j, p, out);
    field(j, p, "text", out.text);
    field(j, p, "at", out.at);
}

void read(const Json& j, const Path& p, Attachment& out)
{
    const std::string_view kind = tagOf(j, p, "kind");
    if (kind == "dynamic")
        readAs<Dynamic>(j, p, out);
    else if (kind == "technique")
        readAs<TechniqueMarking>(j, p, out);
    else if (kind == "text")
        readAs<TextMarking>(j, p, out);
    else
        fail(p, "unknown attachment kind \"" + std::string(kind) + "\"");
}

// ── Staves, tracks, the score ──────────────────────────────────────────────────

void read(const Json& j, const Path& p, KitEntry& out)
{
    expectKind(j, p, "kit-entry");
    field(j, p, "id", out.id);
    field(j, p, "name", out.name);
    field(j, p, "sound", out.sound);
    field(j, p, "displayPitch", out.displayPitch);
    field(j, p, "notehead", out.notehead);
}

void read(const Json& j, const Path& p, Staff& out)
{
    expectKind(j, p, "staff");
    field(j, p, "id", out.id);
    field(j, p, "measures", out.measures);
    field(j, p, "staffSpanners", out.staffSpanners);
    field(j, p, "lines", out.lines);
}

void read(const Json& j, const Path& p, Transposition& out)
{
    expectObject(j, p);
    field(j, p, "diatonic", out.diatonic);
    field(j, p, "chromatic", out.chromatic);
    field(j, p, "octave", out.octave);
}

void read(const Json& j, const Path& p, Track& out)
{
    expectKind(j, p, "track");
    field(j, p, "id", out.id);
    field(j, p, "instrumentName", out.instrumentName);
    field(j, p, "instrumentAbbr", out.instrumentAbbr);
    field(j, p, "staves", out.staves);
    field(j, p, "trackSpanners", out.trackSpanners);
    field(j, p, "attachments", out.attachments);
    field(j, p, "transposition", out.transposition);
    field(j, p, "kit", out.kit);
}

void read(const Json& j, const Path& p, MusicData& out)
{
    expectObject(j, p);
    field(j, p, "title", out.title);
    field(j, p, "composer", out.composer);
    field(j, p, "tpq", out.tpq);
    field(j, p, "measures", out.measures);
    field(j, p, "tracks", out.tracks);
    field(j, p, "globalSpanners", out.globalSpanners);
}

// ── Layout profile ─────────────────────────────────────────────────────────────

void read(const Json& j, const Path& p, LayoutProfile::Margins& out)
{
    expectObject(j, p);
    field(j, p, "top", out.top);
    field(j, p, "bottom", out.bottom);
    field(j, p, "left", out.left);
    field(j, p, "right", out.right);
}

void read(const Json& j, const Path& p, LayoutProfile::Page& out)
{
    expectObject(j, p);
    field(j, p, "widthMm", out.widthMm);
    field(j, p, "heightMm", out.heightMm);
    field(j, p, "margins", out.margins);
}

void read(const Json& j, const Path& p, LayoutProfile::Spacing& out)
{
    expectObject(j, p);
    field(j, p, "quarterSp", out.quarterSp);
    field(j, p, "durationRatio", out.durationRatio);
    field(j, p, "minNoteSp", out.minNoteSp);
    field(j, p, "measurePaddingSp", out.measurePaddingSp);
}

void read(const Json& j, const Path& p, LayoutProfile::Distances& out)
{
    expectObject(j, p);
    field(j, p, "staffSp", out.staffSp);
    field(j, p, "trackSp", out.trackSp);
    field(j, p, "systemSp", out.systemSp);
    field(j, p, "firstIndentSp", out.firstIndentSp);
}

void read(const Json& j, const Path& p, LayoutProfile::Breaking& out)
{
    expectObject(j, p);
    field(j, p, "lastSystemFillMin", out.lastSystemFillMin);
}

void read(const Json& j, const Path& p, LayoutProfile::Fonts& out)
{
    expectObject(j, p);
    field(j, p, "music", out.music);
    field(j, p, "text", out.text);
}

void read(const Json& j, const Path& p, LayoutProfile::Padding& out)
{
    expectObject(j, p);
    field(j, p, "collisionSp", out.collisionSp);
}

void read(const Json& j, const Path& p, LayoutProfile& out)
{
    expectObject(j, p);
    field(j, p, "id", out.id);
    field(j, p, "name", out.name);
    field(j, p, "extends", out.extends);
    field(j, p, "page", out.page);
    field(j, p, "staffSpaceMm", out.staffSpaceMm);
    field(j, p, "spacing", out.spacing);
    field(j, p, "distances", out.distances);
    field(j, p, "breaking", out.breaking);
    field(j, p, "placement", out.placement);
    field(j, p, "fonts", out.fonts);
    field(j, p, "padding", out.padding);
}

// ── Layout setup ───────────────────────────────────────────────────────────────

void read(const Json& j, const Path& p, FlowSystem& out)
{
    expectObject(j, p);
    field(j, p, "startMeasureId", out.startMeasureId);
    field(j, p, "ySp", out.ySp);
    field(j, p, "staffYSp", out.staffYSp);
}

void read(const Json& j, const Path& p, FlowPage& out)
{
    expectObject(j, p);
    field(j, p, "systems", out.systems);
}

void read(const Json& j, const Path& p, Flow& out)
{
    expectObject(j, p);
    field(j, p, "pages", out.pages);
}

// The fields every layout element has.
template <class E>
void readElementBase(const Json& j, const Path& p, E& out)
{
    field(j, p, "id", out.id);
    field(j, p, "nodeId", out.nodeId);
}

void read(const Json& j, const Path& p, LayoutElement& out)
{
    const std::string_view kind = tagOf(j, p, "kind");
    if (kind == "break")
    {
        auto& e = out.emplace<BreakElement>();
        readElementBase(j, p, e);
        field(j, p, "type", e.type);
    }
    else if (kind == "no-break")
    {
        readElementBase(j, p, out.emplace<NoBreakElement>());
    }
    else if (kind == "stretch")
    {
        auto& e = out.emplace<StretchElement>();
        readElementBase(j, p, e);
        field(j, p, "factor", e.factor);
    }
    else if (kind == "offset")
    {
        auto& e = out.emplace<OffsetElement>();
        readElementBase(j, p, e);
        field(j, p, "x", e.x);
        field(j, p, "y", e.y);
        field(j, p, "part", e.part);
        field(j, p, "segment", e.segment);
    }
    else if (kind == "placement")
    {
        auto& e = out.emplace<PlacementElement>();
        readElementBase(j, p, e);
        field(j, p, "side", e.side);
    }
    else if (kind == "stem")
    {
        auto& e = out.emplace<StemElement>();
        readElementBase(j, p, e);
        field(j, p, "direction", e.direction);
    }
    else if (kind == "staff-gap")
    {
        auto& e = out.emplace<StaffGapElement>();
        readElementBase(j, p, e);
        field(j, p, "staffId", e.staffId);
        field(j, p, "extraSp", e.extraSp);
    }
    else if (kind == "hidden")
    {
        auto& e = out.emplace<HiddenElement>();
        readElementBase(j, p, e);
        field(j, p, "part", e.part);
    }
    else
    {
        fail(p, "unknown layout element kind \"" + std::string(kind) + "\"");
    }
}

void read(const Json& j, const Path& p, Layout& out)
{
    expectObject(j, p);
    field(j, p, "id", out.id);
    field(j, p, "name", out.name);
    field(j, p, "layoutProfileId", out.layoutProfileId);
    field(j, p, "trackIds", out.trackIds);
    field(j, p, "elements", out.elements);
    field(j, p, "flow", out.flow);
}

void read(const Json& j, const Path& p, LayoutSetup& out)
{
    expectObject(j, p);
    field(j, p, "layoutProfiles", out.layoutProfiles);
    field(j, p, "layouts", out.layouts);
}

// ── Entry points ───────────────────────────────────────────────────────────────

Json parseText(std::string_view text)
{
    try
    {
        return Json::parse(text.begin(), text.end());
    }
    catch (const Json::parse_error& e)
    {
        throw LoadError(std::string("invalid JSON: ") + e.what());
    }
}

Json parseFile(const std::filesystem::path& file)
{
    std::ifstream in(file, std::ios::binary);
    if (!in)
        throw LoadError("can't open " + file.string());
    try
    {
        return Json::parse(in);
    }
    catch (const Json::parse_error& e)
    {
        throw LoadError(file.string() + ": invalid JSON: " + e.what());
    }
}

template <class T>
T readRoot(const Json& j)
{
    T out;
    read(j, Path {}, out);
    return out;
}

// Puts the file name in front of a LoadError from reading it.
template <class T>
T readRootFrom(const Json& j, const std::filesystem::path& file)
{
    try
    {
        return readRoot<T>(j);
    }
    catch (const LoadError& e)
    {
        throw LoadError(file.string() + ": " + e.what());
    }
}

} // namespace

MusicData parseMusicData(std::string_view json) { return readRoot<MusicData>(parseText(json)); }

MusicData loadMusicData(const std::filesystem::path& file) { return readRootFrom<MusicData>(parseFile(file), file); }

LayoutSetup parseLayoutSetup(std::string_view json) { return readRoot<LayoutSetup>(parseText(json)); }

LayoutSetup loadLayoutSetup(const std::filesystem::path& file)
{
    return readRootFrom<LayoutSetup>(parseFile(file), file);
}

} // namespace msd
