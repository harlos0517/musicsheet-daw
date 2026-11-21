#pragma once

// Reads the JSON written by the TS side (`pnpm midi` → prototype-web/midi-out/*.music.json)
// into the C++ types.
//
// Strict about what it knows: a missing required field, a wrong type, an unknown
// `kind` or enum name is an error that says where it is, e.g.
//   tracks[3].staves[0].measures[12].voices[0].events[4].notes[0].pitch.step:
//   unknown name "h"
// Fields it doesn't know are ignored, so the TS side can add fields first.

#include <filesystem>
#include <stdexcept>
#include <string_view>

#include "core/types/LayoutSetup.h"
#include "core/types/MusicData.h"

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

} // namespace msd
