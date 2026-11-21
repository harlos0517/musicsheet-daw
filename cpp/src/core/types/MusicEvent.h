#pragma once

// Mirrors prototype-web/src/core/types/MusicEvent.ts, the way MusicData.h mirrors
// MusicData.ts (see the comment at its top).
//
// Ticks here are double, not msd::Tick: rules scale lengths (0.9 × a dotted eighth
// …), and the TS keeps the fraction. Keeping it here too lets the C++ output be
// compared with .events.json exactly.

#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "core/types/MusicData.h"

namespace msd
{

// ── Enums ──────────────────────────────────────────────────────────────────────

// hold: holds the start value and changes at the end
// What steepness (k) means for each type:
//   hold, linear  no effect
//   exp           (e^(kt) − 1) / (e^k − 1)
//   power         k > 0: t^(1+k); k < 0: 1 − (1−t)^(1−k)
//   ease          the power curve on each half, mirrored at the middle
enum class CurveType { Hold, Linear, Exp, Power, Ease };

enum class SlurPosition { First, Inner, Last };

enum class CurveCombine { Add, Replace, Scale }; // CurveEvent.combine

// ── CurveParameter ─────────────────────────────────────────────────────────────
// TS: 'dynamics' | 'sustain' | 'sostenuto' | 'una-corda' | { kind: 'custom', name }

enum class BuiltInCurveParameter { Dynamics, Sustain, Sostenuto, UnaCorda };

struct CustomCurveParameter // JSON: { "kind": "custom", "name": ... }
{
    std::string name;

    bool operator==(const CustomCurveParameter&) const = default;
};

// Comparable with ==, so a mapping can be found by its parameter.
using CurveParameter = std::variant<BuiltInCurveParameter, CustomCurveParameter>;

// ── Events ─────────────────────────────────────────────────────────────────────
// TS shares id, sourceIds and trackId through MusicEventBase; each struct repeats them.
//   id         ${sourceId}@${pass}#${segmentIndex}
//   sourceIds  notes or elements it came from (could be several)

struct KitSound // JSON: { "sound": ... }
{
    std::string sound;
};

using EventPitch = std::variant<int, KitSound>; // sounding MIDI number, or a kit sound

struct NoteEvent
{
    std::string id;
    std::vector<NodeId> sourceIds;
    NodeId trackId;

    NodeId staffId;
    int voiceIndex = 0;
    int pass = 1;
    EventPitch pitch = 60;
    double startTick = 0;     // played-order ticks (after repeat expansion)
    double notatedTicks = 0;  // before rules; lets an instrument ignore length rules
    double durationTicks = 0; // after rules
    double velocity = 0;      // 0 ~ 1, attack strength
    std::vector<std::string> techniques; // registry IDs: 'core:short', 'core:accent'
    std::optional<double> startOffsetMs; // humanizing / attack compensation, after the tempo map
    std::optional<SlurPosition> slur;
};

struct CurveTarget // an entry of CurveEvent.appliesTo
{
    NodeId staffId;
    std::optional<int> voiceIndex;
};

// Curves for the same parameter shouldn't overlap; if they do, the later one wins.
// The value holds between segments.
struct CurveEvent
{
    std::string id;
    std::vector<NodeId> sourceIds;
    NodeId trackId;

    int pass = 1;
    int segmentIndex = 0;
    CurveParameter parameter = BuiltInCurveParameter::Dynamics;
    std::optional<std::vector<CurveTarget>> appliesTo; // a curve tied to a technique group
    std::optional<CurveCombine> combine; // default: Replace
    // Scale: the value multiplies the rest of the curve (1 leaves it as it is); used for
    // shaping relative to the written level, such as swells.
    double startTick = 0;
    double durationTicks = 0;
    double fromValue = 0; // 0 ~ 1
    double toValue = 0;   // 0 ~ 1
    std::optional<CurveType> curveType; // ignored (Hold) when durationTicks is 0
    std::optional<double> steepness;
};

using MusicEvent = std::variant<NoteEvent, CurveEvent>;

} // namespace msd
