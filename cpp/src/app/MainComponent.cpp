#include "MainComponent.h"

#include <algorithm>
#include <exception>
#include <string_view>
#include <unordered_set>

#include "BinaryData.h"
#include "core/json/Load.h"
#include "core/json/Save.h"
#include "layout/PageLayout.h"
#include "playback/Play.h"

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

// A plugin's own editor in a window of its own; closing it only hides it.
class PluginWindow final : public juce::DocumentWindow
{
public:
    explicit PluginWindow(juce::AudioProcessor& plugin)
        : DocumentWindow(plugin.getName(), juce::Colours::darkgrey, DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar(true);
        juce::AudioProcessorEditor* editor = plugin.hasEditor() ? plugin.createEditorAndMakeActive() : nullptr;
        if (editor == nullptr)
            editor = new juce::GenericAudioProcessorEditor(plugin);
        setContentOwned(editor, true);
        setResizable(editor->isResizable(), false);
        centreWithSize(getWidth(), getHeight());
        setVisible(true);
    }

    void closeButtonPressed() override { setVisible(false); }
};

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
    exportButton.setTooltip("Play the score as it is now and write a MIDI file");
    exportButton.onClick = [this] { chooseMidiFile(); };
    audioButton.setTooltip("Output device, sample rate and buffer size");
    audioButton.onClick = [this] { showAudioSettings(); };
    rackButton.setTooltip("The plugins scores play on, shared by every score");
    rackButton.onClick = [this] { showRack(); };
    zoomSlider.setRange(2, 30, 0.5);
    zoomSlider.setValue(6, juce::dontSendNotification);
    zoomSlider.onValueChange = [this] { scoreView.setZoom(static_cast<float>(zoomSlider.getValue())); };
    boundsToggle.onClick = [this] { scoreView.setShowBounds(boundsToggle.getToggleState()); };

    scoreView.onHover = [this](const juce::String& text) {
        statusLabel.setText(text.isEmpty() ? status : text, juce::dontSendNotification);
    };
    scoreView.onNoteClick = [this](const NodeId& noteId, bool shift) { clickNote(noteId, shift); };
    viewport.setViewedComponent(&scoreView, false);
    // The arrow keys move notes, so the viewport must not take them for scrolling.
    viewport.setWantsKeyboardFocus(false);
    viewport.setScrollBarsShown(true, true);

    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &openButton, &fileLabel, &reflowButton, &exportButton, &zoomLabel, &zoomSlider, &boundsToggle,
             &audioButton, &rackButton, &statusLabel, &viewport })
        addAndMakeVisible(c);

    setSize(1000, 800);
    loadRack();

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

MainComponent::~MainComponent()
{
    saveRack(true);
    // The windows that show plugins' editors go before the plugins.
    rackComponent = nullptr;
    rackWindow.reset();
    pluginWindows.clear();
}

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
        item(reflowButton, 80), item(exportButton, 110),
        item(zoomLabel, 44),    item(zoomSlider, 160),
        item(boundsToggle, 72),
    };
    toolbar.performLayout(area.removeFromTop(40));

    juce::FlexBox audioBar;
    audioBar.alignItems = juce::FlexBox::AlignItems::center;
    audioBar.items = {
        item(audioButton, 80),
        item(rackButton, 80),
    };
    audioBar.performLayout(area.removeFromTop(34));

    statusLabel.setBounds(area.removeFromBottom(26).reduced(8, 0));
    viewport.setBounds(area);
}

