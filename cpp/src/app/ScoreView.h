#pragma once

// Paints pages: each system's boxes, glyphs and lines, nothing else. All the decisions
// about where things go were made by layout; this only scales sp to pixels and stacks
// the pages one below the other.

#include <functional>
#include <optional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "MusicFont.h"
#include "layout/PageLayout.h"

namespace msd
{

class ScoreView final : public juce::Component
{
public:
    explicit ScoreView(MusicFont& font);

    void setPages(std::vector<PageLayout> pages);
    void clear();
    void setZoom(float pixelsPerSp);
    void setShowBounds(bool show);

    // The top of a page, in pixels, for scrolling to it.
    int pageTop(std::size_t page) const;

    // Called with "nodeId · part" for the box under the mouse, or "" when there's none.
    std::function<void(const juce::String&)> onHover;

    void paint(juce::Graphics&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;

private:
    struct BoxRef
    {
        std::size_t page;
        std::size_t system;
        std::size_t box;
        bool operator==(const BoxRef&) const = default;
    };

    void updateSize();
    Point pageOrigin(std::size_t page) const; // in sp, in this component
    void paintSystem(juce::Graphics& g, const PlacedSystem& placed, Point pageAt,
                     std::optional<std::size_t> hoveredBox);
    void setHovered(std::optional<BoxRef> box);
    const LayoutBox& boxAt(const BoxRef& ref) const;

    MusicFont& font;
    std::vector<PageLayout> pages;
    std::vector<double> pageTops; // in sp
    double widthSp = 0;
    double heightSp = 0;
    float zoom = 6.0f; // pixels per sp
    bool showBounds = false;
    std::optional<BoxRef> hovered;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ScoreView)
};

juce::String partName(Part part);

} // namespace msd
