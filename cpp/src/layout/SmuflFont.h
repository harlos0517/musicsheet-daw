#pragma once

// What layout needs to know about a SMuFL music font, read from its metadata JSON
// (e.g. leland_metadata.json). Any SMuFL font with metadata can be swapped in.
//
// SMuFL metadata measures in staff spaces with y pointing up; everything this class
// returns is converted to y pointing down, like the rest of layout.
// Spec: https://w3c.github.io/smufl/latest/specification/font-specific-metadata.html

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "layout/Geometry.h"

namespace msd
{

// The engravingDefaults this layout uses, in sp. A font that leaves one out gets
// the value given here.
struct EngravingDefaults
{
    double staffLineThickness = 0.13;
    double stemThickness = 0.12;
    double legerLineThickness = 0.16;
    double legerLineExtension = 0.4;
    double thinBarlineThickness = 0.16;
    double thickBarlineThickness = 0.5;
    double barlineSeparation = 0.4;          // between the two lines of a double barline
    double thinThickBarlineSeparation = 0.4; // between the lines of a final barline
};

class SmuflFont
{
public:
    // Both throw msd::LoadError when the metadata can't be read.
    static SmuflFont parse(std::string_view metadataJson);
    static SmuflFont load(const std::filesystem::path& metadataFile);

    const std::string& name() const { return fontName; }
    const EngravingDefaults& engravingDefaults() const { return defaults; }

    bool hasGlyph(std::string_view glyph) const { return boxes.contains(glyph); }

    // The glyph's bounding box around its origin. Empty when the font has no such glyph.
    std::optional<Rect> bbox(std::string_view glyph) const;

    // A named anchor (e.g. "stemUpSE") relative to the glyph's origin, if the font has it.
    std::optional<Point> anchor(std::string_view glyph, std::string_view anchorName) const;

private:
    // std::less<> lets the maps be searched with a std::string_view without copying it.
    using AnchorMap = std::map<std::string, Point, std::less<>>;

    std::string fontName;
    EngravingDefaults defaults;
    std::map<std::string, Rect, std::less<>> boxes;
    std::map<std::string, AnchorMap, std::less<>> anchors;
};

} // namespace msd