bool MainComponent::keyPressed(const juce::KeyPress& key)
{
    // The MuseScore keys: Up/Down a semitone, Alt+Shift+Up/Down a step in the key,
    // Ctrl+Up/Down an octave.
    const int code = key.getKeyCode();
    const juce::ModifierKeys mods = key.getModifiers();
    if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
    {
        const bool up = code == juce::KeyPress::upKey;
        if (mods.isAltDown() && mods.isShiftDown() && !mods.isCommandDown())
            movePitch(up ? PitchMove::StepUp : PitchMove::StepDown);
        else if (mods.isCommandDown() && !mods.isAltDown() && !mods.isShiftDown())
            movePitch(up ? PitchMove::OctaveUp : PitchMove::OctaveDown);
        else if (!mods.isAnyModifierKeyDown())
            movePitch(up ? PitchMove::SemitoneUp : PitchMove::SemitoneDown);
        else
            return false;
        return true;
    }
    if (key == juce::KeyPress('z', juce::ModifierKeys::commandModifier, 0))
    {
        undo();
        return true;
    }
    if (key == juce::KeyPress('y', juce::ModifierKeys::commandModifier, 0)
        || key == juce::KeyPress('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0))
    {
        redo();
        return true;
    }
    if (code == juce::KeyPress::escapeKey && !selection.notes.empty())
    {
        selection.notes.clear();
        showSelection();
        return true;
    }

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
        playbackSetup.reset();
        if (rackComponent != nullptr)
            rackComponent->setScore(nullptr, nullptr);
        scoreView.clear();
        setStatus(juce::String::fromUTF8(e.what()));
        return;
    }
    fileLabel.setText(file.getFileName(), juce::dontSendNotification);
    scoreFile = file;

    // The playback setup pnpm midi wrote next to it: which outputs each track has.
    const juce::String name = file.getFileName().upToLastOccurrenceOf(".music.json", false, true);
    playbackSetup.reset();
    if (const juce::File setupFile = file.getSiblingFile(name + ".setup.json"); setupFile.existsAsFile())
    {
        try
        {
            playbackSetup = loadPlaybackSetup(setupFile.getFullPathName().toStdString());
        }
        catch (const std::exception&)
        {
            // Export MIDI reports it.
        }
    }
    if (rackComponent != nullptr)
        rackComponent->setScore(&*music, playbackSetup ? &*playbackSetup : nullptr);
    history.clear();
    selection.notes.clear();
    scoreView.setSelected({});

    // Large scores use smaller staves: the default profile, unless one of its systems
    // would be taller than the page.
    Layout& layout = setup.layouts.at(0);
    layout.layoutProfileId = defaultProfileId;
    try
    {
        if (!systemFitsPage(*music, layout, profile()))
            layout.layoutProfileId = largeScoreProfileId;
    }
    catch (const std::exception&)
    {
        // A profile without a staff size: reflow reports it.
    }

    // A new score has no flow yet: the first layout reflows.
    layout.flow.reset();
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

void MainComponent::clickNote(const NodeId& noteId, bool shift)
{
    grabKeyboardFocus(); // for the arrow keys
    std::vector<NodeId>& notes = selection.notes;
    if (noteId.empty())
    {
        if (shift || notes.empty())
            return;
        notes.clear();
    }
    else if (shift)
    {
        const auto found = std::find(notes.begin(), notes.end(), noteId);
        if (found == notes.end())
            notes.push_back(noteId);
        else
            notes.erase(found);
    }
    else
        notes = { noteId };
    showSelection();
}

void MainComponent::showSelection()
{
    scoreView.setSelected(std::unordered_set<NodeId>(selection.notes.begin(), selection.notes.end()));
    const std::size_t count = selection.notes.size();
    setStatus(count == 0   ? juce::String("Nothing selected")
              : count == 1 ? juce::String(selection.notes[0]) + " selected"
                           : juce::String(count) + " notes selected");
}

void MainComponent::movePitch(PitchMove move)
{
    if (!music || selection.notes.empty())
        return;
    Edit edit = movePitches(*music, selection.notes, move);
    if (edit.changes.empty())
        return;
    const juce::String what = juce::String(edit.name) + ": " + juce::String(edit.changes.size()) + " notes";
    const std::vector<NodeId> measures = editedMeasures(*music, edit);
    history.apply(*music, std::move(edit));
    edited(what, measures);
}

void MainComponent::undo()
{
    if (!music || history.nextUndo() == nullptr)
        return;
    const juce::String what = "Undo " + juce::String(history.nextUndo()->name).toLowerCase();
    const std::vector<NodeId> measures = editedMeasures(*music, *history.nextUndo());
    history.undo(*music);
    edited(what, measures);
}

void MainComponent::redo()
{
    if (!music || history.nextRedo() == nullptr)
        return;
    const juce::String what = "Redo " + juce::String(history.nextRedo()->name).toLowerCase();
    const std::vector<NodeId> measures = editedMeasures(*music, *history.nextRedo());
    history.redo(*music);
    edited(what, measures);
}

void MainComponent::edited(const juce::String& what, const std::vector<NodeId>& measures)
{
    // The flow stays: systems and pages stay where they are. Only the systems with
    // changed measures are laid out again, unless the pages don't match the flow.
    const juce::Point<int> scrolled = viewport.getViewPosition();
    const double started = juce::Time::getMillisecondCounterHiRes();
    const Layout& layout = setup.layouts.at(0);
    std::vector<PageLayout> pages = scoreView.takePages();
    bool partly = false;
    try
    {
        partly =
            layout.flow
            && relayoutMeasures({ .music = *music, .profile = profile(), .font = font->metadata(), .layout = layout },
                                *layout.flow, measures, pages);
    }
    catch (const std::exception&)
    {
        partly = false; // relayout() reports it
    }
    if (partly)
        scoreView.setPages(std::move(pages));
    else
        relayout();
    const double layoutMs = millisecondsSince(started);
    viewport.setViewPosition(scrolled);
    scoreView.setSelected(std::unordered_set<NodeId>(selection.notes.begin(), selection.notes.end()));
    setStatus(what + " (layout " + juce::String(juce::roundToInt(layoutMs)) + " ms)");
}

void MainComponent::chooseMidiFile()
{
    if (!music)
        return;
    // Not <name>.mid: pnpm midi writes that one, and the tests compare with it.
    const juce::String name = scoreFile.getFileName().upToLastOccurrenceOf(".music.json", false, true);
    chooser =
        std::make_unique<juce::FileChooser>("Export MIDI", scoreFile.getSiblingFile(name + ".edited.mid"), "*.mid");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                             | juce::FileBrowserComponent::warnAboutOverwriting,
                         [this](const juce::FileChooser& fc) {
                             if (fc.getResult() != juce::File())
                                 exportMidi(fc.getResult());
                         });
}

void MainComponent::exportMidi(const juce::File& file)
{
    // Played with the setup the score was exported with, next to it.
    const juce::String name = scoreFile.getFileName().upToLastOccurrenceOf(".music.json", false, true);
    const juce::File setupFile = scoreFile.getSiblingFile(name + ".setup.json");
    if (!setupFile.existsAsFile())
    {
        setStatus("No playback setup: " + setupFile.getFullPathName() + " is missing");
        return;
    }
    try
    {
        const Playback playback = play(*music, loadPlaybackSetup(setupFile.getFullPathName().toStdString()));
        if (!file.replaceWithData(playback.file.data(), playback.file.size()))
        {
            setStatus("Could not write " + file.getFullPathName());
            return;
        }
        setStatus("Wrote " + file.getFileName() + ": " + juce::String(playback.tracks.size()) + " tracks, "
                  + juce::String(playback.warnings.size()) + " warnings");
    }
    catch (const std::exception& e)
    {
        setStatus(juce::String::fromUTF8(e.what()));
    }
}

void MainComponent::showAudioSettings()
{
    // Output only: no inputs, no MIDI devices yet. AudioHost saves what is picked.
    auto selector =
        std::make_unique<juce::AudioDeviceSelectorComponent>(audio.devices(), 0, 0, 2, 2, false, false, true, false);
    selector->setSize(500, 360);
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(selector.release());
    options.dialogTitle = "Audio settings";
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.componentToCentreAround = this;
    options.launchAsync();
}

namespace
{

juce::File appFolder()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Musicsheet DAW");
}

juce::File rackFile() { return appFolder().getChildFile("rack.json"); }
juce::File stateFile(const juce::String& pluginId)
{
    return appFolder().getChildFile("rack").getChildFile(pluginId + ".state");
}

// The rack's own window; closing it only hides it.
class RackWindow final : public juce::DocumentWindow
{
public:
    explicit RackWindow(std::unique_ptr<RackComponent> content)
        : DocumentWindow("Rack", juce::Colours::darkgrey, DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar(true);
        setContentOwned(content.release(), true);
        setResizable(true, false);
        centreWithSize(getWidth(), getHeight());
    }

    void closeButtonPressed() override { setVisible(false); }
};

} // namespace

