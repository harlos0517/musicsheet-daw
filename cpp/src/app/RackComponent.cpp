#include "RackComponent.h"

#include <algorithm>
#include <cctype>
#include <set>

#include "playback/Rack.h"

namespace msd
{

// A ListBox model from three functions.
class RackComponent::ListModel final : public juce::ListBoxModel
{
public:
    std::function<int()> rows;
    std::function<juce::String(int)> text;
    std::function<void(int)> selected;

    int getNumRows() override { return rows ? rows() : 0; }

    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool isSelected) override
    {
        if (isSelected)
            g.fillAll(juce::Colour(0xff2f5d8a));
        g.setColour(juce::Colours::white);
        g.setFont(14.0f);
        g.drawText(text ? text(row) : juce::String(), 6, 0, width - 12, height, juce::Justification::centredLeft, true);
    }

    void selectedRowsChanged(int row) override
    {
        if (selected)
            selected(row);
    }
};

namespace
{

juce::String str(const std::string& text) { return juce::String::fromUTF8(text.c_str()); }
std::string std8(const juce::String& text) { return text.toStdString(); }

// "Kontakt 8" → "kontakt-8", unique among the rack's plugins.
std::string pluginId(const juce::String& name, const PluginRack& rack)
{
    std::string base;
    for (const unsigned char c : name.toStdString())
    {
        if (std::isalnum(c))
            base += static_cast<char>(std::tolower(c));
        else if (!base.empty() && base.back() != '-')
            base += '-';
    }
    if (base.empty())
        base = "plugin";
    auto taken = [&](const std::string& id) {
        return std::any_of(rack.plugins.begin(), rack.plugins.end(),
                           [&](const PluginInstance& p) { return p.id == id; });
    };
    std::string id = base;
    for (int n = 2; taken(id); ++n)
        id = base + "-" + std::to_string(n);
    return id;
}

} // namespace

RackComponent::RackComponent(PluginRack& r, AudioHost& a)
    : rack(r)
    , audio(a)
    , pluginModel(std::make_unique<ListModel>())
    , slotModel(std::make_unique<ListModel>())
    , scoreModel(std::make_unique<ListModel>())
{
    pluginModel->rows = [this] { return static_cast<int>(rack.plugins.size()); };
    pluginModel->text = [this](int row) {
        const PluginInstance& plugin = rack.plugins[static_cast<std::size_t>(row)];
        const juce::String id = str(plugin.id);
        juce::String text = str(plugin.name);
        if (loading.contains(id))
            text << "  (loading...)";
        else if (failed.containsKey(id))
            text << "  (not loaded)";
        return text;
    };
    pluginModel->selected = [this](int) {
        slotList.deselectAllRows();
        refresh();
    };
    pluginList.setModel(pluginModel.get());

    slotModel->rows = [this] {
        const PluginInstance* plugin = selectedPlugin();
        return plugin == nullptr ? 0 : static_cast<int>(plugin->slots.size());
    };
    slotModel->text = [this](int row) {
        const PluginSlot& slot = selectedPlugin()->slots[static_cast<std::size_t>(row)];
        return juce::String(slot.channel).paddedLeft(' ', 2) + "  " + str(slot.name);
    };
    slotModel->selected = [this](int) { showSlot(); };
    slotList.setModel(slotModel.get());

    scoreModel->rows = [this] { return scoreLines.size(); };
    scoreModel->text = [this](int row) { return scoreLines[row]; };
    scoreList.setModel(scoreModel.get());

    addPluginButton.onClick = [this] { addPlugin(); };
    removePluginButton.onClick = [this] { removePlugin(); };
    pluginWindowButton.onClick = [this] {
        if (const PluginInstance* plugin = selectedPlugin(); plugin != nullptr && openPluginWindow)
            openPluginWindow(str(plugin->id));
    };
    saveButton.setTooltip("Save the rack and every plugin's state now (also done when the app closes)");
    saveButton.onClick = [this] {
        if (saveStates)
            saveStates();
    };
    addSlotButton.onClick = [this] { addSlot(); };
    removeSlotButton.onClick = [this] { removeSlot(); };
    testNoteButton.setTooltip("Hold to play middle C, with CC1 at 90, on the slot's channel");
    testNoteButton.onStateChange = [this] { testNote(testNoteButton.isDown()); };
    addSlotsButton.setTooltip("A slot for every output of the score no slot plays, on the plugin of its library");
    addSlotsButton.onClick = [this] { addSlotsForScore(); };

    for (int c = 1; c <= 16; ++c)
        channelBox.addItem(juce::String(c), c);
    channelBox.onChange = [this] { slotEdited(); };
    instrumentsEditor.setMultiLine(true);
    instrumentsEditor.setReturnKeyStartsNewLine(true);
    for (juce::TextEditor* editor : { &nameEditor, &instrumentsEditor, &outputsEditor })
        editor->onTextChange = [this] { slotEdited(); };

    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &pluginsLabel,  &pluginList,   &addPluginButton, &removePluginButton, &pluginWindowButton, &saveButton,
             &slotsLabel,    &slotList,     &addSlotButton,   &removeSlotButton,   &testNoteButton,     &nameLabel,
             &nameEditor,    &channelLabel, &channelBox,      &instrumentsLabel,   &instrumentsEditor,  &outputsLabel,
             &outputsEditor, &scoreLabel,   &scoreList,       &addSlotsButton,     &statusLabel })
        addAndMakeVisible(c);

    if (!rack.plugins.empty())
        pluginList.selectRow(0);
    refresh();
    setSize(920, 600);
}

