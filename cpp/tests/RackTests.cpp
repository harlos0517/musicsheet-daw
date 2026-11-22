// The plugin template: reading and writing rack.json, and which slot plays what.

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "core/json/Load.h"
#include "core/json/Save.h"
#include "playback/Rack.h"

using namespace msd;

namespace
{

PluginSlot slot(const std::string& id, int channel, std::vector<std::string> names,
                std::optional<std::vector<std::string>> outputIds = {})
{
    return PluginSlot {
        .id = id, .name = id, .channel = channel, .instrumentNames = std::move(names), .outputIds = std::move(outputIds)
    };
}

PluginRack rack()
{
    return PluginRack { .plugins = {
                            PluginInstance {
                                .id = "woodwinds",
                                .name = "Woodwinds",
                                .pluginFile = "Kontakt.vst3",
                                .slots = { slot("flute-1", 2, { "Flute 1", "Flute" }), slot("oboe", 3, { "Oboe" }) } },
                            PluginInstance {
                                .id = "percussion",
                                .name = "Percussion",
                                .pluginFile = "Kontakt.vst3",
                                .slots = { slot("drums", 1, { "Percussion" }, std::vector<std::string> { "main" }),
                                           slot("toms", 2, { "Percussion" }, std::vector<std::string> { "toms" }) } },
                        } };
}

// A score with these tracks (id = instrument name), each with the outputs given as
// { output ID, instrument profile }.
struct TrackSpec
{
    std::string name;
    std::vector<std::pair<std::string, std::string>> outputs;
};

std::pair<MusicData, PlaybackSetup> score(const std::vector<TrackSpec>& tracks)
{
    MusicData music;
    PlaybackSetup setup;
    for (const TrackSpec& spec : tracks)
    {
        music.tracks.push_back(Track { .id = spec.name, .instrumentName = spec.name });
        TrackPlayback playback { .interpretationProfileId = "default", .outputs = {} };
        for (const auto& [id, profile] : spec.outputs)
            playback.outputs.push_back(
                TrackOutput { .id = id, .name = {}, .instrumentProfileId = profile, .sounds = {} });
        setup.trackProfiles[spec.name] = playback;
    }
    return { music, setup };
}

} // namespace

TEST_CASE("rack.json reads back what was written", "[rack]")
{
    const PluginRack written = rack();
    const PluginRack read = parsePluginRack(writePluginRack(written));
    REQUIRE(read.plugins.size() == 2);
    CHECK(read.plugins[0].id == "woodwinds");
    CHECK(read.plugins[0].pluginFile == "Kontakt.vst3");
    REQUIRE(read.plugins[0].slots.size() == 2);
    CHECK(read.plugins[0].slots[0].channel == 2);
    CHECK(read.plugins[0].slots[0].instrumentNames == std::vector<std::string> { "Flute 1", "Flute" });
    CHECK_FALSE(read.plugins[0].slots[0].outputIds);
    CHECK(read.plugins[1].slots[1].outputIds == std::vector<std::string> { "toms" });
    CHECK(writePluginRack(read) == writePluginRack(written));
}

TEST_CASE("a slot channel must be 1-16", "[rack]")
{
    const std::string json = R"({ "plugins": [{ "id": "k", "name": "K", "pluginFile": "k.vst3",
        "slots": [{ "id": "s", "name": "S", "channel": 17, "instrumentNames": [] }] }] })";
    CHECK_THROWS_AS(parsePluginRack(json), LoadError);
}

TEST_CASE("an output goes to the first slot naming its instrument, in any case", "[rack]")
{
    const PluginRack r = rack();
    CHECK(findSlot(r, "Flute 1", "main") == SlotRef { 0, 0 });
    CHECK(findSlot(r, "flute", "main") == SlotRef { 0, 0 });
    CHECK(findSlot(r, "Oboe", "main") == SlotRef { 0, 1 });
    CHECK_FALSE(findSlot(r, "Flute 3", "main"));
    // outputIds split one track over two slots.
    CHECK(findSlot(r, "Percussion", "main") == SlotRef { 1, 0 });
    CHECK(findSlot(r, "Percussion", "toms") == SlotRef { 1, 1 });
    CHECK_FALSE(findSlot(r, "Percussion", "cymbals"));
}

