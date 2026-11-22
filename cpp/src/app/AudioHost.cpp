#include "AudioHost.h"

#include <algorithm>

namespace msd
{
namespace
{

constexpr const char* deviceKey = "audioDevice";

std::unique_ptr<juce::PropertiesFile> openSettings()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "Musicsheet DAW";
    options.folderName = "Musicsheet DAW";
    options.filenameSuffix = ".settings";
    options.osxLibrarySubFolder = "Application Support";
    return std::make_unique<juce::PropertiesFile>(options);
}

} // namespace

AudioHost::AudioHost()
    : settings(openSettings())
{
    formats.addFormat(std::make_unique<juce::VST3PluginFormat>());

    // No inputs; a stereo output. The saved state, if any, picks the device.
    const std::unique_ptr<juce::XmlElement> saved = settings->getXmlValue(deviceKey);
    deviceManager.initialise(0, 2, saved.get(), true);
    deviceManager.addAudioCallback(this);
    deviceManager.addChangeListener(this);
}

AudioHost::~AudioHost()
{
    deviceManager.removeChangeListener(this);
    deviceManager.removeAudioCallback(this); // stops the device first: nothing plays after this
    const juce::ScopedLock locked(lock);
    hosted.clear();
}

void AudioHost::changeListenerCallback(juce::ChangeBroadcaster*) { saveDeviceSettings(); }

void AudioHost::saveDeviceSettings()
{
    if (const std::unique_ptr<juce::XmlElement> state = deviceManager.createStateXml())
        settings->setValue(deviceKey, state.get());
    else
        settings->removeValue(deviceKey); // the default device
    settings->saveIfNeeded();
}

juce::StringArray AudioHost::findPlugins()
{
    juce::VST3PluginFormat vst3;
    juce::StringArray files = vst3.searchPathsForPlugins(vst3.getDefaultLocationsToSearch(), true, false);
    files.sortNatural();
    return files;
}

AudioHost::Hosted* AudioHost::find(const juce::String& id)
{
    const auto found = std::find_if(hosted.begin(), hosted.end(), [&](const auto& h) { return h->id == id; });
    return found == hosted.end() ? nullptr : found->get();
}

void AudioHost::loadPlugin(const juce::String& id, const juce::String& file, const juce::MemoryBlock& state,
                           std::function<void(const juce::String& error)> done)
{
    juce::OwnedArray<juce::PluginDescription> types;
    juce::VST3PluginFormat().findAllTypesForFile(types, file);
    if (types.isEmpty())
    {
        done("No plugin found in " + file);
        return;
    }
    formats.createPluginInstanceAsync(
        *types[0], sampleRate, blockSize,
        [this, id, state, done = std::move(done)](std::unique_ptr<juce::AudioPluginInstance> created,
                                                  const juce::String& error) {
            if (created == nullptr)
            {
                done(error.isEmpty() ? juce::String("The plugin could not be loaded") : error);
                return;
            }
            if (!state.isEmpty())
                created->setStateInformation(state.getData(), static_cast<int>(state.getSize()));

            auto entry = std::make_unique<Hosted>();
            entry->id = id;
            entry->instance = std::move(created);
            std::unique_ptr<Hosted> old; // deleted below, outside the lock
            {
                const juce::ScopedLock locked(lock);
                prepare(*entry);
                const auto found =
                    std::find_if(hosted.begin(), hosted.end(), [&](const auto& h) { return h->id == id; });
                if (found != hosted.end())
                    old = std::exchange(*found, std::move(entry));
                else
                    hosted.push_back(std::move(entry));
            }
            if (old != nullptr)
                old->instance->releaseResources();
            done({});
        });
}

void AudioHost::removePlugin(const juce::String& id)
{
    std::unique_ptr<Hosted> removed;
    {
        const juce::ScopedLock locked(lock);
        const auto found = std::find_if(hosted.begin(), hosted.end(), [&](const auto& h) { return h->id == id; });
        if (found == hosted.end())
            return;
        removed = std::move(*found);
        hosted.erase(found);
    }
    removed->instance->releaseResources();
}

juce::AudioPluginInstance* AudioHost::plugin(const juce::String& id)
{
    Hosted* entry = find(id);
    return entry == nullptr ? nullptr : entry->instance.get();
}

juce::MemoryBlock AudioHost::pluginState(const juce::String& id)
{
    juce::MemoryBlock state;
    if (juce::AudioPluginInstance* instance = plugin(id))
        instance->getStateInformation(state);
    return state;
}

void AudioHost::sendMidi(const juce::String& id, const juce::MidiMessage& message)
{
    Hosted* entry = find(id);
    if (entry == nullptr)
        return;
    juce::MidiMessage timed(message);
    timed.setTimeStamp(juce::Time::getMillisecondCounterHiRes() * 0.001);
    entry->midi.addMessageToQueue(timed);
}

void AudioHost::prepare(Hosted& entry)
{
    juce::AudioPluginInstance& instance = *entry.instance;
    instance.setRateAndBufferSizeDetails(sampleRate, blockSize);
    instance.prepareToPlay(sampleRate, blockSize);
    entry.midi.reset(sampleRate);
    entry.buffer.setSize(std::max({ instance.getTotalNumInputChannels(), instance.getTotalNumOutputChannels(), 2 }),
                         blockSize);
    entry.block.ensureSize(4096);
}

void AudioHost::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    const juce::ScopedLock locked(lock);
    sampleRate = device->getCurrentSampleRate();
    blockSize = device->getCurrentBufferSizeSamples();
    for (const auto& entry : hosted)
        prepare(*entry);
}

void AudioHost::audioDeviceStopped()
{
    const juce::ScopedLock locked(lock);
    for (const auto& entry : hosted)
        entry->instance->releaseResources();
}

void AudioHost::audioDeviceIOCallbackWithContext(const float* const*, int, float* const* outputs, int numOutputs,
                                                 int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    for (int c = 0; c < numOutputs; ++c)
        if (outputs[c] != nullptr)
            juce::FloatVectorOperations::clear(outputs[c], numSamples);

    const juce::ScopedTryLock locked(lock);
    if (!locked.isLocked())
        return;
    for (const auto& entry : hosted)
    {
        juce::AudioBuffer<float>& buffer = entry->buffer;
        if (numSamples > buffer.getNumSamples())
            continue; // a block longer than prepared for: skip rather than allocate here
        // A view of the first numSamples samples of each channel.
        juce::AudioBuffer<float> view(buffer.getArrayOfWritePointers(), buffer.getNumChannels(), numSamples);
        view.clear();
        entry->block.clear();
        entry->midi.removeNextBlockOfMessages(entry->block, numSamples);
        entry->instance->processBlock(view, entry->block);
        // The plugin's first outputs: its main stereo pair.
        for (int c = 0; c < std::min(numOutputs, view.getNumChannels()); ++c)
            if (outputs[c] != nullptr)
                juce::FloatVectorOperations::add(outputs[c], view.getReadPointer(c), numSamples);
    }
}

} // namespace msd
