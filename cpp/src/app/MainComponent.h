#pragma once

#include <memory>
#include <optional>

#include <juce_gui_basics/juce_gui_basics.h>

#if MSD_ENABLE_INSPECTOR
    #include <melatonin_inspector/melatonin_inspector.h>
#endif

#include <map>

#include "AudioHost.h"
#include "MusicFont.h"
#include "RackComponent.h"
#include "ScoreView.h"
#include "core/types/LayoutSetup.h"
#include "core/types/MusicData.h"
#include "core/types/PlaybackSetup.h"
#include "core/types/PluginRack.h"
#include "edit/EditHistory.h"
#include "edit/PitchEdit.h"
#include "edit/Selection.h"

namespace msd
{

// The window's content: a toolbar to open a score, reflow it and export it as MIDI; a
// second one for audio and the plugin rack; and the pages below, where notes can be
// selected and moved.
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
    void reflowScore();
    void relayout();
    void clickNote(const NodeId& noteId, bool shift);
    void showSelection();
    void movePitch(PitchMove move);
    void undo();
    void redo();
    // Lays out the systems of `measures` again and says what changed.
    void edited(const juce::String& what, const std::vector<NodeId>& measures);
    void chooseMidiFile();
    void exportMidi(const juce::File& file);
    void showAudioSettings();
    void showRack();
    void togglePlay();
    void startPlaying(double preparedMs); // once `playback` is there
    void showPlaying(); // the Play button, while playing and once it stops
    void forgetPlayback(); // after an edit, or another score: play() runs again
    void loadRack();               // rack.json and each plugin's state, at the start
    void saveRack(bool withStates); // rack.json; with the plugins' states too
    void openPluginWindow(const juce::String& pluginId);
    void closePluginWindow(const juce::String& pluginId);
    void setStatus(const juce::String& text);
    const LayoutProfile& profile() const; // the layout's, or the first one

    // Profile IDs in resources/default.layout.json.
    static constexpr const char* defaultProfileId = "default";
    static constexpr const char* largeScoreProfileId = "large-score"; // 1 mm staves on A3

    // Declared before scoreView, which keeps a reference to it.
    std::unique_ptr<MusicFont> font;
    LayoutSetup setup; // the default profile and the full-score layout, with its flow
    std::optional<MusicData> music;
    std::optional<PlaybackSetup> playbackSetup; // the score's <name>.setup.json, if there is one
    std::shared_ptr<const MusicData> playedMusic; // the copy of the score `playback` was made from
    std::optional<Playback> playback;             // play()'s result, until the score changes
    std::vector<PluginSequence> sequences;      // what the audio host plays, from playback
    bool sequencesReady = false;                // false after an edit or a rack change
    bool preparing = false;                     // play() is running on `preparer`
    int playGeneration = 0;                     // changes when a running play() is for an old score
    juce::ThreadPool preparer { 1 };
    juce::File scoreFile;
    Selection selection;
    EditHistory history;
    double reflowMs = 0;

    juce::TextButton openButton { "Open..." };
    juce::Label fileLabel;
    juce::TextButton reflowButton { "Reflow" };
    juce::TextButton exportButton { "Export MIDI..." };
    juce::Label zoomLabel { {}, "Zoom" };
    juce::Slider zoomSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::ToggleButton boundsToggle { "Boxes" };
    juce::TextButton audioButton { "Audio..." };
    juce::TextButton rackButton { "Rack..." };
    juce::TextButton playButton { "Play" };
    juce::Label statusLabel;
    juce::String status; // shown when nothing is hovered

    ScoreView scoreView;
    juce::Viewport viewport;

    std::unique_ptr<juce::FileChooser> chooser;

    // The windows showing plugins' editors are declared after the audio host, so they go
    // before their plugins do.
    PluginRack rack;
    AudioHost audio;
    std::map<juce::String, std::unique_ptr<juce::DocumentWindow>> pluginWindows;
    std::unique_ptr<juce::DocumentWindow> rackWindow;
    RackComponent* rackComponent = nullptr; // inside rackWindow
    juce::TimedCallback saveRackSoon { [this] { saveRack(false); } };
    juce::TimedCallback playingTimer { [this] { showPlaying(); } };

#if MSD_ENABLE_INSPECTOR
    std::unique_ptr<melatonin::Inspector> inspector;
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

} // namespace msd
