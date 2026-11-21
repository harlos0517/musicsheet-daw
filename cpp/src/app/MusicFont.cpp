#include "MusicFont.h"

#include <cmath>
#include <utility>

namespace msd
{
namespace
{

// Outlines are taken at this font height, then scaled to sp.
constexpr float referenceHeight = 100.0f;

} // namespace

MusicFont::MusicFont(const void* fontData, std::size_t fontSize, SmuflFont metadata, SmuflGlyphNames glyphNames)
    : typeface(juce::Typeface::createSystemTypefaceFor(fontData, fontSize))
    , smufl(std::move(metadata))
    , names(std::move(glyphNames))
{
    // SMuFL defines the em as 4 staff spaces, but JUCE sizes fonts in its own units.
    // Rather than rely on either, measure one glyph's outline against the width the
    // metadata gives for it: that ratio converts every outline to sp.
    const std::optional<char32_t> notehead = names.codepoint("noteheadBlack");
    const std::optional<Rect> box = smufl.bbox("noteheadBlack");
    if (typeface != nullptr && notehead && box && box->width() > 0)
    {
        const float measured = outlineAtReferenceHeight(*notehead).getBounds().getWidth();
        if (measured > 0)
            spPerUnit = static_cast<float>(box->width()) / measured;
    }
    jassert(spPerUnit > 0); // the font or its metadata didn't load

    // In Debug, check the scale against a tall glyph too.
    if (const auto clef = names.codepoint("gClef"); clef && spPerUnit > 0)
        if (const auto clefBox = smufl.bbox("gClef"))
        {
            const float height = outlineAtReferenceHeight(*clef).getBounds().getHeight() * spPerUnit;
            if (std::abs(height - static_cast<float>(clefBox->height())) > 0.05f * static_cast<float>(clefBox->height()))
                DBG("MusicFont: gClef outline is " << height << " sp tall, metadata says " << clefBox->height());
        }
}

juce::Path MusicFont::outlineAtReferenceHeight(char32_t codepoint) const
{
    juce::GlyphArrangement arrangement;
    arrangement.addLineOfText(juce::Font(juce::FontOptions {}.withTypeface(typeface).withHeight(referenceHeight)),
                              juce::String::charToString(static_cast<juce::juce_wchar>(codepoint)),
                              0.0f, 0.0f); // pen at the origin, on the baseline
    juce::Path path;
    arrangement.createPath(path);
    return path;
}

const juce::Path& MusicFont::outline(const std::string& glyph)
{
    if (const auto it = cache.find(glyph); it != cache.end())
        return it->second;

    juce::Path path;
    if (const std::optional<char32_t> codepoint = names.codepoint(glyph); codepoint && spPerUnit > 0)
    {
        path = outlineAtReferenceHeight(*codepoint);
        path.applyTransform(juce::AffineTransform::scale(spPerUnit));
    }
    return cache.emplace(glyph, std::move(path)).first->second;
}

} // namespace msd
