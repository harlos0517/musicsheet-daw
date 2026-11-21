#include "layout/SmuflGlyphNames.h"

#include <charconv>
#include <fstream>
#include <iterator>

#include <nlohmann/json.hpp>

#include "core/json/Load.h" // LoadError

namespace msd
{
namespace
{

// "U+E050" → 0xE050
std::optional<char32_t> parseCodepoint(std::string_view text)
{
    if (text.size() < 3 || text.substr(0, 2) != "U+")
        return std::nullopt;
    unsigned long value = 0;
    const char* first = text.data() + 2;
    const char* last = text.data() + text.size();
    const auto [end, error] = std::from_chars(first, last, value, 16);
    if (error != std::errc {} || end != last || value > 0x10FFFF)
        return std::nullopt;
    return static_cast<char32_t>(value);
}

} // namespace

SmuflGlyphNames SmuflGlyphNames::parse(std::string_view glyphnamesJson)
{
    nlohmann::json j;
    try
    {
        j = nlohmann::json::parse(glyphnamesJson.begin(), glyphnamesJson.end());
    }
    catch (const nlohmann::json::parse_error& e)
    {
        throw LoadError(std::string("glyphnames: invalid JSON: ") + e.what());
    }
    if (!j.is_object())
        throw LoadError("glyphnames: expected an object");

    SmuflGlyphNames names;
    for (const auto& [glyph, entry] : j.items())
    {
        if (!entry.is_object())
            continue;
        const auto it = entry.find("codepoint");
        if (it == entry.end() || !it->is_string())
            continue;
        if (const std::optional<char32_t> codepoint = parseCodepoint(it->get_ref<const std::string&>()))
            names.codepoints.emplace(glyph, *codepoint);
    }
    return names;
}

SmuflGlyphNames SmuflGlyphNames::load(const std::filesystem::path& glyphnamesFile)
{
    std::ifstream in(glyphnamesFile, std::ios::binary);
    if (!in)
        throw LoadError("can't open " + glyphnamesFile.string());
    const std::string text { std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
    return parse(text);
}

std::optional<char32_t> SmuflGlyphNames::codepoint(std::string_view glyph) const
{
    const auto it = codepoints.find(glyph);
    if (it == codepoints.end())
        return std::nullopt;
    return it->second;
}

} // namespace msd
