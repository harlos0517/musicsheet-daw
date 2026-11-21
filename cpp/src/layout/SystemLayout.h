#pragma once

// Lays out one system of one staff: the result is a list of boxes, each saying which
// MusicData node it came from and what to draw. Nothing here knows about JUCE or a
// window; painting (cpp/src/app) only draws what the boxes say.
//
// Which measures make up the system is the caller's choice. Later it comes from
// Layout.flow; this function only spaces the music inside the system it is given,
// so editing a note never moves a system.
//
// Coordinates are in staff spaces (sp): x from the system's left edge, y from the top
// staff line, down positive.

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
    Barline,       // node: the measure
    Clef,          // node: the clef
    KeySignature,  // node: the key signature; one box per accidental
    TimeSignature, // node: the time signature; one box per digit
    Notehead,      // node: the note
    Accidental,    // node: the note; with parentheses, one box per glyph
    Dot,           // node: the note or rest; one box per dot
    Stem,          // node: the chord
    Flag,          // node: the chord
    Rest,          // node: the rest
};

// A SMuFL glyph, drawn with its origin at `origin`.
struct GlyphShape
{
    std::string glyph; // SMuFL name, e.g. "noteheadBlack"
    Point origin;
};

// A straight line of a given thickness, with square ends at `from` and `to`.
struct LineShape
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
    std::variant<GlyphShape, LineShape> shape;
};

struct MeasureSlot
{
    NodeId measureId;
    double left = 0;  // the previous barline, or the system's start
    double right = 0; // this measure's barline
};

struct SystemLayout
{
    NodeId staffId;
    int staffLines = 5;
    double width = 0;
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

struct SystemInput
{
    const MusicData& music;
    const LayoutProfile& profile;
    const SmuflFont& font;
    NodeId staffId;
    std::size_t firstMeasure = 0; // index into music.measures
    std::size_t measureCount = 1;
    std::optional<double> width;  // stretch or squeeze the notes to fill it; empty: natural width.
                                  // Never squeezed until symbols touch: a system that does not
                                  // fit then comes out wider (overfull, until the next reflow).
    HeaderSpacing header {};
    NoteSpacing notes {};
};

// Throws std::invalid_argument for an unknown staff or a measure range outside the score.
SystemLayout layoutSystem(const SystemInput& input);

} // namespace msd
