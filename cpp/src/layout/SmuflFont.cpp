#include "layout/SmuflFont.h"

#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

#include "core/json/Load.h" // LoadError

namespace msd
{
namespace
{

using Json = nlohmann::json;

// SMuFL stores points as [x, y] with y up; flip y.
std::optional<Point> pointFrom(const Json& j)
{
    if (!j.is_array() || j.size() != 2 || !j[0].is_number() || !j[1].is_number())
        return std::nullopt;
    return Point { j[0].get<double>(), -j[1].get<double>() };
}

void readDefault(const Json& defaults, const char* key, double& out)
{
    const auto it = defaults.find(key);
    if (it != defaults.end() && it->is_number())
        out = it->get<double>();
}

} // namespace

SmuflFont SmuflFont::parse(std::string_view metadataJson)
{
    Json j;
    try
    {
        j = Json::parse(metadataJson.begin(), metadataJson.end());
    }
    catch (const Json::parse_error& e)
    {
        throw LoadError(std::string("SMuFL metadata: invalid JSON: ") + e.what());
    }
    if (!j.is_object())
        throw LoadError("SMuFL metadata: expected an object");

    SmuflFont font;
    if (const auto it = j.find("fontName"); it != j.end() && it->is_string())
        font.fontName = it->get<std::string>();

    if (const auto it = j.find("engravingDefaults"); it != j.end() && it->is_object())
    {
        EngravingDefaults& d = font.defaults;
        readDefault(*it, "staffLineThickness", d.staffLineThickness);
        readDefault(*it, "stemThickness", d.stemThickness);
        readDefault(*it, "legerLineThickness", d.legerLineThickness);
        readDefault(*it, "legerLineExtension", d.legerLineExtension);
        readDefault(*it, "thinBarlineThickness", d.thinBarlineThickness);
        readDefault(*it, "thickBarlineThickness", d.thickBarlineThickness);
        readDefault(*it, "barlineSeparation", d.barlineSeparation);
        readDefault(*it, "thinThickBarlineSeparation", d.thinThickBarlineSeparation);
    }

    const auto boxes = j.find("glyphBBoxes");
    if (boxes == j.end() || !boxes->is_object())
        throw LoadError("SMuFL metadata: no glyphBBoxes");
    for (const auto& [glyph, box] : boxes->items())
    {
        if (!box.is_object() || !box.contains("bBoxNE") || !box.contains("bBoxSW"))
            continue;
        const std::optional<Point> ne = pointFrom(box["bBoxNE"]); // y already flipped:
        const std::optional<Point> sw = pointFrom(box["bBoxSW"]); // ne is the top right
        if (ne && sw)
            font.boxes.emplace(glyph, Rect { sw->x, ne->y, ne->x, sw->y });
    }

    if (const auto it = j.find("glyphsWithAnchors"); it != j.end() && it->is_object())
    {
        for (const auto& [glyph, glyphAnchors] : it->items())
        {
            if (!glyphAnchors.is_object())
                continue;
            AnchorMap& map = font.anchors[glyph];
            for (const auto& [anchorName, value] : glyphAnchors.items())
                if (const std::optional<Point> point = pointFrom(value))
                    map.emplace(anchorName, *point);
        }
    }
    return font;
}

SmuflFont SmuflFont::load(const std::filesystem::path& metadataFile)
{
    std::ifstream in(metadataFile, std::ios::binary);
    if (!in)
        throw LoadError("can't open " + metadataFile.string());
    const std::string text { std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
    try
    {
        return parse(text);
    }
    catch (const LoadError& e)
    {
        throw LoadError(metadataFile.string() + ": " + e.what());
    }
}

std::optional<Rect> SmuflFont::bbox(std::string_view glyph) const
{
    const auto it = boxes.find(glyph);
    if (it == boxes.end())
        return std::nullopt;
    return it->second;
}

std::optional<Point> SmuflFont::anchor(std::string_view glyph, std::string_view anchorName) const
{
    const auto glyphIt = anchors.find(glyph);
    if (glyphIt == anchors.end())
        return std::nullopt;
    const auto it = glyphIt->second.find(anchorName);
    if (it == glyphIt->second.end())
        return std::nullopt;
    return it->second;
}

} // namespace msd
