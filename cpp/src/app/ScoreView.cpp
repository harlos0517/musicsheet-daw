#include "ScoreView.h"

#include <cmath>
#include <utility>

namespace msd
{
namespace
{

// Room around the system, in sp: ledger lines and stems reach above and below the staff.
constexpr float marginXSp = 4.0f;
constexpr float marginAboveSp = 8.0f;
constexpr float marginBelowSp = 8.0f;

juce::Rectangle<float> toJuce(const Rect& r)
{
    return { static_cast<float>(r.left), static_cast<float>(r.top), static_cast<float>(r.width()),
             static_cast<float>(r.height()) };
}

} // namespace

juce::String partName(Part part)
{
    switch (part)
    {
    case Part::StaffLine: return "staff line";
    case Part::LedgerLine: return "ledger line";
    case Part::Barline: return "barline";
    case Part::Clef: return "clef";
    case Part::KeySignature: return "key signature";
    case Part::TimeSignature: return "time signature";
    case Part::Notehead: return "notehead";
    case Part::Stem: return "stem";
    case Part::Rest: return "rest";
    }
    return "?";
}

ScoreView::ScoreView(MusicFont& musicFont)
    : font(musicFont)
{
    setOpaque(true);
}

void ScoreView::setSystem(SystemLayout layout)
{
    system = std::move(layout);
    hovered.reset();
    updateSize();
    repaint();
}

void ScoreView::clear()
{
    system.reset();
    hovered.reset();
    repaint();
}

void ScoreView::setZoom(float pixelsPerSp)
{
    zoom = pixelsPerSp;
    updateSize();
    repaint();
}

void ScoreView::setShowBounds(bool show)
{
    showBounds = show;
    repaint();
}

juce::AffineTransform ScoreView::spToPixels() const
{
    return juce::AffineTransform::translation(marginXSp, marginAboveSp).scaled(zoom);
}

void ScoreView::updateSize()
{
    if (!system)
        return;
    const float widthSp = static_cast<float>(system->width) + 2 * marginXSp;
    const float heightSp = static_cast<float>(system->staffLines - 1) + marginAboveSp + marginBelowSp;
    setSize(juce::roundToInt(widthSp * zoom), juce::roundToInt(heightSp * zoom));
}

void ScoreView::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::white);
    if (!system)
        return;

    const juce::AffineTransform view = spToPixels();
    for (std::size_t i = 0; i < system->boxes.size(); ++i)
    {
        const LayoutBox& box = system->boxes[i];
        g.setColour(hovered == i ? juce::Colour(0xff1565c0) : juce::Colours::black);

        if (const auto* glyph = std::get_if<GlyphShape>(&box.shape))
        {
            const juce::AffineTransform at = juce::AffineTransform::translation(static_cast<float>(glyph->origin.x),
                                                                                static_cast<float>(glyph->origin.y))
                                                 .followedBy(view);
            g.fillPath(font.outline(glyph->glyph), at);
        }
        else if (const auto* line = std::get_if<LineShape>(&box.shape))
        {
            juce::Path path; // a rectangle around the segment, square ends
            path.addLineSegment({ static_cast<float>(line->from.x), static_cast<float>(line->from.y),
                                  static_cast<float>(line->to.x), static_cast<float>(line->to.y) },
                                static_cast<float>(line->thickness));
            g.fillPath(path, view);
        }
    }

    if (showBounds)
    {
        g.setColour(juce::Colour(0x80e53935));
        for (const LayoutBox& box : system->boxes)
            if (box.part != Part::StaffLine)
                g.drawRect(toJuce(box.bounds).transformedBy(view), 1.0f);
    }
}

void ScoreView::mouseMove(const juce::MouseEvent& event)
{
    if (!system)
        return;
    // Back from pixels to sp, then the topmost box there (the last one painted).
    juce::Point<float> sp = event.position;
    spToPixels().inverted().transformPoint(sp.x, sp.y);
    std::optional<std::size_t> found;
    for (std::size_t i = system->boxes.size(); i-- > 0;)
    {
        const Rect& r = system->boxes[i].bounds;
        if (sp.x >= r.left && sp.x <= r.right && sp.y >= r.top && sp.y <= r.bottom)
        {
            found = i;
            break;
        }
    }
    setHovered(found);
}

void ScoreView::mouseExit(const juce::MouseEvent&) { setHovered(std::nullopt); }

void ScoreView::setHovered(std::optional<std::size_t> index)
{
    if (index == hovered)
        return;
    hovered = index;
    repaint();
    if (onHover)
    {
        if (hovered && system)
        {
            const LayoutBox& box = system->boxes[*hovered];
            onHover(juce::String(box.nodeId) + juce::String::fromUTF8(" \xc2\xb7 ") + partName(box.part));
        }
        else
        {
            onHover({});
        }
    }
}

} // namespace msd
