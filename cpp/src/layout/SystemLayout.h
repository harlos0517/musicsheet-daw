#pragma once

// Lays out one system: some measures of some staves, spaced together so that notes
// sounding at the same time line up. The result is a list of boxes, each saying which
// MusicData node it came from and what to draw. Nothing here knows about JUCE or a
// window; painting (cpp/src/app) only draws what the boxes say.
//
// Which measures and staves make up the system, and where each staff sits, is the
// caller's choice: it comes from Layout.flow (see PageLayout.h). This function only
// spaces the music inside the system it is given, so editing a note never moves a
// system.
//
// Coordinates are in staff spaces (sp): x from the system's left edge, y from the top
// line of the system's first staff, down positive.

#include <cstddef>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "core/types/LayoutProfile.h"
#include "core/types/MusicData.h"
#include "layout/Geometry.h"
#include "layout/SmuflFont.h"

namespace msd
{

// Which piece of a node a box draws. Where LayoutSetup names a part (OffsetElement,
// HiddenElement: 'accidental', 'stem', …), it refers to these.
enum class Part {
    StaffLine,     // node: the staff
    LedgerLine,    // node: the chord
    Barline,       // node: the measure; the line at a system's start: its first measure
    Brace,         // node: the track
    Clef,          // node: the clef
    KeySignature,  // node: the key signature; one box per accidental
    TimeSignature, // node: the time signature; one box per digit
    Notehead,      // node: the note
    Accidental,    // node: the note; with parentheses, one box per glyph
    Dot,           // node: the note or rest; one box per dot
    Stem,          // node: the chord
    Flag,          // node: the chord
    Beam,          // node: the chord where the beam starts; one box per beam and hook
    Rest,          // node: the rest
};

// A SMuFL glyph, drawn with its origin at `origin`.
struct GlyphShape
{
    std::string glyph; // SMuFL name, e.g. "noteheadBlack"
    Point origin;
    double scale = 1; // 1: the font's own size. Braces grow to fit their staves.
};

// A straight line of a given thickness, with square ends at `from` and `to`.
struct LineShape
{
    Point from;
    Point to;
    double thickness = 0;
};

// A beam: a parallelogram with vertical ends at from.x and to.x, reaching
// thickness / 2 above and below the line from `from` to `to`.
struct BeamShape
{
    Point from;
    Point to;
    double thickness = 0;
};

struct LayoutBox
{
    NodeId nodeId;   // the MusicData node it came from
    Part part = Part::Notehead;
    Rect bounds;     // what it covers, for hit testing and collisions
    std::variant<GlyphShape, LineShape, BeamShape> shape;
};

struct MeasureSlot
{
    NodeId measureId;
    double left = 0;  // the previous barline, or the system's start
    double right = 0; // this measure's barline
};

struct StaffSlot
{
    NodeId staffId;
    double y = 0; // its top line
    int lines = 5;
};

struct SystemLayout
{
    double width = 0;
    std::vector<StaffSlot> staves;
    std::vector<MeasureSlot> measures;
    std::vector<LayoutBox> boxes; // in drawing order
};

// Gaps in the system header (clef, key, time signature) and around signatures inside
// the system, in sp. LayoutProfile has no fields for these yet.
struct HeaderSpacing
{
    double clefLeftSp = 0.75;        // system start → clef
    double betweenItemsSp = 1.0;     // clef → key → time signature
    double keyAccidentalGapSp = 0.15; // between the accidentals of a key signature
    double toFirstNoteSp = 2.0;      // last header item → first note
    double braceGapSp = 0.3;         // brace → system start
};

// Gaps around notes, in sp. LayoutProfile has no fields for these yet either.
struct NoteSpacing
{
    double accidentalGapSp = 0.2;        // accidental → notehead
    double accidentalColumnGapSp = 0.1;  // between columns of accidentals in a chord
    double accidentalOverlapSp = 0.25;   // how far two accidentals in one column may overlap vertically
    double dotGapSp = 0.35;              // notehead or rest → first dot
    double dotDotGapSp = 0.25;           // between the dots of a double-dotted note
};

// How beams sit, in sp. LayoutProfile has no fields for these yet.
struct BeamSpacing
{
    double stemSp = 3.5;      // the shortest stem in a group: note → the beam's outer edge
    double clearanceSp = 2.0; // note → the innermost beam's near edge, at least
    double riseRatio = 0.5;   // how much of the interval between the end notes the beam follows
    double maxRiseSp = 1.0;   // the most a beam rises or falls from end to end
    double maxSlope = 0.25;   // and per sp of its length
};

struct SystemStaff
{
    NodeId staffId;
    double y = 0; // its top line, from the top line of the system's first staff
};

struct SystemInput
{
    const MusicData& music;
    const LayoutProfile& profile;
    const SmuflFont& font;
    std::vector<SystemStaff> staves; // in score order
    std::size_t firstMeasure = 0;    // index into music.measures
    std::size_t measureCount = 1;
    std::optional<double> width; // stretch or squeeze the notes to fill it; empty: natural width.
                                 // Never squeezed until symbols touch: a system that does not
                                 // fit then comes out wider (overfull, until the next reflow).
    HeaderSpacing header {};
    NoteSpacing notes {};
    BeamSpacing beams {};
};

// Throws std::invalid_argument for no staves, an unknown staff, or a measure range
// outside the score.
SystemLayout layoutSystem(const SystemInput& input);

// The natural width of a measure: as the first of a system (with the clef and key at
// its start), and after another measure. A system's natural width is its first
// measure's `first` plus the others' `other`.
struct MeasureWidth
{
    double first = 0;
    double other = 0;
};

// One per measure in input's range; input.width is ignored. Throws like layoutSystem.
std::vector<MeasureWidth> measureWidths(const SystemInput& input);

} // namespace msd
