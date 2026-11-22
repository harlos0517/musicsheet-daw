#pragma once

// What each plugin of the rack plays: the rendered tracks, routed through the rack.

#include <optional>
#include <string>
#include <vector>

#include "core/types/MusicData.h"
#include "core/types/PlaybackSetup.h"
#include "core/types/PluginRack.h"
#include "playback/Play.h"
#include "playback/RenderMidi.h"

namespace msd
{

// What one plugin of the rack plays: the messages of every output routed to it,
// moved to their slot's channel, with their profile's trackDelayMs added
// (-66 ms: sent 66 ms early), sorted by time.
struct PluginSequence
{
    std::string pluginId;
    std::vector<MidiMessage> messages; // the MidiMessage we already have; timeMs may go below 0
};

// One per plugin that plays anything, in rack order. An output without a slot, or with
// a channel past 16 on it, is left out with a warning. `music` is the score `playback`
// was made from: the slots match its tracks' instrument names.
std::vector<PluginSequence> buildSequences(const MusicData& music, const Playback& playback, const PlaybackSetup& setup,
                                           const PluginRack& rack, std::vector<std::string>& warnings);

// To send before playing from `startMs`, so the patch starts as it would have been:
// on each channel, the last value of each CC before it, and the last keyswitch
// (pressed and let go; one still held at `startMs` is only pressed, its release
// comes later in the sequence). Notes already sounding there are not replayed.
std::vector<MidiMessage> chaseMessages(const PluginSequence& sequence, double startMs);

// When a measure is first played, in ms; nothing for a measure that isn't played.
std::optional<double> measureStartMs(const Playback& playback, const NodeId& measureId);

} // namespace msd
