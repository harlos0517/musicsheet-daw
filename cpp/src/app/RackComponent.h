#pragma once

// Edits the shared PluginRack: its plugins (each a loaded VST3), their slots, and shows
// where each output of the open score goes.

#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "AudioHost.h"
#include "core/types/MusicData.h"
#include "core/types/PlaybackSetup.h"
#include "core/types/PluginRack.h"

namespace msd
{

class RackComponent final : public juce::Component
{
public:
    RackComponent(PluginRack& rack, AudioHost& audio);
    ~RackComponent() override;

    std::function<void(const juce::String& pluginId)> openPluginWindow;
    std::function<void(const juce::String& pluginId)> closePluginWindow; // before a plugin goes
    std::function<void()> changed;                                        // the rack changed: save it
    std::function<void()> saveStates;                                     // "Save now"

    // The open score, for its outputs; either may be null.
    void setScore(const MusicData* music, const PlaybackSetup* setup);

    // A plugin started loading; finished loading, or failed to.
    void pluginLoading(const juce::String& pluginId);
    void pluginLoaded(const juce::String& pluginId, const juce::String& error);

    void resized() override;

private:
    class ListModel;

    PluginInstance* selectedPlugin();
    PluginSlot* selectedSlot();
    void refresh(bool slotEditorToo = true); // the lists, and the slot editor unless it is being typed in
    void showSlot();
    void slotEdited();
    void addPlugin();
    void removePlugin();
    void addSlot();
    void removeSlot();
    void addSlotsForScore();
    void testNote(bool down);
    void setStatus(const juce::String& text);

    PluginRack& rack;
    AudioHost& audio;
    const MusicData* music = nullptr;
    const PlaybackSetup* setup = nullptr;
    juce::StringArray loading; // plugin IDs not loaded yet
    juce::StringPairArray failed; // plugin ID → why it didn't load

    juce::Label pluginsLabel { {}, "Plugins" };
    std::unique_ptr<ListModel> pluginModel;
    juce::ListBox pluginList;
    juce::TextButton addPluginButton { "Add plugin..." };
    juce::TextButton removePluginButton { "Remove" };
    juce::TextButton pluginWindowButton { "Plugin window" };
    juce::TextButton saveButton { "Save now" };

    juce::Label slotsLabel { {}, "Slots (channels)" };
    std::unique_ptr<ListModel> slotModel;
    juce::ListBox slotList;
    juce::TextButton addSlotButton { "Add slot" };
    juce::TextButton removeSlotButton { "Remove" };
    juce::TextButton testNoteButton { "Test note" };
    bool testNoteDown = false;
    juce::String testPlugin; // where the test note is playing
    int testChannel = 1;

    juce::Label nameLabel { {}, "Name" };
    juce::TextEditor nameEditor;
    juce::Label channelLabel { {}, "Channel" };
    juce::ComboBox channelBox;
    juce::Label instrumentsLabel { {}, "Instrument names, one per line" };
    juce::TextEditor instrumentsEditor;
    juce::Label outputsLabel { {}, "Output IDs, comma-separated (empty: all)" };
    juce::TextEditor outputsEditor;
    bool showingSlot = false; // filling the editors, not the user typing

    juce::Label scoreLabel { {}, "This score" };
    std::unique_ptr<ListModel> scoreModel;
    juce::ListBox scoreList;
    juce::TextButton addSlotsButton { "Add slots for this score" };
    juce::StringArray scoreLines;

    juce::Label statusLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RackComponent)
};

} // namespace msd
