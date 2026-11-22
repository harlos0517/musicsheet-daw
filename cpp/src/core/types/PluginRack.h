#pragma once

// The plugins scores play on, and which score tracks each channel plays. One file shared
// by every score, like a DAW template: %APPDATA%\Musicsheet DAW\rack.json, with each
// plugin's saved state (for Kontakt: its loaded patches) in rack\<id>.state beside it.
//
// Only the C++ app uses it, so unlike the other types it has no TS original.

#include <optional>
#include <string>
#include <vector>

namespace msd
{

// One channel of a plugin: one track of a DAW template.
struct PluginSlot
{
    std::string id;   // "flute-1"
    std::string name; // "Flute 1", shown in the app
    int channel = 1;  // 1-16. The profile's channel 1 goes here, its channel 2 to the next, and so on.
    std::vector<std::string> instrumentNames;          // score tracks it plays, by instrument name, any case
    std::optional<std::vector<std::string>> outputIds; // only these outputs of such a track ("toms"); default: all
};

// One plugin: a Kontakt with several patches loaded.
struct PluginInstance
{
    std::string id;         // "kontakt-woodwinds"; also names its state file
    std::string name;       // shown in the app
    std::string pluginFile; // the .vst3, as found in the VST3 folders
    std::vector<PluginSlot> slots;
};

struct PluginRack
{
    std::vector<PluginInstance> plugins;
};

} // namespace msd
