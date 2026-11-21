#pragma once

#include <memory>
#include <optional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#if MSD_ENABLE_INSPECTOR
    #include <melatonin_inspector/melatonin_inspector.h>
#endif

#include "MusicFont.h"
#include "ScoreView.h"
#include "core/types/LayoutProfile.h"
#include "core/types/MusicData.h"

namespace msd
{

// The window's content: a toolbar to pick a score, staff and measures, and the
// score view below it.
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
    void relayout();
    void setStatus(const juce::String& text);
    double systemWidthSp() const;

    // Declared before scoreView, which keeps a reference to it.
    std::unique_ptr<MusicFont> font;
    LayoutProfile profile;
    std::optional<MusicData> music;
    std::vector<NodeId> staffIds; // by staffBox item ID - 1

    juce::TextButton openButton { "Open..." };
    juce::Label fileLabel;
    juce::ComboBox staffBox;
    juce::Label firstLabel { {}, "From" };
    juce::Slider firstSlider { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };
    juce::Label countLabel { {}, "Measures" };
    juce::Slider countSlider { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };
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
