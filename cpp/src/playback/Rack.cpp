#include "playback/Rack.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>

namespace msd
{
namespace
{

std::string lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

bool plays(const PluginSlot& slot, const std::string& instrumentName, const std::string& outputId)
{
    const std::string name = lower(instrumentName);
    const bool named = std::any_of(slot.instrumentNames.begin(), slot.instrumentNames.end(),
                                   [&](const std::string& candidate) { return lower(candidate) == name; });
    if (!named)
        return false;
    return !slot.outputIds
           || std::find(slot.outputIds->begin(), slot.outputIds->end(), outputId) != slot.outputIds->end();
}

const Track* trackById(const MusicData& music, const NodeId& id)
{
    const auto found =
        std::find_if(music.tracks.begin(), music.tracks.end(), [&](const Track& t) { return t.id == id; });
    return found == music.tracks.end() ? nullptr : &*found;
}

std::string library(const std::string& profileId) { return profileId.substr(0, profileId.find('-')); }

// "Flute 1" → "flute-1"; made unique among the plugin's slots.
std::string slotId(const std::string& name, const PluginInstance& plugin)
{
    std::string base;
    for (const unsigned char c : name)
    {
        if (std::isalnum(c))
            base += static_cast<char>(std::tolower(c));
        else if (!base.empty() && base.back() != '-')
            base += '-';
    }
    while (!base.empty() && base.back() == '-')
        base.pop_back();
    if (base.empty())
        base = "slot";
    auto taken = [&](const std::string& id) {
        return std::any_of(plugin.slots.begin(), plugin.slots.end(), [&](const PluginSlot& s) { return s.id == id; });
    };
    std::string id = base;
    for (int n = 2; taken(id); ++n)
        id = base + "-" + std::to_string(n);
    return id;
}

std::optional<int> freeChannel(const PluginInstance& plugin)
{
    std::set<int> used;
    for (const PluginSlot& slot : plugin.slots)
        used.insert(slot.channel);
    for (int channel = 1; channel <= 16; ++channel)
        if (!used.contains(channel))
            return channel;
    return std::nullopt;
}

} // namespace

std::optional<SlotRef> findSlot(const PluginRack& rack, const std::string& instrumentName, const std::string& outputId)
{
    for (std::size_t p = 0; p < rack.plugins.size(); ++p)
        for (std::size_t s = 0; s < rack.plugins[p].slots.size(); ++s)
            if (plays(rack.plugins[p].slots[s], instrumentName, outputId))
                return SlotRef { p, s };
    return std::nullopt;
}

std::optional<int> slotChannel(const PluginSlot& slot, int channel)
{
    const int result = slot.channel + channel - 1;
    if (result < 1 || result > 16)
        return std::nullopt;
    return result;
}

std::vector<std::optional<SlotRef>> routeTracks(const PluginRack& rack, const MusicData& music,
                                                const std::vector<RenderedTrack>& tracks,
                                                std::vector<std::string>& warnings)
{
    std::vector<std::optional<SlotRef>> routes;
    routes.reserve(tracks.size());
    for (const RenderedTrack& rendered : tracks)
    {
        const Track* track = trackById(music, rendered.trackId);
        std::optional<SlotRef> slot;
        if (track != nullptr)
            slot = findSlot(rack, track->instrumentName, rendered.outputId);
        if (!slot)
            warnings.push_back(rendered.name + " (track " + rendered.trackId + ", output " + rendered.outputId
                               + "): no plugin slot");
        routes.push_back(slot);
    }
    return routes;
}

PluginRack addSlotsForScore(PluginRack rack, const MusicData& music, const PlaybackSetup& setup,
                            std::size_t fallbackPlugin, std::vector<std::string>& added)
{
    if (rack.plugins.empty())
    {
        added.push_back("The rack has no plugin to add slots to");
        return rack;
    }
    fallbackPlugin = std::min(fallbackPlugin, rack.plugins.size() - 1);

    // Which plugin each library already plays on, from the outputs that have a slot.
    std::map<std::string, std::size_t> pluginOfLibrary;
    for (const Track& track : music.tracks)
    {
        const auto found = setup.trackProfiles.find(track.id);
        if (found == setup.trackProfiles.end())
            continue;
        for (const TrackOutput& output : found->second.outputs)
            if (const std::optional<SlotRef> slot = findSlot(rack, track.instrumentName, output.id))
                pluginOfLibrary.emplace(library(output.instrumentProfileId), slot->plugin);
    }

    for (const Track& track : music.tracks)
    {
        const auto found = setup.trackProfiles.find(track.id);
        if (found == setup.trackProfiles.end())
            continue;
        const std::vector<TrackOutput>& outputs = found->second.outputs;
        for (const TrackOutput& output : outputs)
        {
            if (findSlot(rack, track.instrumentName, output.id))
                continue;
            const std::string lib = library(output.instrumentProfileId);
            const auto known = pluginOfLibrary.find(lib);
            const std::size_t p = known != pluginOfLibrary.end() ? known->second : fallbackPlugin;
            PluginInstance& plugin = rack.plugins[p];

            const std::string name = outputs.size() > 1 ? track.instrumentName + " - " + output.name.value_or(output.id)
                                                        : track.instrumentName;
            const std::optional<int> channel = freeChannel(plugin);
            if (!channel)
            {
                added.push_back(name + ": " + plugin.name + " has no free channel");
                continue;
            }
            PluginSlot slot { .id = slotId(name, plugin),
                              .name = name,
                              .channel = *channel,
                              .instrumentNames = { track.instrumentName },
                              .outputIds = {} };
            if (outputs.size() > 1)
                slot.outputIds = std::vector<std::string> { output.id };
            plugin.slots.push_back(std::move(slot));
            pluginOfLibrary.emplace(lib, p);
            added.push_back(name + ": " + plugin.name + ", channel " + std::to_string(*channel) + " (profile "
                            + output.instrumentProfileId + "): load its patch there");
        }
    }
    return rack;
}

} // namespace msd