RackComponent::~RackComponent()
{
    testNote(false);
    pluginList.setModel(nullptr);
    slotList.setModel(nullptr);
    scoreList.setModel(nullptr);
}

void RackComponent::resized()
{
    auto area = getLocalBounds().reduced(8);
    statusLabel.setBounds(area.removeFromBottom(24));
    auto score = area.removeFromBottom(170);
    area.removeFromBottom(8);

    auto buttonRow = [](juce::Rectangle<int>& column, std::initializer_list<juce::Component*> buttons) {
        auto row = column.removeFromBottom(28);
        column.removeFromBottom(4);
        const int width = row.getWidth() / static_cast<int>(buttons.size());
        for (juce::Component* b : buttons)
            b->setBounds(row.removeFromLeft(width).reduced(2, 0));
    };

    auto plugins = area.removeFromLeft(280);
    pluginsLabel.setBounds(plugins.removeFromTop(22));
    buttonRow(plugins, { &pluginWindowButton, &saveButton });
    buttonRow(plugins, { &addPluginButton, &removePluginButton });
    pluginList.setBounds(plugins);
    area.removeFromLeft(8);

    auto slots = area.removeFromLeft(260);
    slotsLabel.setBounds(slots.removeFromTop(22));
    buttonRow(slots, { &addSlotButton, &removeSlotButton, &testNoteButton });
    slotList.setBounds(slots);
    area.removeFromLeft(8);

    nameLabel.setBounds(area.removeFromTop(22));
    nameEditor.setBounds(area.removeFromTop(26));
    channelLabel.setBounds(area.removeFromTop(22));
    channelBox.setBounds(area.removeFromTop(26).removeFromLeft(80));
    outputsEditor.setBounds(area.removeFromBottom(26));
    outputsLabel.setBounds(area.removeFromBottom(22));
    instrumentsLabel.setBounds(area.removeFromTop(22));
    instrumentsEditor.setBounds(area);

    auto scoreTop = score.removeFromTop(26);
    addSlotsButton.setBounds(scoreTop.removeFromRight(200));
    scoreLabel.setBounds(scoreTop);
    scoreList.setBounds(score);
}

PluginInstance* RackComponent::selectedPlugin()
{
    const int row = pluginList.getSelectedRow();
    return row >= 0 && row < static_cast<int>(rack.plugins.size()) ? &rack.plugins[static_cast<std::size_t>(row)]
                                                                   : nullptr;
}

PluginSlot* RackComponent::selectedSlot()
{
    PluginInstance* plugin = selectedPlugin();
    const int row = slotList.getSelectedRow();
    return plugin != nullptr && row >= 0 && row < static_cast<int>(plugin->slots.size())
               ? &plugin->slots[static_cast<std::size_t>(row)]
               : nullptr;
}

void RackComponent::setScore(const MusicData* newMusic, const PlaybackSetup* newSetup)
{
    music = newMusic;
    setup = newSetup;
    refresh();
}

void RackComponent::pluginLoading(const juce::String& id)
{
    loading.addIfNotAlreadyThere(id);
    pluginList.updateContent();
    pluginList.repaint();
}

void RackComponent::pluginLoaded(const juce::String& id, const juce::String& error)
{
    loading.removeString(id);
    if (error.isNotEmpty())
    {
        failed.set(id, error);
        setStatus(id + ": " + error);
    }
    else
    {
        failed.remove(juce::StringRef(id));
    }
    refresh();
}

