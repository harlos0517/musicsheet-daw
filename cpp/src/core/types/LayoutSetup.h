#pragma once

// Mirrors prototype-web/src/core/types/LayoutSetup.ts.
//
// Everything layout needs besides the score itself. Layout never goes into MusicData:
// the score and each part lay out the same music, each in its own way.
//
// Pages do not move while the music is edited. Where systems and pages break, and
// where each system sits on its page, is kept in `flow`, the result of the last reflow;
// edits only respace the notes inside a system. A reflow runs only when it is asked
// for, and computes `flow` again from the profile and the layout elements.

#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "core/types/LayoutProfile.h"
#include "core/types/MusicData.h"

namespace msd
{

/**
 * Where the last reflow broke the music, and where it put each system. A system holds
 * the measures from its start up to the next system's start. A measure added since
 * goes into the system of the measure before it, which may then be too full: it is
 * squeezed until the next reflow. When a system's start measure is deleted, the system
 * starts at the measure after it.
 */
struct FlowSystem
{
    NodeId startMeasureId;
    double ySp = 0; // top of its first staff, from the page's top margin
    std::map<NodeId, double> staffYSp; // staff ID → top of the staff, from the system's top;
                                       // a staff not listed (one added since) goes below the last one
};

struct FlowPage
{
    std::vector<FlowSystem> systems;
};

struct Flow
{
    std::vector<FlowPage> pages;
};

// ── Layout elements: what the user set by hand ─────────────────────────────────
// Each has its own `id` and the `nodeId` of the MusicData node it belongs to; it is
// dropped when that node is deleted.

enum class BreakType { System, Page };
enum class StemDirection { Up, Down };

/** A system or page break after this measure, kept by every reflow. */
struct BreakElement
{
    std::string id;
    NodeId nodeId;
    BreakType type = BreakType::System;
};

/** This measure stays on the same system as the next one. */
struct NoBreakElement
{
    std::string id;
    NodeId nodeId;
};

/** This measure gets more or less room than its notes ask for. */
struct StretchElement
{
    std::string id;
    NodeId nodeId;
    double factor = 1; // 1 leaves it as it is
};

/** Moves a drawn item away from where the layout put it, in sp. */
struct OffsetElement
{
    std::string id;
    NodeId nodeId;
    double x = 0;
    double y = 0; // down is positive
    std::optional<std::string> part; // 'accidental', 'stem', 'dot', 'number' (tuplet);
                                     // default: all of it
    std::optional<int> segment; // a spanner cut by system breaks: which piece, from 0;
                                // default: every piece
};

/** Which side of the staff, or of the notes, an item goes on. */
struct PlacementElement
{
    std::string id;
    NodeId nodeId;
    Side side = Side::Above; // slur, tie, tuplet, dynamic, articulation, text
};

struct StemElement
{
    std::string id;
    NodeId nodeId;
    StemDirection direction = StemDirection::Up;
};

/** Extra room below a staff, on the systems from this measure on, kept by reflows. */
struct StaffGapElement
{
    std::string id;
    NodeId nodeId;
    NodeId staffId;
    double extraSp = 0;
};

/** Not drawn: a rest in a second voice, a courtesy accidental. Still played. */
struct HiddenElement
{
    std::string id;
    NodeId nodeId;
    std::optional<std::string> part; // as in OffsetElement; default: all of it
};

using LayoutElement = std::variant<BreakElement, NoBreakElement, StretchElement, OffsetElement, PlacementElement,
                                   StemElement, StaffGapElement, HiddenElement>;

/** The full score, or one part. */
struct Layout
{
    std::string id; // 'score', 'part-tpt1'
    std::string name;
    std::string layoutProfileId;
    std::optional<std::vector<NodeId>> trackIds; // in score order; default: every track
    std::vector<LayoutElement> elements; // what the user set by hand; every reflow keeps them
    std::optional<Flow> flow;            // none yet: the first render reflows
};

struct LayoutSetup
{
    std::vector<LayoutProfile> layoutProfiles;
    std::vector<Layout> layouts;
};

} // namespace msd
