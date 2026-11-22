#pragma once

// The audio side of the app: the output device, and the VST3 plugins of the rack
// playing through it, mixed. Each plugin is known by its rack ID and gets its own MIDI:
// its sequence while the score plays, and whatever is sent to it by hand.

#include <atomic>
#include <bitset>
#include <functional>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "playback/Sequence.h"

namespace msd
{

class AudioHost final : private juce::ChangeListener, private juce::AudioIODeviceCallback
{
public:
    AudioHost(); // opens the device used last time, or the system's default
    ~AudioHost() override;

    // Changes to it are saved, for the next start.
    juce::AudioDeviceManager& devices() { return deviceManager; }

    // The .vst3 files in the system's VST3 folders, without loading them.
    juce::StringArray findPlugins();

    // Loads the first plugin in a .vst3 file as `id`, in place of one with that ID,
    // restoring `state` if it isn't empty. Calls `done` on the message thread with ""
    // or what went wrong. Close the plugin's window before replacing it.
    void loadPlugin(const juce::String& id, const juce::String& file, const juce::MemoryBlock& state,
                    std::function<void(const juce::String& error)> done);
    void removePlugin(const juce::String& id); // close its window first
    juce::AudioPluginInstance* plugin(const juce::String& id);

    // What the plugin would need to be restored: for Kontakt, its patches. Empty for an
    // ID with no plugin.
    juce::MemoryBlock pluginState(const juce::String& id);

    // Played by the plugin at the start of the next audio block.
    void sendMidi(const juce::String& id, const juce::MidiMessage& message);

    // What each plugin plays from now on (one with no sequence plays nothing). Stops.
    void setSequences(std::vector<PluginSequence> sequences);

    // Plays the sequences from `fromMs`, each plugin first getting the messages `chase`
    // has for it. Stops by itself a second after the last message.
    void start(double fromMs, const std::vector<PluginSequence>& chase);

    // Lets go of every note still sounding, and sends All Notes Off on every channel
    // the plugins were played on.
    void stop();

    bool isPlaying() const { return playing.load(); }
    double positionMs() const { return position.load(); } // where playback is, in sequence time

private:
    struct Hosted
    {
        juce::String id;
        std::unique_ptr<juce::AudioPluginInstance> instance;
        juce::MidiMessageCollector midi;
        juce::AudioBuffer<float> buffer;
        juce::MidiBuffer block;

        // Its part of `sequences`, and the next message of it to play.
        const PluginSequence* sequence = nullptr;
        std::size_t next = 0;
        std::vector<MidiMessage> pending; // sent at the start of the next block
        std::bitset<16 * 128> held;       // notes on: channel * 128 + key
        std::bitset<16> channels;         // played on since the last release
        bool releaseAll = false;          // at the start of the next block
    };

    Hosted* find(const juce::String& id);
    void prepare(Hosted& hosted); // for the current device; under `lock`
    void attach(Hosted& hosted);  // its sequence, from where playback is; under `lock`
    void stopLocked();
    static void add(Hosted& hosted, const MidiMessage& message, int sample); // to its block, noting held notes

    void changeListenerCallback(juce::ChangeBroadcaster*) override; // the device changed
    void saveDeviceSettings();

    void audioDeviceIOCallbackWithContext(const float* const* inputs, int numInputs, float* const* outputs,
                                          int numOutputs, int numSamples,
                                          const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    juce::AudioDeviceManager deviceManager;
    juce::AudioPluginFormatManager formats;
    std::unique_ptr<juce::PropertiesFile> settings;

    // The audio thread only tries this lock: while the message thread adds or removes
    // a plugin, it plays a block of silence instead of waiting.
    juce::CriticalSection lock;
    std::vector<std::unique_ptr<Hosted>> hosted;
    double sampleRate = 44100;
    int blockSize = 512;

    std::vector<PluginSequence> sequences; // under `lock`
    double endMs = 0;                      // a second after the last message
    double from = 0;                       // the start of the next block, in ms; under `lock`
    std::atomic<bool> playing { false };
    std::atomic<double> position { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioHost)
};

} // namespace msd
