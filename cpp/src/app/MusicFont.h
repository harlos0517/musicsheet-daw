#pragma once

// A SMuFL music font for painting: the font file for outlines, plus its metadata and
// the SMuFL glyph names to find each glyph. Outlines come back in staff spaces with
// the glyph's origin at (0, 0) and y down, the same units layout works in.

#include <map>
#include <string>

#include <juce_graphics/juce_graphics.h>

#include "layout/SmuflFont.h"
#include "layout/SmuflGlyphNames.h"

namespace msd
{

class MusicFont
{
public:
    MusicFont(const void* fontData, std::size_t fontSize, SmuflFont metadata, SmuflGlyphNames names);

    const SmuflFont& metadata() const { return smufl; }

    // The glyph's outline in sp. Empty when the glyph has no codepoint or the font lacks it.
    const juce::Path& outline(const std::string& glyph);

private:
    juce::Path outlineAtReferenceHeight(char32_t codepoint) const;

    juce::Typeface::Ptr typeface;
    SmuflFont smufl;
    SmuflGlyphNames names;
    float spPerUnit = 0; // outline units at the reference height → sp
    std::map<std::string, juce::Path> cache;
};

} // namespace msd
