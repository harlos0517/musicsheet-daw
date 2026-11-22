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
                attach(*entry);
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
    const bool isPlaying = playing.load();
    const double to = from + numSamples * 1000.0 / sampleRate;
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

        if (entry->releaseAll)
        {
            for (std::size_t i = 0; i < entry->held.size(); ++i)
                if (entry->held[i])
                    add(*entry,
                        { 0, MidiMessageType::NoteOff, static_cast<int>(i / 128) + 1, static_cast<int>(i % 128), 0,
                          MidiSource::Note },
                        0);
            for (int c = 0; c < 16; ++c)
                if (entry->channels[static_cast<std::size_t>(c)])
                    add(*entry, { 0, MidiMessageType::Cc, c + 1, 123, 0, MidiSource::Note }, 0); // All Notes Off
            entry->held.reset();
            entry->channels.reset();
            entry->releaseAll = false;
        }
        for (const MidiMessage& message : entry->pending)
            add(*entry, message, 0);
        entry->pending.clear();
        if (isPlaying && entry->sequence != nullptr)
        {
            const std::vector<MidiMessage>& messages = entry->sequence->messages;
            for (; entry->next < messages.size() && messages[entry->next].timeMs < to; ++entry->next)
            {
                const MidiMessage& message = messages[entry->next];
                const double offset = (message.timeMs - from) * sampleRate / 1000.0;
                add(*entry, message, std::clamp(static_cast<int>(offset), 0, numSamples - 1));
            }
        }

        entry->instance->processBlock(view, entry->block);
        // The plugin's first outputs: its main stereo pair.
        for (int c = 0; c < std::min(numOutputs, view.getNumChannels()); ++c)
            if (outputs[c] != nullptr)
                juce::FloatVectorOperations::add(outputs[c], view.getReadPointer(c), numSamples);
    }

    if (isPlaying)
    {
        from = to;
        position.store(to);
        if (to >= endMs)
        {
            playing.store(false);
            for (const auto& entry : hosted)
                entry->releaseAll = true;
        }
    }
}

void AudioHost::add(Hosted& entry, const MidiMessage& message, int sample)
{
    const int channel = std::clamp(message.channel, 1, 16) - 1;
    const auto key = static_cast<std::size_t>(channel * 128 + (message.data1 & 0x7f));
    juce::uint8 status = 0xb0;
    switch (message.type)
    {
    case MidiMessageType::NoteOn:
        status = 0x90;
        entry.held[key] = message.data2 > 0;
        break;
    case MidiMessageType::NoteOff:
        status = 0x80;
        entry.held[key] = false;
        break;
    case MidiMessageType::Cc: break;
    }
    entry.channels[static_cast<std::size_t>(channel)] = true;
    const juce::uint8 bytes[] = { static_cast<juce::uint8>(status | channel),
                                  static_cast<juce::uint8>(message.data1 & 0x7f),
                                  static_cast<juce::uint8>(message.data2 & 0x7f) };
    entry.block.addEvent(bytes, 3, sample);
}

void AudioHost::attach(Hosted& entry)
{
    const auto found = std::find_if(sequences.begin(), sequences.end(), [&](const PluginSequence& s) {
        return juce::String::fromUTF8(s.pluginId.c_str()) == entry.id;
    });
    entry.sequence = found == sequences.end() ? nullptr : &*found;
    entry.next = 0;
    if (entry.sequence != nullptr)
    {
        const std::vector<MidiMessage>& messages = entry.sequence->messages;
        entry.next =
            static_cast<std::size_t>(std::lower_bound(messages.begin(), messages.end(), from,
                                                      [](const MidiMessage& m, double ms) { return m.timeMs < ms; })
                                     - messages.begin());
    }
}

void AudioHost::stopLocked()
{
    playing.store(false);
    for (const auto& entry : hosted)
        entry->releaseAll = true;
}

void AudioHost::setSequences(std::vector<PluginSequence> newSequences)
{
    const juce::ScopedLock locked(lock);
    stopLocked();
    sequences = std::move(newSequences);
    endMs = 0;
    for (const PluginSequence& sequence : sequences)
        if (!sequence.messages.empty())
            endMs = std::max(endMs, sequence.messages.back().timeMs);
    endMs += 1000;
    for (const auto& entry : hosted)
        attach(*entry);
}

void AudioHost::start(double fromMs, const std::vector<PluginSequence>& chase)
{
    const juce::ScopedLock locked(lock);
    if (playing.load())
        stopLocked();
    from = fromMs;
    position.store(fromMs);
    for (const auto& entry : hosted)
    {
        attach(*entry);
        for (const PluginSequence& messages : chase)
            if (juce::String::fromUTF8(messages.pluginId.c_str()) == entry->id)
                entry->pending.insert(entry->pending.end(), messages.messages.begin(), messages.messages.end());
    }
    playing.store(true);
}

void AudioHost::stop()
{
    const juce::ScopedLock locked(lock);
    stopLocked();
}

} // namespace msd
