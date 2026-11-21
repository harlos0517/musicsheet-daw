#pragma once

// Pages. Two steps, kept apart so that pages don't move while the music is edited:
//
//  - reflow() decides where systems and pages break, and where each system and staff
//    sits. Its result is Layout.flow. It runs only when asked for (and the first time
//    a layout is shown, when it has no flow yet).
//  - layoutPages() lays out the pages a flow describes. Editing the music and laying
//    out again respaces the notes inside each system, but keeps every system where the
//    flow put it.
//
// Page coordinates are in sp from the page's top-left corner, y down.

#include <vector>

#include "core/types/LayoutProfile.h"
#include "core/types/LayoutSetup.h"
#include "core/types/MusicData.h"
#include "layout/Geometry.h"
#include "layout/SmuflFont.h"
#include "layout/SystemLayout.h"

namespace msd
{

struct ScoreInput
{
    const MusicData& music;
    const LayoutProfile& profile;
    const SmuflFont& font;
    const Layout& layout; // which tracks, and the user's breaks and staff gaps
    HeaderSpacing header {};
    NoteSpacing notes {};
    BeamSpacing beams {};
};

struct PlacedSystem
{
    Point origin; // where the system's (0, 0) goes: its left edge, its first staff's top line
    SystemLayout system;
};

struct PageLayout
{
    double widthSp = 0;
    double heightSp = 0;
    std::vector<PlacedSystem> systems;
};

// The staves a layout shows, in score order: those of Layout::trackIds, or of every
// track.
std::vector<NodeId> layoutStaffIds(const MusicData& music, const Layout& layout);

// The height of a system with every staff the layout shows, at the profile's
// distances, without the user's staff gaps: from the first staff's top line to the last
// one's bottom line, in sp.
double systemHeightSp(const MusicData& music, const Layout& layout, const LayoutProfile& profile);

// Whether such a system fits the profile's page. Throws std::invalid_argument when the
// profile has no staff size.
bool systemFitsPage(const MusicData& music, const Layout& layout, const LayoutProfile& profile);

// Breaks the music into systems and pages, and places each system and staff.
//  - Systems: measures are added while their natural widths fit the page's width (less
//    the first system's indent), so a fresh reflow never squeezes. A measure wider
//    than the page gets a system of its own. BreakElement forces a break after its
//    measure; NoBreakElement keeps its measure on the same system as the next.
//  - Staves: distances.staffSp apart within a track, trackSp between tracks, plus the
//    StaffGapElement in force below a staff.
//  - Pages: systems are stacked distances.systemSp apart while they fit the page's
//    height. A page BreakElement starts a new page. A system taller than the page gets
//    one of its own, and runs past the bottom margin.
// Throws std::invalid_argument when the profile has no staff size.
Flow reflow(const ScoreInput& input);

// Lays out the pages of a flow. Systems stretch to the page's width; the last one
// only when its notes fill at least breaking.lastSystemFillMin of it. A system holds
// the measures from its start up to the next system's start, so a measure added since
// the reflow goes into the system before it, which may then be overfull. A system
// whose start measure is gone is left out, its measures joining the system before it;
// a staff the flow doesn't place goes below the system's last staff.
std::vector<PageLayout> layoutPages(const ScoreInput& input, const Flow& flow);

} // namespace msd
