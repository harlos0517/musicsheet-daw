#include "core/json/Save.h"

#include <fstream>
#include <stdexcept>
#include <system_error>

#include <nlohmann/json.hpp>

namespace msd
{
namespace
{

using Json = nlohmann::ordered_json; // keeps the struct's field order

Json toJson(const PluginSlot& slot)
{
    Json j { { "id", slot.id },
             { "name", slot.name },
             { "channel", slot.channel },
             { "instrumentNames", slot.instrumentNames } };
    if (slot.outputIds)
        j["outputIds"] = *slot.outputIds;
    return j;
}

Json toJson(const PluginInstance& plugin)
{
    Json slots = Json::array();
    for (const PluginSlot& slot : plugin.slots)
        slots.push_back(toJson(slot));
    return { { "id", plugin.id }, { "name", plugin.name }, { "pluginFile", plugin.pluginFile }, { "slots", slots } };
}

} // namespace

std::string writePluginRack(const PluginRack& rack)
{
    Json plugins = Json::array();
    for (const PluginInstance& plugin : rack.plugins)
        plugins.push_back(toJson(plugin));
    return Json { { "plugins", plugins } }.dump(2) + "\n";
}

void savePluginRack(const PluginRack& rack, const std::filesystem::path& file)
{
    std::filesystem::path temporary = file;
    temporary += ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out << writePluginRack(rack);
        if (!out)
            throw std::runtime_error("can't write " + temporary.string());
    }
    std::error_code error;
    std::filesystem::rename(temporary, file, error);
    if (error)
        throw std::runtime_error("can't replace " + file.string() + ": " + error.message());
}

} // namespace msd
