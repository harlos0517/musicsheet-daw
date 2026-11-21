#include "MainComponent.h"

#include <algorithm>
#include <exception>
#include <string_view>

#include "BinaryData.h"
#include "core/json/Load.h"
#include "layout/PageLayout.h"

namespace msd
{
namespace
{

std::string_view binary(const char* data, int size) { return { data, static_cast<std::size_t>(size) }; }

std::unique_ptr<MusicFont> makeLeland()
{
    return std::make_unique<MusicFont>(
        BinaryData::Leland_otf, static_cast<std::size_t>(BinaryData::Leland_otfSize),
        SmuflFont::parse(binary(BinaryData::leland_metadata_json, BinaryData::leland_metadata_jsonSize)),
        SmuflGlyphNames::parse(binary(BinaryData::glyphnames_json, BinaryData::glyphnames_jsonSize)));
}

LayoutSetup defaultSetup()
{
    return parseLayoutSetup(binary(BinaryData::default_layout_json, BinaryData::default_layout_jsonSize));
}

double millisecondsSince(double start) { return juce::Time::getMillisecondCounterHiRes() - start; }

} // namespace

MainComponent::MainComponent(const juce::String& scoreToOpen)
    : font(makeLeland())
    , setup(defaultSetup())
    , scoreView(*font)
{
    setWantsKeyboardFocus(true);

    openButton.onClick = [this] { chooseScore(); };
    fileLabel.setText("No score", juce::dontSendNotification);
    fileLabel.setMinimumHorizontalScale(0.7f);
    reflowButton.setTooltip("Break the music into systems and pages again");
    reflowButton.onClick = [this] {
        reflowScore();
        relayout();
    };
    zoomSlider.setRange(2, 30, 0.5);
    zoomSlider.setValue(6, juce::dontSendNotification);
    zoomSlider.onValueChange = [this] { scoreView.setZoom(static_cast<float>(zoomSlider.getValue())); };
    boundsToggle.onClick = [this] { scoreView.setShowBounds(boundsToggle.getToggleState()); };

    scoreView.onHover = [this](const juce::String& text) {
        statusLabel.setText(text.isEmpty() ? status : text, juce::dontSendNotification);
    };
    viewport.setViewedComponent(&scoreView, false);
    viewport.setScrollBarsShown(true, true);

    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &openButton, &fileLabel, &reflowButton, &zoomLabel, &zoomSlider, &boundsToggle, &statusLabel, &viewport })
        addAndMakeVisible(c);

    setSize(1000, 800);

    // Command line: [score.music.json] [page to show]. Without a score, in development,
    // an exported example.
    const juce::StringArray args = juce::StringArray::fromTokens(scoreToOpen, true);
    juce::File start = juce::File(MSD_EXPORTED_SCORES_DIR).getChildFile("melody.music.json");
    if (!args.isEmpty())
        start = juce::File::getCurrentWorkingDirectory().getChildFile(args[0].unquoted());
    if (!start.existsAsFile())
    {
        setStatus("Open a .music.json file (pnpm midi writes them to prototype-web/midi-out)");
        return;
    }
    openScore(start);
    if (args.size() > 1)
    {
        const int page = std::max(1, args[1].unquoted().getIntValue());
        viewport.setViewPosition(0, scoreView.pageTop(static_cast<std::size_t>(page - 1)));
    }
}

MainComponent::~MainComponent() = default;

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void MainComponent::resized()
{
    auto area = getLocalBounds();

    // The toolbar, laid out with FlexBox.
    juce::FlexBox toolbar;
    toolbar.alignItems = juce::FlexBox::AlignItems::center;
    auto item = [](juce::Component& c, float width) {
        return juce::FlexItem(c).withWidth(width).withHeight(26).withMargin({ 0, 4, 0, 4 });
    };
    toolbar.items = {
        item(openButton, 80),   item(fileLabel, 200).withFlex(1).withMinWidth(100),
        item(reflowButton, 80), item(zoomLabel, 44),
        item(zoomSlider, 160),  item(boundsToggle, 72),
    };
    toolbar.performLayout(area.removeFromTop(40));

    statusLabel.setBounds(area.removeFromBottom(26).reduced(8, 0));
    viewport.setBounds(area);
}

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

void MainComponent::chooseScore()
{
    chooser = std::make_unique<juce::FileChooser>("Open a score", juce::File(MSD_EXPORTED_SCORES_DIR), "*.music.json");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                         [this](const juce::FileChooser& fc) {
                             if (fc.getResult().existsAsFile())
                                 openScore(fc.getResult());
                         });
}

void MainComponent::openScore(const juce::File& file)
{
    try
    {
        music = loadMusicData(file.getFullPathName().toStdString());
    }
    catch (const std::exception& e)
    {
        music.reset();
        scoreView.clear();
        setStatus(juce::String::fromUTF8(e.what()));
        return;
    }
    fileLabel.setText(file.getFileName(), juce::dontSendNotification);

    // A new score has no flow yet: the first layout reflows.
    setup.layouts.at(0).flow.reset();
    reflowScore();
    relayout();
    viewport.setViewPosition(0, 0);
}

const LayoutProfile& MainComponent::profile() const
{
    const std::string& id = setup.layouts.at(0).layoutProfileId;
    for (const LayoutProfile& candidate : setup.layoutProfiles)
        if (candidate.id == id)
            return candidate;
    return setup.layoutProfiles.at(0);
}

void MainComponent::reflowScore()
{
    if (!music)
        return;
    Layout& layout = setup.layouts.at(0);
    try
    {
        const double started = juce::Time::getMillisecondCounterHiRes();
        layout.flow = reflow({ .music = *music, .profile = profile(), .font = font->metadata(), .layout = layout });
        reflowMs = millisecondsSince(started);
    }
    catch (const std::exception& e)
    {
        layout.flow.reset();
        setStatus(juce::String::fromUTF8(e.what()));
    }
}

void MainComponent::relayout()
{
    const Layout& layout = setup.layouts.at(0);
    if (!music || !layout.flow)
        return;
    try
    {
        const double started = juce::Time::getMillisecondCounterHiRes();
        std::vector<PageLayout> pages = layoutPages(
            { .music = *music, .profile = profile(), .font = font->metadata(), .layout = layout }, *layout.flow);
        const double layoutMs = millisecondsSince(started);

        std::size_t systems = 0;
        for (const PageLayout& page : pages)
            systems += page.systems.size();
        const std::size_t pageCount = pages.size();
        scoreView.setPages(std::move(pages));
        setStatus(juce::String::fromUTF8(music->title.c_str()) + ": " + juce::String::fromUTF8(profile().name.c_str())
                  + ", " + juce::String(pageCount) + " pages, " + juce::String(systems) + " systems (reflow "
                  + juce::String(juce::roundToInt(reflowMs)) + " ms, layout " + juce::String(juce::roundToInt(layoutMs))
                  + " ms). Hover a symbol to see its node.");
    }
    catch (const std::exception& e)
    {
        scoreView.clear();
        setStatus(juce::String::fromUTF8(e.what()));
    }
}

void MainComponent::setStatus(const juce::String& text)
{
    status = text;
    statusLabel.setText(text, juce::dontSendNotification);
}

} // namespace msd