TEST_CASE("a profile's channels move to the slot's", "[rack]")
{
    const PluginSlot s = slot("s", 5, {});
    CHECK(slotChannel(s, 1) == 5);
    CHECK(slotChannel(s, 3) == 7);
    CHECK_FALSE(slotChannel(slot("t", 16, {}), 2));
}

TEST_CASE("a rendered track without a slot is warned about", "[rack]")
{
    const auto [music, setup] = score({ { "Flute 1", { { "main", "csw" } } }, { "Flute 3", { { "main", "csw" } } } });
    std::vector<std::string> warnings;
    const std::vector<RenderedTrack> tracks { { "Flute 1", "main", "Flute 1", {} },
                                              { "Flute 3", "main", "Flute 3", {} } };
    const auto routes = routeTracks(rack(), music, tracks, warnings);
    REQUIRE(routes.size() == 2);
    CHECK(routes[0] == SlotRef { 0, 0 });
    CHECK_FALSE(routes[1]);
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0].find("Flute 3") != std::string::npos);
}

TEST_CASE("slots are added on the plugin that plays the same library", "[rack]")
{
    const auto [music, setup] = score({ { "Flute 1", { { "main", "csw" } } },
                                        { "Piccolo", { { "main", "csw-piccolo" } } },
                                        { "Violins 1", { { "main", "css" } } },
                                        { "Drums", { { "main", "ssp-snare" }, { "toms", "ssp-toms" } } } });
    std::vector<std::string> added;
    const PluginRack r = addSlotsForScore(rack(), music, setup, 1, added);

    // Piccolo: the same library as Flute 1, so on Woodwinds, on its lowest free channel.
    const auto piccolo = findSlot(r, "Piccolo", "main");
    REQUIRE(piccolo);
    CHECK(piccolo->plugin == 0);
    CHECK(r.plugins[0].slots[piccolo->slot].channel == 1);
    CHECK(r.plugins[0].slots[piccolo->slot].id == "piccolo");
    // No plugin plays css or ssp yet: the fallback, Percussion; channels 1 and 2 are taken.
    const auto violins = findSlot(r, "Violins 1", "main");
    REQUIRE(violins);
    CHECK(violins->plugin == 1);
    CHECK(r.plugins[1].slots[violins->slot].channel == 3);
    // Two outputs of one track: a slot each, named after the output.
    const auto toms = findSlot(r, "Drums", "toms");
    REQUIRE(toms);
    CHECK(r.plugins[toms->plugin].slots[toms->slot].name == "Drums - toms");
    CHECK(r.plugins[toms->plugin].slots[toms->slot].outputIds == std::vector<std::string> { "toms" });
    CHECK(findSlot(r, "Drums", "main") != toms);
    CHECK(added.size() == 4);

    // Already there: nothing more.
    std::vector<std::string> again;
    CHECK(writePluginRack(addSlotsForScore(r, music, setup, 1, again)) == writePluginRack(r));
    CHECK(again.empty());
}

TEST_CASE("a full plugin gets no more slots", "[rack]")
{
    PluginRack full { .plugins = { PluginInstance { .id = "k", .name = "K", .pluginFile = "k.vst3", .slots = {} } } };
    for (int c = 1; c <= 16; ++c)
        full.plugins[0].slots.push_back(slot("s" + std::to_string(c), c, { "x" }));
    const auto [music, setup] = score({ { "Flute 1", { { "main", "csw" } } } });
    std::vector<std::string> added;
    const PluginRack r = addSlotsForScore(full, music, setup, 0, added);
    CHECK(r.plugins[0].slots.size() == 16);
    REQUIRE(added.size() == 1);
    CHECK(added[0].find("no free channel") != std::string::npos);
}
