#pragma once

// Which plugin channel each output of a score plays on, by the shared PluginRack.

#include <optional>
#include <string>
#include <vector>

#include "core/types/MusicData.h"
#include "core/types/PlaybackSetup.h"
#include "core/types/PluginRack.h"
#include "playback/RenderMidi.h"

namespace msd
{

struct SlotRef
{
    std::size_t plugin = 0; // into PluginRack::plugins
    std::size_t slot = 0;   // into that plugin's slots
    bool operator==(const SlotRef&) const = default;
};

// The first slot that plays this output of a track: one whose instrumentNames have the
// track's instrument name (any case), and whose outputIds, if it has them, the output.
std::optional<SlotRef> findSlot(const PluginRack& rack, const std::string& instrumentName, const std::string& outputId);

// The plugin channel a message on the profile's `channel` goes to: the slot's channel
// for channel 1, the next one for 2, and so on. Nothing past 16.
std::optional<int> slotChannel(const PluginSlot& slot, int channel);

// The slot of each of `tracks`, in the same order; nothing, with a warning, for a track
// no slot plays.
std::vector<std::optional<SlotRef>> routeTracks(const PluginRack& rack, const MusicData& music,
                                                const std::vector<RenderedTrack>& tracks,
                                                std::vector<std::string>& warnings);

// The rack with a slot for every output of the score that no slot plays yet, on the
// lowest free channel of a plugin: the one that already plays another output of the
// same library (the profile ID up to its first '-': "csw-piccolo" is "csw"), else
// `fallbackPlugin`. Each new slot is named after the instrument (and the output, for a
// track with several), and `added` gets a line saying where its patch should be
// loaded, or why there was no room.
PluginRack addSlotsForScore(PluginRack rack, const MusicData& music, const PlaybackSetup& setup,
                            std::size_t fallbackPlugin, std::vector<std::string>& added);

} // namespace msd