void RackComponent::refresh(bool slotEditorToo)
{
    pluginList.updateContent();
    pluginList.repaint();
    slotList.updateContent();
    slotList.repaint();
    const PluginInstance* plugin = selectedPlugin();
    removePluginButton.setEnabled(plugin != nullptr);
    pluginWindowButton.setEnabled(plugin != nullptr && audio.plugin(str(plugin->id)) != nullptr);
    addSlotButton.setEnabled(plugin != nullptr);
    if (slotEditorToo)
        showSlot();

    // Where each output of the score goes.
    scoreLines.clear();
    if (music != nullptr && setup != nullptr)
        for (const Track& track : music->tracks)
        {
            const auto found = setup->trackProfiles.find(track.id);
            if (found == setup->trackProfiles.end())
                continue;
            for (const TrackOutput& output : found->second.outputs)
            {
                juce::String line = str(track.instrumentName) + " (" + str(output.id) + ", "
                                    + str(output.instrumentProfileId) + ")  ->  ";
                if (const std::optional<SlotRef> slot = findSlot(rack, track.instrumentName, output.id))
                {
                    const PluginInstance& p = rack.plugins[slot->plugin];
                    line << str(p.name) << ", channel " << p.slots[slot->slot].channel << " ("
                         << str(p.slots[slot->slot].name) << ")";
                }
                else
                {
                    line << "no slot";
                }
                scoreLines.add(line);
            }
        }
    else
        scoreLines.add(setup == nullptr && music != nullptr ? "No .setup.json next to the score" : "No score open");
    scoreList.updateContent();
    scoreList.repaint();
    addSlotsButton.setEnabled(music != nullptr && setup != nullptr && !rack.plugins.empty());
}

void RackComponent::showSlot()
{
    const PluginSlot* slot = selectedSlot();
    showingSlot = true;
    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &nameEditor, &channelBox, &instrumentsEditor, &outputsEditor, &removeSlotButton, &testNoteButton })
        c->setEnabled(slot != nullptr);
    if (slot == nullptr)
    {
        nameEditor.clear();
        channelBox.setSelectedId(0, juce::dontSendNotification);
        instrumentsEditor.clear();
        outputsEditor.clear();
    }
    else
    {
        nameEditor.setText(str(slot->name), false);
        channelBox.setSelectedId(slot->channel, juce::dontSendNotification);
        juce::StringArray names;
        for (const std::string& name : slot->instrumentNames)
            names.add(str(name));
        instrumentsEditor.setText(names.joinIntoString("\n"), false);
        juce::StringArray outputs;
        if (slot->outputIds)
            for (const std::string& id : *slot->outputIds)
                outputs.add(str(id));
        outputsEditor.setText(outputs.joinIntoString(", "), false);
    }
    showingSlot = false;
}

void RackComponent::slotEdited()
{
    PluginSlot* slot = selectedSlot();
    if (showingSlot || slot == nullptr)
        return;
    slot->name = std8(nameEditor.getText().trim());
    if (channelBox.getSelectedId() > 0)
        slot->channel = channelBox.getSelectedId();
    slot->instrumentNames.clear();
    for (const juce::String& line : juce::StringArray::fromLines(instrumentsEditor.getText()))
        if (line.trim().isNotEmpty())
            slot->instrumentNames.push_back(std8(line.trim()));
    juce::StringArray outputs = juce::StringArray::fromTokens(outputsEditor.getText(), ",", "");
    outputs.trim();
    outputs.removeEmptyStrings();
    if (outputs.isEmpty())
        slot->outputIds.reset();
    else
    {
        slot->outputIds.emplace();
        for (const juce::String& id : outputs)
            slot->outputIds->push_back(std8(id));
    }

    // Two slots of a plugin on one channel would play each other's notes.
    const PluginInstance* plugin = selectedPlugin();
    const auto sameChannel = std::count_if(plugin->slots.begin(), plugin->slots.end(),
                                           [&](const PluginSlot& s) { return s.channel == slot->channel; });
    setStatus(sameChannel > 1 ? "Another slot of this plugin is on channel " + juce::String(slot->channel) : "");

    const int row = slotList.getSelectedRow();
    slotList.updateContent();
    slotList.repaintRow(row);
    refresh(false);
    if (changed)
        changed();
}

void RackComponent::addPlugin()
{
    const juce::StringArray files = audio.findPlugins();
    if (files.isEmpty())
    {
        setStatus("No VST3 plugins in the system's VST3 folders");
        return;
    }
    juce::PopupMenu menu;
    for (int i = 0; i < files.size(); ++i)
        menu.addItem(i + 1, juce::File(files[i]).getFileNameWithoutExtension());
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(addPluginButton), [this, files](int chosen) {
        if (chosen <= 0)
            return;
        const juce::String file = files[chosen - 1];
        const juce::String name = juce::File(file).getFileNameWithoutExtension();
        PluginInstance plugin { .id = pluginId(name, rack), .name = std8(name), .pluginFile = std8(file), .slots = {} };
        const juce::String id = str(plugin.id);
        rack.plugins.push_back(std::move(plugin));
        loading.add(id);
        pluginList.updateContent();
        pluginList.selectRow(static_cast<int>(rack.plugins.size()) - 1);
        if (changed)
            changed();
        setStatus("Loading " + name + "...");
        audio.loadPlugin(id, file, {}, [this, id](const juce::String& error) {
            pluginLoaded(id, error);
            if (error.isEmpty())
            {
                setStatus("Load its patches in the plugin's window, one per channel, then add a slot for each");
                if (openPluginWindow)
                    openPluginWindow(id);
            }
        });
    });
}

