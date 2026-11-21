#include <juce_gui_basics/juce_gui_basics.h>

#include "MainComponent.h"

namespace msd
{

class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow(const juce::String& name, const juce::String& scoreToOpen)
        : DocumentWindow(name,
                         juce::Desktop::getInstance().getDefaultLookAndFeel().findColour(
                             juce::ResizableWindow::backgroundColourId),
                         DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar(true);
        setContentOwned(new MainComponent(scoreToOpen), true);
        setResizable(true, true);
        centreWithSize(getWidth(), getHeight());
        setVisible(true);
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

class Application final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return true; }

    // Command line: [score.music.json] [staff ID] [first measure].
    void initialise(const juce::String& commandLine) override
    {
        mainWindow = std::make_unique<MainWindow>(getApplicationName(), commandLine.trim());
    }

    void shutdown() override { mainWindow.reset(); }

    void systemRequestedQuit() override { quit(); }

private:
    std::unique_ptr<MainWindow> mainWindow;
};

} // namespace msd

START_JUCE_APPLICATION(msd::Application)
