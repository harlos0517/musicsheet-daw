#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#if MSD_ENABLE_INSPECTOR
    #include <melatonin_inspector/melatonin_inspector.h>
#endif

namespace msd
{

// Placeholder window content. The score view replaces the paint() body in step 5.
class MainComponent final : public juce::Component
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;

private:
#if MSD_ENABLE_INSPECTOR
    std::unique_ptr<melatonin::Inspector> inspector;
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

} // namespace msd
