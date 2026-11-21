#pragma once

// Mirrors prototype-web/src/core/types/InterpretationProfile.ts (conventions: see
// MusicData.h). qn stands for quarter note.
//
// The TS writes several objects inline ({ mode, value }, swell, hairpin, …). C++ needs
// a name for each; the names are given where they're declared.

#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "core/types/MusicData.h"
#include "core/types/MusicEvent.h"

namespace msd
{

// ── Enums ──────────────────────────────────────────────────────────────────────

enum class ChangeMode { Set, Add, Scale };

// String literal unions that have no name in TS.
enum class Phrasing { Detached, Connected };      // defaultPhrasing
enum class BeatPlacement { BeforeBeat, OnBeat };  // graceNotes.*.placement
enum class DynamicCombine { Add, Replace };       // DynamicMapping.combine
enum class DurationOp { Add, Scale, SetQn, SetRatio }; // NoteRuleAction for 'duration'

// ── Small values ───────────────────────────────────────────────────────────────

struct ValueChange // TS: { mode: ChangeMode, value: number }
{
    ChangeMode mode = ChangeMode::Set;
    double value = 0;
};

struct CurveShape
{
    CurveType curveType = CurveType::Linear;
    std::optional<double> steepness; // see CurveType
};

// ── Dynamics ───────────────────────────────────────────────────────────────────

struct DynamicFollowUp // TS: DynamicMapping.then
{
    double afterQn = 0;
    ChangeMode mode = ChangeMode::Set;
    double value = 0;    // against the value before
    CurveType curveType = CurveType::Linear; // from the peak value
};

struct DynamicMapping
{
    DynamicType type = DynamicType::Mf;
    ChangeMode mode = ChangeMode::Set;
    std::optional<DynamicCombine> combine; // default: Replace
    double value = 0; // sets the peak value
    std::optional<DynamicFollowUp> then;
};

// ── Note rules ─────────────────────────────────────────────────────────────────

// Match: TS NoteRuleMatch. The technique match is shared with CurveRule and
// InstrumentProfile's NoteMatch, which have the same JSON shape.
struct ArticulationMatch // { "kind": "articulation", "type": ... }
{
    ArticulationType type = ArticulationType::Accent;
};

struct TechniqueMatch // { "kind": "technique", "techniqueId": ... }
{
    std::string techniqueId; // from TechniqueMarking / CustomSpanner
};

struct SlurRuleMatch // { "kind": "slur", "position"?: ... }
{
    std::optional<SlurPosition> position;
};

struct GraceMatch // { "kind": "grace", "slash"?: ... }
{
    std::optional<bool> slash;
};

struct DynamicMatch // { "kind": "dynamic", "type": ... }: written on the note, e.g. sfp
{
    DynamicType type = DynamicType::Sfz;
};

struct AnyMatch // { "kind": "any" }
{};

using NoteRuleMatch =
    std::variant<ArticulationMatch, TechniqueMatch, SlurRuleMatch, GraceMatch, DynamicMatch, AnyMatch>;

struct NoteRuleCondition
{
    std::optional<double> maxNotatedQn;
    std::optional<double> minNotatedQn;
    std::optional<double> maxPreviousNotatedQn; // the note right before it in the same voice
    // the same two in real time, at the tempo the note is played in:
    std::optional<double> maxNotatedMs;
    std::optional<double> maxPreviousNotatedMs;
    std::optional<SlurPosition> slurPosition;
    std::optional<HairpinType> hairpin; // a hairpin of this type runs over the note
    std::optional<std::vector<int>> passes;
};

// Action: TS NoteRuleAction, told apart by "op" (and "for" between the first two).
struct DurationAction // { "op": ..., "for": "duration", "value": ... }; lengths in qn
{
    DurationOp op = DurationOp::Scale;
    double value = 0;
};

struct VelocityAction // { "op": ChangeMode, "for": "velocity", "value": ... }
{
    ChangeMode op = ChangeMode::Set;
    double value = 0;
};

struct StartOffsetAction // { "op": "startOffset", "qn": ... }; does not move the note's end
{
    double qn = 0;
};

struct AddTechniqueAction // { "op": "addTechnique", ... }: chaining techniques
{
    std::string techniqueId;
};

struct RemoveTechniqueAction // { "op": "removeTechnique", ... }: overriding a technique
{
    std::string techniqueId;
};

// A roll (an unmeasured tremolo that stays one note) starts with a single stroke of qn:
// played with it (together), or before it, the roll coming in after. Without qn, the
// stroke rings as long as the roll (together only).
struct RollAttackAction // { "op": "rollAttack", "qn"?: ..., "together": ... }
{
    std::optional<double> qn;
    bool together = true;
};

using NoteRuleAction = std::variant<DurationAction, VelocityAction, StartOffsetAction, AddTechniqueAction,
                                    RemoveTechniqueAction, RollAttackAction>;

struct NoteRule
{
    std::string id;               // a child profile replaces a parent rule with the same ID
    std::optional<bool> enabled;  // a child can switch a parent's rule off
    NoteRuleMatch match;
    std::optional<NoteRuleCondition> when; // optional refinement
    std::vector<NoteRuleAction> actions;
};

// ── Curve rules ────────────────────────────────────────────────────────────────

struct PedalMatch // { "kind": "pedal", "type": ... }
{
    PedalType type = PedalType::Sustain;
};

using CurveRuleMatch = std::variant<TechniqueMatch, PedalMatch>;

struct CurvePoint // an entry of CurveRule.points
{
    double time = 0;  // 0 ~ 1, 0 at curve start and 1 at curve end
    double value = 0; // 0 ~ 1, 0 is the start value and 1 the end value
    std::optional<CurveType> curveType;
    std::optional<double> steepness; // how intense the curve is
};

struct CurveRule
{
    std::string id;
    std::optional<bool> enabled;
    CurveRuleMatch match;
    CurveParameter parameter = BuiltInCurveParameter::Dynamics;
    std::optional<double> startValue; // 0 ~ 1, default: the current value
    std::optional<double> endValue;   // 0 ~ 1, default: the next value, or 1
    // A point at time 0 jumps to its value at the curve's start; its curveType has no
    // effect. A technique with scope UntilNext allows only a time-0 point.
    std::vector<CurvePoint> points;
};

// ── Profile ────────────────────────────────────────────────────────────────────

// A small cresc. into and dim. out of long notes and slow slurs, not written.
struct SwellSettings // TS: InterpretationProfile.swell
{
    double minMs = 0; // only notes played longer than this
    // A slur rises only from a first note written longer than this, and falls only into
    // a last note that is; default: minMs. Set it to where a slur counts as a fast run,
    // so that slow notes swell and fast runs do not.
    std::optional<double> slurMinMs;
    double startLevel = 0.75; // fraction of the written dynamic it starts and ends at
    double noteRatio = 0;     // share of a single note the rise and the fall each take
    double slurRatio = 0;     // share of a slur's first note (rise) and last note (fall)
    std::optional<CurveShape> rise; // default: linear
    std::optional<CurveShape> fall; // default: linear
};

struct HairpinSettings // TS: InterpretationProfile.hairpin
{
    std::optional<CurveShape> cresc; // default: linear
    std::optional<CurveShape> dim;   // default: linear
    double defaultChange = 0;        // when no dynamic ends it
};

// Write measured tremolos out as repeated notes. Without this setting every tremolo
// stays one note, for the patch to play.
struct TremoloExpansion // TS: InterpretationProfile.expandTremolo
{
    // Also unmeasured ones (patches that cannot roll), each note about this long.
    // Default: an unmeasured tremolo stays one note, a roll.
    std::optional<double> unmeasuredMs;
};

struct GraceNoteTiming // TS: graceNotes.appoggiatura / graceNotes.acciaccatura
{
    std::optional<BeatPlacement> placement; // default: OnBeat for appoggiaturas, BeforeBeat for acciaccaturas
    double ratio = 0; // appoggiatura: how much of the main note it takes; acciaccatura:
                      // how much it prepends, relative to the notated grace notes
};

struct GraceNoteSettings // TS: InterpretationProfile.graceNotes
{
    GraceNoteTiming appoggiatura;
    GraceNoteTiming acciaccatura; // slashed
};

struct InterpretationProfile
{
    std::string id;
    std::string name;
    std::optional<std::string> extends; // parent profile ID
    std::optional<double> initialDynamics; // 0 - 1
    std::optional<Phrasing> defaultPhrasing; // default: Detached
    // Connected: an unbroken run of single notes in a voice is played as if slurred.
    // A run breaks at a rest, a staccato note, an accent, a repeated pitch, a chord,
    // and where a written slur begins or ends.
    std::optional<SwellSettings> swell;
    std::optional<std::vector<DynamicMapping>> dynamics; // merged by type
    std::optional<HairpinSettings> hairpin;
    std::optional<TremoloExpansion> expandTremolo;
    std::optional<GraceNoteSettings> graceNotes;
    std::vector<NoteRule> noteRules;
    std::vector<CurveRule> curveRules;
};

} // namespace msd
