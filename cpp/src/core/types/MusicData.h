#pragma once

// Mirrors prototype-web/src/core/types/MusicData.ts. Keep the two in step: same
// names, same fields, same order. JSON keys are the member names.
//
// How the TS maps to C++ (see also cpp/README.md):
//   interface X extends MusicDataNode  → struct X with `NodeId id` as its first member.
//                                        `kind` is not stored: the type itself says it.
//   field?: T                          → std::optional<T>, never filled in by the loader.
//                                        Defaults ("default 5") are applied where used.
//   'a' | 'b' and string enums         → enum class, names in core/json/EnumNames.h
//   A | B (tagged by kind or type)     → std::variant<A, B>
//   T[]                                → std::vector<T>
//
// Every struct is a plain aggregate, so tests can build one field by field:
//   Note n { .id = "n1", .pitch = { .step = Step::C, .alter = 0, .octave = 4 } };

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace msd
{

using NodeId = std::string;
using Tick = std::int64_t;

// ── Enums ──────────────────────────────────────────────────────────────────────

// Numeric in TS and in the JSON: the value is log2 of the length in quarters.
enum class DurationType : std::int8_t {
    Longa = 4,
    Double = 3,
    Whole = 2,
    Half = 1,
    Quarter = 0,
    Eighth = -1,
    Sixteenth = -2,
    ThirtySecond = -3,
    SixtyFourth = -4,
    OneHundredTwentyEighth = -5,
};

enum class ArticulationType { Accent, Tenuto, Staccato, Staccatissimo };

enum class ClefType { G, F, C, Percussion };

enum class Step { C, D, E, F, G, A, B };

enum class BarlineStyle { Single, Double, Dashed, Final };

enum class DynamicType { Pppp, Ppp, Pp, P, Mp, Mf, F, Ff, Fff, Ffff, Sfz, Fp, Sf, Rfz };

// String literal unions that have no name in TS.
enum class BeamMode { BreakBefore, JoinPrevious, None };        // Chord/Rest.beam
enum class AccidentalDisplay { Show, Hide, Cautionary };        // Note.accidentalDisplay
enum class GracePlacement { Before, After };                    // GraceChords.placement
enum class MarkerType { Segno, Coda, Fine, ToCoda };            // Marker.type
enum class HairpinType { Cresc, Dim };                          // Hairpin.type
enum class PedalType { Sustain, Sostenuto, UnaCorda };          // Pedal.type
enum class TempoChangeType { Rit, Accel };                      // TempoChange.type
enum class TechniqueScope { This, UntilNext };                  // TechniqueMarking.scope
enum class Notehead { Normal, X, CircleX, Triangle, Diamond };  // KitEntry.notehead

// ── Small values ───────────────────────────────────────────────────────────────

struct Duration
{
    DurationType type = DurationType::Quarter;
    int dots = 0;
};

struct Pitch
{
    Step step = Step::C;
    int alter = 0; // -2..2
    int octave = 4;
};

// ── Anchors ────────────────────────────────────────────────────────────────────

struct ElementAnchor // JSON: { "type": "element", "id": ... }
{
    NodeId id; // chord, rest, or note
};

struct TickAnchor // JSON: { "type": "tick", "measureId": ..., "tick": ... }
{
    NodeId measureId; // free position in a bar
    Tick tick = 0;    // tick = bar length means just before the barline
};

using Anchor = std::variant<ElementAnchor, TickAnchor>;

// ── Measure-level items ────────────────────────────────────────────────────────

struct TimeSignature
{
    NodeId id;
    int beats = 4;
    int unit = 4;
};

struct KeySignature
{
    NodeId id;
    int accidentals = 0; // negative as flats, positive as sharps
};

struct Tempo
{
    NodeId id;
    Duration unit;
    double bpm = 120.0;
    Tick tick = 0;
    std::optional<std::string> text;
};

struct Clef
{
    NodeId id;
    ClefType type = ClefType::G;
    int line = 2;
    int octaveChange = 0;
};

// ── Repeat marks ───────────────────────────────────────────────────────────────

struct Marker
{
    NodeId id;
    MarkerType type = MarkerType::Segno;
    std::optional<std::string> label; // for scores with several codas
};

struct JumpToStart // JSON: { "type": "start" }
{};

struct JumpToMarker // JSON: { "type": "marker", "id": ... }
{
    NodeId id;
};
using JumpTarget = std::variant<JumpToStart, JumpToMarker>;

struct Jump
{
    NodeId id;
    JumpTarget to;
    std::optional<NodeId> until; // marker ID where this pass stops (fine / to-coda)
    std::optional<NodeId> then;  // marker ID to continue from (coda)
    bool playRepeats = false;    // repeat again after the jump?
    std::optional<std::string> displayText; // e.g. "D.S. al Coda"
};

struct RepeatEnd
{
    int times = 2; // total times played
};

struct Measure
{
    NodeId id;
    std::optional<TimeSignature> timeSig;
    std::optional<KeySignature> key;
    std::vector<Tempo> tempos;
    std::optional<Tick> actualTicks;

    // repeats
    std::optional<bool> repeatStart;
    std::optional<RepeatEnd> repeatEnd;
    std::optional<BarlineStyle> barline;
    std::vector<Marker> markers;
    std::vector<Jump> jumps;
};

// ── Notes and chords ───────────────────────────────────────────────────────────

struct TieTarget
{
    NodeId to;
};

struct LetRing // JSON: { "letRing": true }
{};

// TS: { to: string }[] | { letRing: true }. Several targets when crossing repeats.
using Tie = std::variant<std::vector<TieTarget>, LetRing>;

struct Note
{
    NodeId id;
    Pitch pitch;
    std::optional<Tie> tie;
    std::optional<AccidentalDisplay> accidentalDisplay;
};

struct UnpitchedNote
{
    NodeId id;
    NodeId kitEntryId;
    std::optional<Tie> tie;
};

using ChordNote = std::variant<Note, UnpitchedNote>;

struct Articulation
{
    NodeId id;
    ArticulationType type = ArticulationType::Accent;
};

// TS: Omit<Chord, 'graceNotes'>. Its JSON kind is still "chord".
struct GraceChord
{
    NodeId id;
    Duration duration;
    std::vector<ChordNote> notes;
    std::vector<Articulation> articulations;
    std::optional<BeamMode> beam;
};

struct GraceChords
{
    NodeId id;
    std::optional<bool> slash;
    std::vector<GraceChord> chords;
    std::optional<GracePlacement> placement;
};

struct Chord
{
    NodeId id;
    Duration duration;
    std::vector<ChordNote> notes;
    std::vector<Articulation> articulations;
    std::optional<GraceChords> graceNotes;
    std::optional<BeamMode> beam;
};

struct Rest
{
    NodeId id;
    Duration duration;
    std::optional<bool> fullMeasure;
    std::optional<BeamMode> beam;
};

struct Space
{
    NodeId id;
    Duration duration;
};

// A Tuplet holds VoiceEvents, and a VoiceEvent can be a Tuplet. A `using` alias can't
// refer to itself, so VoiceEvent is a struct that *is* the variant (it inherits from
// it). std::visit, std::get and std::holds_alternative work on it as on any variant.
struct VoiceEvent;

struct Tuplet
{
    NodeId id;
    Duration unit;
    int normal = 2;
    int actual = 3;
    std::vector<VoiceEvent> events; // std::vector may hold a type declared but not yet defined
};

struct VoiceEvent : std::variant<Chord, Rest, Space, Tuplet>
{
    using variant::variant; // take over the variant's constructors
};

struct Voice
{
    NodeId id;
    int index = 0;
    std::vector<VoiceEvent> events;
};

struct ClefChange // TS: { tick: number, clef: Clef }
{
    Tick tick = 0;
    Clef clef;
};

struct StaffMeasure
{
    NodeId id;
    NodeId measureId;
    std::vector<ClefChange> clefs;
    std::vector<Voice> voices;
    // every voice must fill the measure's length (actualTicks, otherwise from the time signature).
};

// ── Spanners ───────────────────────────────────────────────────────────────────

struct Slur
{
    NodeId id;
    ElementAnchor start;
    ElementAnchor end; // always include end
};

struct Hairpin
{
    NodeId id;
    Anchor start;
    Anchor end;
    std::optional<bool> includeEnd;              // default: false
    std::optional<std::vector<NodeId>> staffIds; // default: all staves of the track
    HairpinType type = HairpinType::Cresc;
    std::optional<bool> niente;
};

struct Ottava
{
    NodeId id;
    ElementAnchor start;
    ElementAnchor end; // always include end
    int shift = 1;     // -2, -1, 1 or 2
};

struct Pedal
{
    NodeId id;
    PedalType type = PedalType::Sustain;
    TickAnchor start;
    TickAnchor end;
    std::optional<bool> includeEnd; // default: false
};

struct CustomSpanner
{
    NodeId id;
    std::optional<std::vector<NodeId>> staffIds; // default: all staves of the track
    std::optional<std::vector<int>> voiceIndexes; // default: all voices
    Anchor start;
    Anchor end;
    std::optional<bool> includeEnd; // default: false
    std::string techniqueId;
    std::optional<std::string> text;
    std::optional<std::vector<int>> passes;
};

using StaffSpanner = Ottava; // TS: type StaffSpanner = Ottava
using TrackSpanner = std::variant<Slur, Hairpin, Pedal, CustomSpanner>;

struct Volta
{
    NodeId id;
    NodeId startMeasureId;
    NodeId endMeasureId;
    std::vector<int> passes;     // [1], [2], [1, 2, 3]…
    std::optional<bool> openEnd; // drawn without a closing hook
};

struct TempoChange // score-wide
{
    NodeId id;
    TickAnchor start;
    TickAnchor end;
    TempoChangeType type = TempoChangeType::Rit;
    std::optional<double> targetTempo; // otherwise the playback profile decides
};

using GlobalSpanner = std::variant<Volta, TempoChange>;

// ── Dynamics and text ──────────────────────────────────────────────────────────
// TS shares these first fields through AttachmentBase; here each struct repeats them,
// so all three stay plain aggregates.
//   staffIds      default: all staves of the track
//   voiceIndexes  default: all voices
//   beforeTick    default: false. Placed at the end of the previous event (e.g. the
//                 dynamic where a hairpin ends), not on the chord at this tick.

struct Dynamic
{
    NodeId id;
    std::optional<std::vector<NodeId>> staffIds;
    std::optional<std::vector<int>> voiceIndexes;
    std::optional<bool> beforeTick;

    Anchor at;
    DynamicType type = DynamicType::Mf;
    std::optional<std::string> text; // custom display, e.g. "sub. p", "mf espress."
};

struct TechniqueMarking
{
    NodeId id;
    std::optional<std::vector<NodeId>> staffIds;
    std::optional<std::vector<int>> voiceIndexes;
    std::optional<bool> beforeTick;

    std::string techniqueId; // registry ID: 'core:pizz', 'user:sul-pont'
    std::optional<std::string> text; // display, e.g. "pizz."
    std::optional<std::vector<int>> passes;
    Anchor at;
    TechniqueScope scope = TechniqueScope::UntilNext; // a TickAnchor allows only UntilNext
};

struct TextMarking
{
    NodeId id;
    std::optional<std::vector<NodeId>> staffIds;
    std::optional<std::vector<int>> voiceIndexes;
    std::optional<bool> beforeTick;

    std::optional<std::string> text;
    Anchor at;
};

using Attachment = std::variant<Dynamic, TechniqueMarking, TextMarking>;

// ── Percussion ─────────────────────────────────────────────────────────────────

struct KitEntry
{
    NodeId id;
    std::string name;  // "Snare", "Closed Hi-hat"
    std::string sound; // e.g. 'snare': the Instrument Profile maps this to a MIDI note
    int displayPitch = 0; // relative to percussion clef center
    Notehead notehead = Notehead::Normal;
};

// ── Staves, tracks, the score ──────────────────────────────────────────────────

struct Staff
{
    NodeId id;
    std::vector<StaffMeasure> measures;
    std::vector<StaffSpanner> staffSpanners;
    std::optional<int> lines; // default 5
};

struct Transposition // written -> sounding; B♭ clarinet: { -1, -2, 0 }
{
    int diatonic = 0;
    int chromatic = 0;
    int octave = 0;
};

struct Track
{
    NodeId id;
    std::string instrumentName;
    std::string instrumentAbbr;
    std::vector<Staff> staves;
    std::vector<TrackSpanner> trackSpanners;
    std::vector<Attachment> attachments;
    std::optional<Transposition> transposition;

    // percussion
    std::optional<std::vector<KitEntry>> kit; // only for percussion tracks
};

struct MusicData
{
    std::string title;
    std::string composer;
    int tpq = 480;
    std::vector<Measure> measures;
    std::vector<Track> tracks;
    std::vector<GlobalSpanner> globalSpanners;
};

} // namespace msd
