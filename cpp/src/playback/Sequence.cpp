#include "playback/Sequence.h"

#include <algorithm>
#include <map>
#include <set>

#include "playback/Rack.h"

namespace msd
{
namespace
{

const InstrumentProfile* profileOf(const PlaybackSetup& setup, const RenderedTrack& track)
{
    const auto playback = setup.trackProfiles.find(track.trackId);
    if (playback == setup.trackProfiles.end())
        return nullptr;
    const auto output = std::find_if(playback->second.outputs.begin(), playback->second.outputs.end(),
                                     [&](const TrackOutput& o) { return o.id == track.outputId; });
    if (output == playback->second.outputs.end())
        return nullptr;
    const auto profile = std::find_if(setup.instrumentProfiles.begin(), setup.instrumentProfiles.end(),
                                      [&](const InstrumentProfile& p) { return p.id == output->instrumentProfileId; });
    return profile == setup.instrumentProfiles.end() ? nullptr : &*profile;
}

} // namespace

std::vector<PluginSequence> buildSequences(const MusicData& music, const Playback& playback, const PlaybackSetup& setup,
                                           const PluginRack& rack, std::vector<std::string>& warnings)
{
    std::vector<std::vector<MidiMessage>> perPlugin(rack.plugins.size());
    const std::vector<std::optional<SlotRef>> routes = routeTracks(rack, music, playback.tracks, warnings);
    for (std::size_t t = 0; t < playback.tracks.size(); ++t)
    {
        if (!routes[t])
            continue;
        const RenderedTrack& track = playback.tracks[t];
        const PluginSlot& slot = rack.plugins[routes[t]->plugin].slots[routes[t]->slot];
        const InstrumentProfile* profile = profileOf(setup, track);
        const double delay = profile != nullptr ? profile->trackDelayMs : 0.0;
        std::vector<MidiMessage>& messages = perPlugin[routes[t]->plugin];
        bool pastSixteen = false;
        for (MidiMessage message : track.messages)
        {
            const std::optional<int> channel = slotChannel(slot, message.channel);
            if (!channel)
            {
                pastSixteen = true;
                continue;
            }
            message.channel = *channel;
            message.timeMs += delay;
            messages.push_back(message);
        }
        if (pastSixteen)
            warnings.push_back(track.name + ": some messages would go past channel 16 of slot " + slot.name
                               + ", left out");
    }

    std::vector<PluginSequence> sequences;
    for (std::size_t p = 0; p < rack.plugins.size(); ++p)
    {
        if (perPlugin[p].empty())
            continue;
        std::stable_sort(perPlugin[p].begin(), perPlugin[p].end(), [](const MidiMessage& a, const MidiMessage& b) {
            return a.timeMs != b.timeMs ? a.timeMs < b.timeMs : messageOrder(a) < messageOrder(b);
        });
        sequences.push_back({ rack.plugins[p].id, std::move(perPlugin[p]) });
    }
    return sequences;
}

std::vector<MidiMessage> chaseMessages(const PluginSequence& sequence, double startMs)
{
    std::map<std::pair<int, int>, MidiMessage> controllers; // (channel, number) → last value
    struct Keyswitches
    {
        double pressedAt = -1;
        std::vector<MidiMessage> lastPresses; // pressed together, last
        std::map<int, MidiMessage> held;      // key → its press
    };
    std::map<int, Keyswitches> keyswitches; // by channel

    for (const MidiMessage& message : sequence.messages)
    {
        if (message.timeMs >= startMs)
            break;
        if (message.type == MidiMessageType::Cc)
            controllers[{ message.channel, message.data1 }] = message;
        else if (message.source == MidiSource::Keyswitch)
        {
            Keyswitches& channel = keyswitches[message.channel];
            if (message.type == MidiMessageType::NoteOn)
            {
                if (message.timeMs != channel.pressedAt)
                    channel.lastPresses.clear();
                channel.pressedAt = message.timeMs;
                channel.lastPresses.push_back(message);
                channel.held[message.data1] = message;
            }
            else
                channel.held.erase(message.data1);
        }
    }

    std::vector<MidiMessage> chase;
    for (auto [key, message] : controllers)
    {
        message.timeMs = startMs;
        chase.push_back(message);
    }
    for (auto& [channel, state] : keyswitches)
    {
        std::set<int> pressed;
        for (MidiMessage press : state.lastPresses)
        {
            press.timeMs = startMs;
            chase.push_back(press);
            pressed.insert(press.data1);
            if (!state.held.contains(press.data1))
                chase.push_back({ startMs, MidiMessageType::NoteOff, channel, press.data1, 0, MidiSource::Keyswitch });
        }
        for (auto [key, press] : state.held)
            if (!pressed.contains(key))
            {
                press.timeMs = startMs;
                chase.push_back(press);
            }
    }
    return chase;
}

std::optional<double> measureStartMs(const Playback& playback, const NodeId& measureId)
{
    for (const PlayedMeasure& measure : playback.played)
    {
        const MeasureInfo& info = playback.timeline[static_cast<std::size_t>(measure.measureIndex)];
        if (info.measure != nullptr && info.measure->id == measureId)
            return playback.tempo.tickToMs(measure.startTick);
    }
    return std::nullopt;
}

} // namespace msd
