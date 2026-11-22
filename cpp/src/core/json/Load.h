#pragma once

// Reads the JSON written by the TS side into the C++ types: `pnpm midi` writes
// prototype-web/midi-out/*.music.json, *.setup.json and *.events.json.
//
// Strict about what it knows: a missing required field, a wrong type, an unknown
// `kind` or enum name is an error that says where it is, e.g.
//   tracks[3].staves[0].measures[12].voices[0].events[4].notes[0].pitch.step:
//   unknown name "h"
// Fields it doesn't know are ignored, so the TS side can add fields first.

#include <filesystem>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "core/types/LayoutSetup.h"
#include "core/types/MusicData.h"
#include "core/types/MusicEvent.h"
#include "core/types/PlaybackSetup.h"
#include "core/types/PluginRack.h"

namespace msd
{

class LoadError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

// All of these throw LoadError when the JSON is invalid or doesn't match the types.
MusicData parseMusicData(std::string_view json);
MusicData loadMusicData(const std::filesystem::path& file);

LayoutSetup parseLayoutSetup(std::string_view json);
LayoutSetup loadLayoutSetup(const std::filesystem::path& file);

PlaybackSetup parsePlaybackSetup(std::string_view json);
PlaybackSetup loadPlaybackSetup(const std::filesystem::path& file);

// The app's plugin template (rack.json). Written by savePluginRack (Save.h).
PluginRack parsePluginRack(std::string_view json);
PluginRack loadPluginRack(const std::filesystem::path& file);

// A JSON array of note and curve events, as in *.events.json.
std::vector<MusicEvent> parseMusicEvents(std::string_view json);
std::vector<MusicEvent> loadMusicEvents(const std::filesystem::path& file);

} // namespace msd
