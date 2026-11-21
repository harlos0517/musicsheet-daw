#pragma once

// Mirrors prototype-web/src/core/types/LayoutProfile.ts.
//
// The rules a layout is computed by. Page sizes are in millimetres; every other length
// is in staff spaces (sp, the distance between two staff lines), so a profile works at
// any staff size.
//
// The TS object literals without a name (page, spacing, …) become structs nested in
// LayoutProfile, so they are written LayoutProfile::Spacing and so on.

#include <map>
#include <optional>
#include <string>

namespace msd
{

enum class PlacedKind { Dynamic, Hairpin, Tempo, Text, Technique, Pedal };

enum class Side { Above, Below };

struct LayoutProfile
{
    struct Margins // mm
    {
        double top = 0;
        double bottom = 0;
        double left = 0;
        double right = 0;
    };

    struct Page
    {
        double widthMm = 0;
        double heightMm = 0;
        Margins margins;
    };

    struct Spacing
    {
        double quarterSp = 0;        // space given to a quarter note
        double durationRatio = 0;    // space grows by this for each doubling of duration, e.g. 1.4
        double minNoteSp = 0;        // no note gets less, however short
        double measurePaddingSp = 0; // after the barline, before the first note
    };

    struct Distances
    {
        double staffSp = 0;       // between staves of one track (piano)
        double trackSp = 0;       // between tracks
        double systemSp = 0;      // between systems
        double firstIndentSp = 0; // first system, room for full instrument names
    };

    struct Breaking
    {
        double lastSystemFillMin = 0; // 0 - 1; the last system is stretched to the full
                                      // width only when its notes take at least this share of it
    };

    struct Fonts
    {
        std::string music;
        std::string text;
    };

    struct Padding
    {
        double collisionSp = 0; // the least gap between two items pushed apart
    };

    std::string id;
    std::string name;
    std::optional<std::string> extends; // parent profile ID; fields set here replace the parent's
    Page page;
    double staffSpaceMm = 0; // the staff size
    Spacing spacing;
    Distances distances;
    Breaking breaking;
    std::map<PlacedKind, Side> placement; // default side of the staff; TS: Partial<Record<…>>
    Fonts fonts;
    Padding padding;
};

} // namespace msd
