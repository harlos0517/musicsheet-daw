#pragma once

#include <memory>
#include <optional>

#include <juce_gui_basics/juce_gui_basics.h>

#if MSD_ENABLE_INSPECTOR
    #include <melatonin_inspector/melatonin_inspector.h>
#endif

#include "MusicFont.h"
#include "ScoreView.h"
#include "core/types/LayoutSetup.h"
#include "core/types/MusicData.h"

namespace msd
{

// The window's content: a toolbar to open a score and reflow it, and its pages below.
class MainComponent final : public juce::Component
{
public:
    explicit MainComponent(const juce::String& scoreToOpen);
    ~MainComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;

private:
    void chooseScore();
    void openScore(const juce::File& file);
    void reflowScore();
    void relayout();
    void setStatus(const juce::String& text);
    const LayoutProfile& profile() const; // the layout's, or the first one

    // Profile IDs in resources/default.layout.json.
    static constexpr const char* defaultProfileId = "default";
    static constexpr const char* largeScoreProfileId = "large-score"; // 1 mm staves

    // Declared before scoreView, which keeps a reference to it.
    std::unique_ptr<MusicFont> font;
    LayoutSetup setup; // the default profile and the full-score layout, with its flow
    std::optional<MusicData> music;
    double reflowMs = 0;

    juce::TextButton openButton { "Open..." };
    juce::Label fileLabel;
    juce::TextButton reflowButton { "Reflow" };
    juce::Label zoomLabel { {}, "Zoom" };
    juce::Slider zoomSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::ToggleButton boundsToggle { "Boxes" };
    juce::Label statusLabel;
    juce::String status; // shown when nothing is hovered

    ScoreView scoreView;
    juce::Viewport viewport;

    std::unique_ptr<juce::FileChooser> chooser;

#if MSD_ENABLE_INSPECTOR
    std::unique_ptr<melatonin::Inspector> inspector;
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

} // namespace msd
