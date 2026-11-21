#pragma once

// Mirrors prototype-web/src/core/types/InstrumentProfile.ts (conventions: see
// MusicData.h). MIDI numbers (notes, velocities, CC numbers, channels) are int.

#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "core/types/InterpretationProfile.h"
#include "core/types/MusicEvent.h"

namespace msd
{

enum class ChannelMode { Replace, Add }; // TechniqueMapping.channelMode

// ── Small values ───────────────────────────────────────────────────────────────

struct OutputRange
{
    int low = 1;    // MIDI value for 0
    int high = 127; // MIDI value for 1
    // How 0–1 is spread between them; default: linear. Power with steepness above 0
    // puts soft dynamics closer together and loud ones further apart; below 0, the
    // other way round.
    std::optional<CurveShape> curve;
};

struct Keyswitch // TS: { note: number, velocity: number }
{
    int note = 0;
    int velocity = 0;
};

struct KeyswitchRange
{
    double min = 0;
    double max = 1;
    int note = 0; // the keyswitch note number
    int velocity = 0;
};

// A value chosen by how long the note is written, in real time.
struct ByDuration
{
    double shortMs = 0; // at or below this length the value is atShort
    double longMs = 0;  // at or above this length the value is atLong; linear in between
    double atShort = 0;
    double atLong = 0;
};

// ── Technique mappings ─────────────────────────────────────────────────────────

// Match: TS NoteMatch. The technique match is TechniqueMatch (InterpretationProfile.h).
struct CurveRangeMatch // { "kind": "curve", "parameter": ..., "min": ..., "max": ... }
{
    CurveParameter parameter = BuiltInCurveParameter::Dynamics;
    double min = 0;
    double max = 1;
};

struct SlurMatch // { "kind": "slur", "position": ... }; position is required here
{
    SlurPosition position = SlurPosition::First;
};

using NoteMatch = std::variant<TechniqueMatch, CurveRangeMatch, SlurMatch>;

struct TechniqueCc // an entry of TechniqueMapping.cc
{
    int number = 0;
    std::variant<int, ByDuration> value = 0;
};

struct TechniqueMapping
{
    std::vector<NoteMatch> matches; // all must be present
    std::optional<Keyswitch> keyswitch;
    // A controller that a curve mapping also drives is held at this value while the
    // note sounds, and goes back to the curve afterwards.
    std::optional<std::vector<TechniqueCc>> cc;
    std::optional<ChannelMode> channelMode; // default: Replace
    std::optional<std::vector<int>> channels; // default: every channel; otherwise only these
    // The length is cut off before the next start of the same note, so they don't overlap.
    std::optional<ValueChange> durationMs;
    std::optional<double> startOffsetMs; // applied after the duration
    std::optional<ValueChange> velocity;
    // Played instead of the note's own pitch; several take turns, e.g. two keys of the
    // same hit so that repeats do not sound the same.
    std::optional<std::vector<int>> notes;
    // The note keeps the end it is played to (before durationMs): with durationMs it
    // starts that long before it (a sample that peaks at its end).
    std::optional<bool> alignToEnd;
};

// ── Curve mappings ─────────────────────────────────────────────────────────────

struct CurveCc // an entry of CurveMapping.cc
{
    int number = 0;
    OutputRange mapping;
};

struct CurveMapping
{
    CurveParameter parameter = BuiltInCurveParameter::Dynamics;
    double defaultValue = 0; // 0 ~ 1
    std::optional<std::vector<CurveCc>> cc;                 // to every channel it uses
    std::optional<std::vector<KeyswitchRange>> keyswitch;   // later matches win; to every channel it uses
    std::optional<std::vector<int>> channels;
};

// ── Profile ────────────────────────────────────────────────────────────────────

struct FixedVelocity // JSON: { "fixed": ... }
{
    int fixed = 100; // 1 - 127
};

struct MappedVelocity // JSON: { "mapping": ... }: the event's velocity, 0 - 1
{
    OutputRange mapping;
};

// A temporary key that stops the sound: held until the next note or for a beat;
// letting go switches the patch back.
struct DampKeyswitch // TS: InstrumentProfile.dampKeyswitch
{
    int note = 0;
    int velocity = 0;
    std::optional<bool> atRests; // where a rest follows a note (its written end); default: true
    std::optional<std::vector<std::string>> afterTechniques; // also right where such a note's
                                                             // sound ends (staccato)
};

struct InstrumentProfile
{
    std::string id;
    std::string name;        // "Library A – Violin"
    double trackDelayMs = 0; // set on the DAW track, not in the MIDI output
    std::variant<FixedVelocity, MappedVelocity> velocity;
    // If a note matches several mappings, changes accumulate/merge, except keyswitches
    // and channels (the later wins) and mode Set on numbers.
    std::vector<TechniqueMapping> techniqueMappings;
    std::vector<CurveMapping> curveMappings; // dynamics, pedals, custom parameters
    int defaultChannel = 1; // a note without channels after mapping goes here
    // Sound → MIDI note ('snare' → 38); several notes take turns, two keys of the same
    // drum so that repeats do not sound the same.
    std::optional<std::map<std::string, std::variant<int, std::vector<int>>>> kit;
    // Semitones added to every pitched note; for a patch mapped away from sounding
    // pitch (a piccolo or bass patch played at written pitch).
    std::optional<int> pitchOffset;
    std::optional<double> keyswitchLeadMs; // how early to send keyswitches
    std::optional<DampKeyswitch> dampKeyswitch;
};

} // namespace msd
