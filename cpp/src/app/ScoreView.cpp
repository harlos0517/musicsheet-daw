#include "ScoreView.h"

#include <cmath>
#include <utility>

namespace msd
{
namespace
{

constexpr double gapSp = 6.0; // around and between pages

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
    case Part::Brace: return "brace";
    case Part::Clef: return "clef";
    case Part::KeySignature: return "key signature";
    case Part::TimeSignature: return "time signature";
    case Part::Notehead: return "notehead";
    case Part::Accidental: return "accidental";
    case Part::Dot: return "dot";
    case Part::Stem: return "stem";
    case Part::Flag: return "flag";
    case Part::Beam: return "beam";
    case Part::Rest: return "rest";
    }
    return "?";
}

ScoreView::ScoreView(MusicFont& musicFont)
    : font(musicFont)
{
    setOpaque(true);
}

void ScoreView::setPages(std::vector<PageLayout> newPages)
{
    pages = std::move(newPages);
    pageTops.clear();
    widthSp = 0;
    double y = gapSp;
    for (const PageLayout& page : pages)
    {
        pageTops.push_back(y);
        y += page.heightSp + gapSp;
        widthSp = std::max(widthSp, page.widthSp + 2 * gapSp);
    }
    heightSp = y;
    hovered.reset();
    updateSize();
    repaint();
}

void ScoreView::clear() { setPages({}); }

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

int ScoreView::pageTop(std::size_t page) const
{
    return page < pageTops.size() ? juce::roundToInt((pageTops[page] - gapSp / 2) * zoom) : 0;
}

void ScoreView::updateSize()
{
    setSize(juce::roundToInt(std::max(widthSp, 1.0) * zoom), juce::roundToInt(std::max(heightSp, 1.0) * zoom));
}

Point ScoreView::pageOrigin(std::size_t page) const { return { (widthSp - pages[page].widthSp) / 2, pageTops[page] }; }

void ScoreView::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff5f6b73));
    const juce::Rectangle<float> clip = g.getClipBounds().toFloat();

    for (std::size_t p = 0; p < pages.size(); ++p)
    {
        const PageLayout& page = pages[p];
        const Point at = pageOrigin(p);
        const juce::Rectangle<float> paper(static_cast<float>(at.x * zoom), static_cast<float>(at.y * zoom),
                                           static_cast<float>(page.widthSp * zoom),
                                           static_cast<float>(page.heightSp * zoom));
        if (!paper.intersects(clip))
            continue;
        g.setColour(juce::Colours::black.withAlpha(0.25f));
        g.fillRect(paper.translated(3, 3));
        g.setColour(juce::Colours::white);
        g.fillRect(paper);

        for (std::size_t s = 0; s < page.systems.size(); ++s)
        {
            const std::optional<std::size_t> hoveredBox =
                hovered && hovered->page == p && hovered->system == s ? std::optional(hovered->box) : std::nullopt;
            paintSystem(g, page.systems[s], at, hoveredBox);
        }
    }
}

void ScoreView::paintSystem(juce::Graphics& g, const PlacedSystem& placed, Point pageAt,
                            std::optional<std::size_t> hoveredBox)
{
    const juce::AffineTransform view =
        juce::AffineTransform::translation(static_cast<float>(pageAt.x + placed.origin.x),
                                           static_cast<float>(pageAt.y + placed.origin.y))
            .scaled(zoom);
    const std::vector<LayoutBox>& boxes = placed.system.boxes;
    for (std::size_t i = 0; i < boxes.size(); ++i)
    {
        const LayoutBox& box = boxes[i];
        g.setColour(hoveredBox == i ? juce::Colour(0xff1565c0) : juce::Colours::black);

        if (const auto* glyph = std::get_if<GlyphShape>(&box.shape))
        {
            const juce::AffineTransform at =
                juce::AffineTransform::scale(static_cast<float>(glyph->scale))
                    .translated(static_cast<float>(glyph->origin.x), static_cast<float>(glyph->origin.y))
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
        else if (const auto* beam = std::get_if<BeamShape>(&box.shape))
        {
            const float half = static_cast<float>(beam->thickness / 2);
            const float x1 = static_cast<float>(beam->from.x);
            const float y1 = static_cast<float>(beam->from.y);
            const float x2 = static_cast<float>(beam->to.x);
            const float y2 = static_cast<float>(beam->to.y);
            juce::Path path; // vertical ends, `thickness` tall
            path.startNewSubPath(x1, y1 - half);
            path.lineTo(x2, y2 - half);
            path.lineTo(x2, y2 + half);
            path.lineTo(x1, y1 + half);
            path.closeSubPath();
            g.fillPath(path, view);
        }
    }

    if (showBounds)
    {
        g.setColour(juce::Colour(0x80e53935));
        for (const LayoutBox& box : boxes)
            if (box.part != Part::StaffLine)
                g.drawRect(toJuce(box.bounds).transformedBy(view), 1.0f);
    }
}

void ScoreView::mouseMove(const juce::MouseEvent& event)
{
    // Back from pixels to sp, then the topmost box there (the last one painted).
    const Point sp { event.position.x / zoom, event.position.y / zoom };
    for (std::size_t p = 0; p < pages.size(); ++p)
    {
        const Point at = pageOrigin(p);
        if (sp.y < at.y || sp.y > at.y + pages[p].heightSp)
            continue;
        const std::vector<PlacedSystem>& systems = pages[p].systems;
        for (std::size_t s = systems.size(); s-- > 0;)
        {
            const Point local { sp.x - at.x - systems[s].origin.x, sp.y - at.y - systems[s].origin.y };
            const std::vector<LayoutBox>& boxes = systems[s].system.boxes;
            for (std::size_t i = boxes.size(); i-- > 0;)
            {
                const Rect& r = boxes[i].bounds;
                if (boxes[i].part != Part::StaffLine && local.x >= r.left && local.x <= r.right && local.y >= r.top
                    && local.y <= r.bottom)
                {
                    setHovered(BoxRef { p, s, i });
                    return;
                }
            }
        }
    }
    setHovered(std::nullopt);
}

void ScoreView::mouseExit(const juce::MouseEvent&) { setHovered(std::nullopt); }

const LayoutBox& ScoreView::boxAt(const BoxRef& ref) const
{
    return pages[ref.page].systems[ref.system].system.boxes[ref.box];
}

void ScoreView::setHovered(std::optional<BoxRef> box)
{
    if (box == hovered)
        return;
    hovered = box;
    repaint();
    if (onHover)
    {
        if (hovered)
        {
            const LayoutBox& found = boxAt(*hovered);
            onHover(juce::String(found.nodeId) + juce::String::fromUTF8(" \xc2\xb7 ") + partName(found.part) + " (page "
                    + juce::String(hovered->page + 1) + ")");
        }
        else
        {
            onHover({});
        }
    }
}

} // namespace msd
