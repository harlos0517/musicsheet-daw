#include "MainComponent.h"

#include <algorithm>
#include <exception>
#include <string_view>

#include "BinaryData.h"
#include "core/json/Load.h"
#include "layout/SystemLayout.h"

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

LayoutProfile defaultProfile()
{
    return parseLayoutSetup(binary(BinaryData::default_layout_json, BinaryData::default_layout_jsonSize))
        .layoutProfiles.at(0);
}

} // namespace

MainComponent::MainComponent(const juce::String& scoreToOpen)
    : font(makeLeland())
    , profile(defaultProfile())
    , scoreView(*font)
{
    setWantsKeyboardFocus(true);

    openButton.onClick = [this] { chooseScore(); };
    fileLabel.setText("No score", juce::dontSendNotification);
    fileLabel.setMinimumHorizontalScale(0.7f);
    staffBox.setTextWhenNothingSelected("Staff");
    staffBox.onChange = [this] { relayout(); };

    firstSlider.setRange(1, 1, 1);
    firstSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 44, 24);
    firstSlider.onValueChange = [this] { relayout(); };
    countSlider.setRange(1, 16, 1);
    countSlider.setValue(4, juce::dontSendNotification);
    countSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 36, 24);
    countSlider.onValueChange = [this] { relayout(); };
    zoomSlider.setRange(4, 40, 0.5);
    zoomSlider.setValue(12, juce::dontSendNotification);
    zoomSlider.onValueChange = [this] { scoreView.setZoom(static_cast<float>(zoomSlider.getValue())); };
    boundsToggle.onClick = [this] { scoreView.setShowBounds(boundsToggle.getToggleState()); };

    scoreView.onHover = [this](const juce::String& text) {
        statusLabel.setText(text.isEmpty() ? status : text, juce::dontSendNotification);
    };
    viewport.setViewedComponent(&scoreView, false);

    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &openButton, &fileLabel, &staffBox, &firstLabel, &firstSlider, &countLabel, &countSlider, &zoomLabel,
             &zoomSlider, &boundsToggle, &statusLabel, &viewport })
        addAndMakeVisible(c);

    setSize(1200, 420);

    // Command line: [score.music.json] [staff ID] [first measure]. Without a score, in
    // development, an exported example.
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
        const auto it = std::find(staffIds.begin(), staffIds.end(), args[1].unquoted().toStdString());
        if (it != staffIds.end())
            staffBox.setSelectedId(static_cast<int>(it - staffIds.begin()) + 1, juce::dontSendNotification);
    }
    if (args.size() > 2)
        firstSlider.setValue(args[2].unquoted().getIntValue(), juce::dontSendNotification);
    relayout();
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
        item(openButton, 80),   item(fileLabel, 160).withFlex(1).withMinWidth(100),
        item(staffBox, 220),    item(firstLabel, 44),
        item(firstSlider, 100), item(countLabel, 70),
        item(countSlider, 92),  item(zoomLabel, 44),
        item(zoomSlider, 140),  item(boundsToggle, 72),
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
        staffIds.clear();
        staffBox.clear(juce::dontSendNotification);
        scoreView.clear();
        setStatus(juce::String::fromUTF8(e.what()));
        return;
    }

    fileLabel.setText(file.getFileName(), juce::dontSendNotification);

    staffIds.clear();
    staffBox.clear(juce::dontSendNotification);
    for (const Track& track : music->tracks)
        for (std::size_t i = 0; i < track.staves.size(); ++i)
        {
            juce::String name = juce::String::fromUTF8(track.instrumentName.c_str());
            if (track.staves.size() > 1)
                name << " " << static_cast<int>(i + 1);
            staffIds.push_back(track.staves[i].id);
            staffBox.addItem(name, static_cast<int>(staffIds.size()));
        }

    const auto measures = static_cast<double>(std::max<std::size_t>(music->measures.size(), 1));
    firstSlider.setRange(1, std::max(measures, 2.0), 1); // a range needs two ends
    firstSlider.setValue(1, juce::dontSendNotification);
    staffBox.setSelectedId(1, juce::dontSendNotification);
    relayout();
}

double MainComponent::systemWidthSp() const
{
    const LayoutProfile::Page& page = profile.page;
    return (page.widthMm - page.margins.left - page.margins.right) / profile.staffSpaceMm;
}

void MainComponent::relayout()
{
    if (!music || staffIds.empty() || staffBox.getSelectedId() <= 0)
        return;

    const std::size_t total = music->measures.size();
    const std::size_t first = std::min(static_cast<std::size_t>(firstSlider.getValue()), total) - 1;
    const std::size_t count = std::min(static_cast<std::size_t>(countSlider.getValue()), total - first);
    const NodeId& staffId = staffIds[static_cast<std::size_t>(staffBox.getSelectedId() - 1)];

    try
    {
        SystemLayout layout = layoutSystem({ .music = *music,
                                             .profile = profile,
                                             .font = font->metadata(),
                                             .staffId = staffId,
                                             .firstMeasure = first,
                                             .measureCount = count,
                                             .width = systemWidthSp() });
        const auto boxes = layout.boxes.size();
        scoreView.setSystem(std::move(layout));
        setStatus(juce::String::fromUTF8(music->title.c_str()) + ": measures " + juce::String(first + 1) + "-"
                  + juce::String(first + count) + ", " + juce::String(boxes)
                  + " boxes. Hover a symbol to see its node.");
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