void RackComponent::removePlugin()
{
    const PluginInstance* plugin = selectedPlugin();
    if (plugin == nullptr)
        return;
    const juce::String id = str(plugin->id);
    juce::AlertWindow::showOkCancelBox(
        juce::MessageBoxIconType::QuestionIcon, "Remove " + str(plugin->name) + "?",
        "Its slots go with it, and the patches loaded in it are lost when the rack is saved.", "Remove", "Keep", this,
        juce::ModalCallbackFunction::create([this, id](int result) {
            if (result == 0)
                return;
            if (closePluginWindow)
                closePluginWindow(id);
            audio.removePlugin(id);
            std::erase_if(rack.plugins, [&](const PluginInstance& p) { return str(p.id) == id; });
            pluginList.deselectAllRows();
            if (!rack.plugins.empty())
                pluginList.selectRow(0);
            refresh();
            if (changed)
                changed();
        }));
}

void RackComponent::addSlot()
{
    PluginInstance* plugin = selectedPlugin();
    if (plugin == nullptr)
        return;
    std::set<int> used;
    for (const PluginSlot& slot : plugin->slots)
        used.insert(slot.channel);
    int channel = 1;
    while (channel < 16 && used.contains(channel))
        ++channel;
    std::string id;
    for (std::size_t n = plugin->slots.size() + 1;
         id.empty()
         || std::any_of(plugin->slots.begin(), plugin->slots.end(), [&](const PluginSlot& s) { return s.id == id; });
         ++n)
        id = "slot-" + std::to_string(n);
    plugin->slots.push_back(PluginSlot { .id = id,
                                         .name = "Channel " + std::to_string(channel),
                                         .channel = channel,
                                         .instrumentNames = {},
                                         .outputIds = {} });
    slotList.updateContent();
    slotList.selectRow(static_cast<int>(plugin->slots.size()) - 1);
    refresh();
    nameEditor.grabKeyboardFocus();
    if (changed)
        changed();
}

void RackComponent::removeSlot()
{
    PluginInstance* plugin = selectedPlugin();
    const int row = slotList.getSelectedRow();
    if (plugin == nullptr || selectedSlot() == nullptr)
        return;
    plugin->slots.erase(plugin->slots.begin() + row);
    slotList.deselectAllRows();
    refresh();
    if (changed)
        changed();
}

void RackComponent::addSlotsForScore()
{
    if (music == nullptr || setup == nullptr || rack.plugins.empty())
        return;
    const int row = pluginList.getSelectedRow();
    std::vector<std::string> added;
    rack = msd::addSlotsForScore(rack, *music, *setup, row < 0 ? 0 : static_cast<std::size_t>(row), added);
    refresh();
    if (changed)
        changed();
    if (added.empty())
    {
        setStatus("Every output of the score has a slot");
        return;
    }
    juce::StringArray lines;
    for (const std::string& line : added)
        lines.add(str(line));
    setStatus(juce::String(added.size()) + " slots added");
    juce::AlertWindow::showMessageBoxAsync(
        juce::MessageBoxIconType::InfoIcon, "Slots added",
        "Load each patch on its channel in the plugin's window:\n\n" + lines.joinIntoString("\n"), "OK", this);
}

void RackComponent::testNote(bool down)
{
    if (down == testNoteDown)
        return; // onStateChange also fires for hovering
    testNoteDown = down;
    if (down)
    {
        const PluginInstance* plugin = selectedPlugin();
        const PluginSlot* slot = selectedSlot();
        if (plugin == nullptr || slot == nullptr)
            return;
        testPlugin = str(plugin->id);
        testChannel = slot->channel;
        audio.sendMidi(testPlugin, juce::MidiMessage::controllerEvent(testChannel, 1, 90));
        audio.sendMidi(testPlugin, juce::MidiMessage::noteOn(testChannel, 60, static_cast<juce::uint8>(90)));
    }
    else if (testPlugin.isNotEmpty())
    {
        audio.sendMidi(testPlugin, juce::MidiMessage::noteOff(testChannel, 60));
        testPlugin.clear();
    }
}

void RackComponent::setStatus(const juce::String& text) { statusLabel.setText(text, juce::dontSendNotification); }

} // namespace msd
