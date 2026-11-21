#pragma once

// Mirrors prototype-web/src/core/types/EventOverride.ts (conventions: see MusicData.h).
// TS shares id, sourceId and passes through MusicEventOverrideBase; each struct
// repeats them.

#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "core/types/InterpretationProfile.h"
#include "core/types/MusicEvent.h"

namespace msd
{

struct TechniqueChanges // TS: NoteEventOverride.techniques
{
    std::optional<std::vector<std::string>> add;
    std::optional<std::vector<std::string>> remove;
};

struct NoteEventOverride
{
    std::string id;
    NodeId sourceId;
    std::optional<std::vector<int>> passes; // default: every pass

    std::optional<EventPitch> pitch;
    std::optional<ValueChange> durationTicks; // changes the length the rules left
    std::optional<double> startTickOffset;    // does not move the note's end
    std::optional<ValueChange> velocity;
    std::optional<TechniqueChanges> techniques;
    std::optional<double> startOffsetMs;
};

struct CurveEventOverride
{
    std::string id;
    NodeId sourceId;
    std::optional<std::vector<int>> passes; // default: every pass

    int segmentIndex = 0;
    std::optional<CurveParameter> parameter;
    std::optional<std::vector<CurveTarget>> appliesTo;
    std::optional<ValueChange> durationTicks; // changes the length the rules left
    std::optional<double> startTickOffset;    // does not move the curve's end
    std::optional<ValueChange> fromValue;
    std::optional<ValueChange> toValue;
    std::optional<CurveType> curveType;
    std::optional<double> steepness;
};

using MusicEventOverride = std::variant<NoteEventOverride, CurveEventOverride>;

} // namespace msd
