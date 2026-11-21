#pragma once

// Paints a SystemLayout: each box's glyph or line, nothing else. All the decisions
// about where things go were made by layout; this only scales sp to pixels.

#include <functional>
#include <optional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "MusicFont.h"
#include "layout/SystemLayout.h"

namespace msd
{

class ScoreView final : public juce::Component
{
public:
    explicit ScoreView(MusicFont& font);

    void setSystem(SystemLayout layout);
    void clear();
    void setZoom(float pixelsPerSp);
    void setShowBounds(bool show);

    // Called with "nodeId · part" for the box under the mouse, or "" when there's none.
    std::function<void(const juce::String&)> onHover;

    void paint(juce::Graphics&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;

private:
    juce::AffineTransform spToPixels() const;
    void updateSize();
    void setHovered(std::optional<std::size_t> index);

    MusicFont& font;
    std::optional<SystemLayout> system;
    float zoom = 12.0f; // pixels per sp
    bool showBounds = false;
    std::optional<std::size_t> hovered;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ScoreView)
};

juce::String partName(Part part);

} // namespace msd
