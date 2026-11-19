#include "MainComponent.h"

namespace msd
{

MainComponent::MainComponent()
{
    setWantsKeyboardFocus(true);
    setSize(1200, 500);
}

MainComponent::~MainComponent() = default;

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::white);
    g.setColour(juce::Colours::grey);
    g.setFont(juce::FontOptions(16.0f));
    g.drawText("Score view goes here", getLocalBounds(), juce::Justification::centred);
}

void MainComponent::resized() {}

bool MainComponent::keyPressed(const juce::KeyPress& key)
{
#if MSD_ENABLE_INSPECTOR
    if (key == juce::KeyPress('i', juce::ModifierKeys::commandModifier, 0))
    {
        if (inspector == nullptr)
        {
            inspector = std::make_unique<melatonin::Inspector>(*getTopLevelComponent());
            inspector->onClose = [this] { inspector.reset(); };
        }
        inspector->setVisible(true);
        return true;
    }
#else
    juce::ignoreUnused(key);
#endif
    return false;
}

} // namespace msd
