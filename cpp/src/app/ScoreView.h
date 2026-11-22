#pragma once

// Paints pages: each system's boxes, glyphs and lines, nothing else. All the decisions
// about where things go were made by layout; this only scales sp to pixels and stacks
// the pages one below the other.

#include <functional>
#include <optional>
#include <unordered_set>
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
    std::vector<PageLayout> takePages(); // leaves the view empty until setPages
    void clear();
    void setZoom(float pixelsPerSp);
    void setShowBounds(bool show);

    // The top of a page, in pixels, for scrolling to it.
    int pageTop(std::size_t page) const;

    // Called with "nodeId · part" for the box under the mouse, or "" when there's none.
    std::function<void(const juce::String&)> onHover;

    // Called on a click with the note whose notehead or accidental was clicked, or ""
    // for anything else, and whether Shift was down.
    std::function<void(const NodeId& noteId, bool shift)> onNoteClick;

    // The notes drawn as selected: their noteheads and accidentals.
    void setSelected(std::unordered_set<NodeId> notes);

    // Where playback is: a line across the system, `fraction` of the way through the
    // measure (from its start barline, or the system's start, to its end barline).
    struct PlayCursor
    {
        NodeId measureId;
        double fraction = 0;
    };
    void setCursor(std::optional<PlayCursor> cursor);
    juce::Rectangle<int> cursorBounds() const; // in pixels; empty when there is none

    void paint(juce::Graphics&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
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
    std::optional<BoxRef> boxUnder(juce::Point<float> position) const; // in pixels
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
    std::unordered_set<NodeId> selected;
    std::optional<Rect> cursor; // in sp, in this component

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ScoreView)
};

juce::String partName(Part part);

} // namespace msd
