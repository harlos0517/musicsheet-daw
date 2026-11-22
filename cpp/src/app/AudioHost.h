#pragma once

// The audio side of the app: the output device, and the VST3 plugins of the rack
// playing through it, mixed. Each plugin is known by its rack ID and gets its own MIDI.

#include <functional>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

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

private:
    struct Hosted
    {
        juce::String id;
        std::unique_ptr<juce::AudioPluginInstance> instance;
        juce::MidiMessageCollector midi;
        juce::AudioBuffer<float> buffer;
        juce::MidiBuffer block;
    };

    Hosted* find(const juce::String& id);
    void prepare(Hosted& hosted); // for the current device; under `lock`

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioHost)
};

} // namespace msd
