#include "MainComponent.h"

#include <algorithm>
#include <exception>
#include <string_view>
#include <unordered_set>

#include "BinaryData.h"
#include "core/json/Load.h"
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

    for (juce::Component* c :
         std::initializer_list<juce::Component*> { &openButton, &fileLabel, &reflowButton, &exportButton, &zoomLabel,
                                                   &zoomSlider, &boundsToggle, &statusLabel, &viewport })
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
        item(reflowButton, 80), item(exportButton, 110),
        item(zoomLabel, 44),    item(zoomSlider, 160),
        item(boundsToggle, 72),
    };
    toolbar.performLayout(area.removeFromTop(40));

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
        scoreView.clear();
        setStatus(juce::String::fromUTF8(e.what()));
        return;
    }
    fileLabel.setText(file.getFileName(), juce::dontSendNotification);
    scoreFile = file;
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

} // namespace msd