void MainComponent::loadRack()
{
    if (rackFile().existsAsFile())
    {
        try
        {
            rack = loadPluginRack(rackFile().getFullPathName().toStdString());
        }
        catch (const std::exception& e)
        {
            setStatus("The rack could not be read, so it starts empty: " + juce::String::fromUTF8(e.what()));
            rack = {};
        }
    }

    auto content = std::make_unique<RackComponent>(rack, audio);
    rackComponent = content.get();
    rackComponent->openPluginWindow = [this](const juce::String& id) { openPluginWindow(id); };
    rackComponent->closePluginWindow = [this](const juce::String& id) { closePluginWindow(id); };
    rackComponent->changed = [this] { saveRackSoon.startTimer(1000); };
    rackComponent->saveStates = [this] { saveRack(true); };
    rackWindow = std::make_unique<RackWindow>(std::move(content));

    // Each plugin as it was saved, patches and all. Kontakt then loads its samples in
    // the background.
    for (const PluginInstance& plugin : rack.plugins)
    {
        const juce::String id = juce::String::fromUTF8(plugin.id.c_str());
        juce::MemoryBlock state;
        stateFile(id).loadFileAsData(state);
        rackComponent->pluginLoading(id);
        audio.loadPlugin(id, juce::String::fromUTF8(plugin.pluginFile.c_str()), state,
                         [this, id](const juce::String& error) {
                             if (rackComponent != nullptr)
                                 rackComponent->pluginLoaded(id, error);
                         });
    }
}

