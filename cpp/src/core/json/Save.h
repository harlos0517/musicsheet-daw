#pragma once

// Writes the app's own files as JSON, in the shape Load.h reads them. Fields keep the
// order of the struct; empty optionals are left out.

#include <filesystem>
#include <string>

#include "core/types/PluginRack.h"

namespace msd
{

std::string writePluginRack(const PluginRack& rack);

// Writes a temporary file next to `file` first and renames it, so a crash never leaves
// half a file. Throws std::runtime_error when it can't.
void savePluginRack(const PluginRack& rack, const std::filesystem::path& file);

} // namespace msd
