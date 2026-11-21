#pragma once

// SMuFL glyph name → Unicode codepoint, from SMuFL's metadata/glyphnames.json
// (e.g. "gClef" → U+E050). The codepoints are fixed by the SMuFL standard, so one
// table serves every SMuFL font.

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace msd
{

class SmuflGlyphNames
{
public:
    // Both throw msd::LoadError when the file can't be read.
    static SmuflGlyphNames parse(std::string_view glyphnamesJson);
    static SmuflGlyphNames load(const std::filesystem::path& glyphnamesFile);

    std::optional<char32_t> codepoint(std::string_view glyph) const;
    std::size_t size() const { return codepoints.size(); }

private:
    std::map<std::string, char32_t, std::less<>> codepoints;
};

} // namespace msd