void MainComponent::saveRack(bool withStates)
{
    saveRackSoon.stopTimer();
    try
    {
        appFolder().createDirectory();
        savePluginRack(rack, rackFile().getFullPathName().toStdString());
    }
    catch (const std::exception& e)
    {
        setStatus("The rack could not be saved: " + juce::String::fromUTF8(e.what()));
        return;
    }
    if (!withStates)
        return;

    // A plugin that didn't load keeps the state it had.
    const juce::File folder = stateFile("x").getParentDirectory();
    folder.createDirectory();
    juce::StringArray kept;
    for (const PluginInstance& plugin : rack.plugins)
    {
        const juce::String id = juce::String::fromUTF8(plugin.id.c_str());
        kept.add(stateFile(id).getFileName());
        if (audio.plugin(id) == nullptr)
            continue;
        const juce::MemoryBlock state = audio.pluginState(id);
        if (!stateFile(id).replaceWithData(state.getData(), state.getSize()))
            setStatus("Could not write " + stateFile(id).getFullPathName());
    }
    // The states of removed plugins.
    for (const juce::File& file : folder.findChildFiles(juce::File::findFiles, false, "*.state"))
        if (!kept.contains(file.getFileName()))
            file.deleteFile();
}

void MainComponent::showRack()
{
    if (rackWindow == nullptr)
        return;
    rackWindow->setVisible(true);
    rackWindow->toFront(true);
}

void MainComponent::openPluginWindow(const juce::String& id)
{
    juce::AudioPluginInstance* plugin = audio.plugin(id);
    if (plugin == nullptr)
        return;
    std::unique_ptr<juce::DocumentWindow>& window = pluginWindows[id];
    if (window == nullptr)
        window = std::make_unique<PluginWindow>(*plugin);
    window->setVisible(true);
    window->toFront(true);
}

void MainComponent::closePluginWindow(const juce::String& id) { pluginWindows.erase(id); }

} // namespace msd
