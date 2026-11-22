// MusicEvents → MIDI messages → a MIDI file. Ported from the TS tests in
// playback.test.ts, and checked against the *.midi.json and *.mid files `pnpm midi`
// writes.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/json/Load.h"
#include "playback/BuildEvents.h"
#include "playback/MidiFile.h"
#include "playback/RenderMidi.h"

using namespace msd;

namespace
{

const std::filesystem::path exported = MSD_EXPORTED_SCORES_DIR;

void requireExported(const std::string& file)
{
    if (!std::filesystem::exists(exported / file))
        SKIP("no " << (exported / file).string() << "; run pnpm midi in prototype-web first");
}

const InstrumentProfile& profileNamed(const PlaybackSetup& setup, const std::string& id)
{
    const auto profile = std::find_if(setup.instrumentProfiles.begin(), setup.instrumentProfiles.end(),
                                      [&](const InstrumentProfile& p) { return p.id == id; });
    REQUIRE(profile != setup.instrumentProfiles.end());
    return *profile;
}

// Points into `messages`, which must outlive the result.
std::vector<const MidiMessage*> controllers(const std::vector<MidiMessage>& messages, int number)
{
    std::vector<const MidiMessage*> result;
    for (const MidiMessage& message : messages)
        if (message.type == MidiMessageType::Cc && message.data1 == number)
            result.push_back(&message);
    return result;
}

// A ramp of the dynamics curve from 0 to `toValue` over `beats` quarters, at 60 bpm
// (one quarter is one second), through `profile`.
std::vector<MidiMessage> ramp(const InstrumentProfile& profile, double beats, double toValue)
{
    constexpr int tpq = 480;
    const TempoMap tempo({ { 0, 60 } }, tpq);
    const CurveEvent curve { .id = "ramp",
                             .trackId = "t",
                             .durationTicks = beats * tpq,
                             .fromValue = 0,
                             .toValue = toValue,
                             .curveType = CurveType::Linear };
    return renderTrack({ curve }, profile, tempo, {}).messages;
}

std::string number(double value)
{
    char text[32];
    std::snprintf(text, sizeof text, "%.17g", value + 0.0);
    return text;
}

std::string describe(const MidiMessage& m)
{
    return number(m.timeMs) + " " + std::string(enumName(m.type)) + " ch" + std::to_string(m.channel) + " "
           + std::to_string(m.data1) + " " + std::to_string(m.data2) + " " + std::string(enumName(m.source));
}

std::string describe(const nlohmann::json& m)
{
    return number(m.at("timeMs").get<double>()) + " " + m.at("type").get<std::string>() + " ch"
           + std::to_string(m.at("channel").get<int>()) + " " + std::to_string(m.at("data1").get<int>()) + " "
           + std::to_string(m.at("data2").get<int>()) + " " + m.at("source").get<std::string>();
}

// Plays a score `pnpm midi` exported and compares every output's messages with the ones
// the TS wrote, exactly. Shows the first few differences of each track and counts the rest.
void compareWithExported(const std::string& name)
{
    INFO(name);
    const MusicData music = loadMusicData(exported / (name + ".music.json"));
    const PlaybackSetup setup = loadPlaybackSetup(exported / (name + ".setup.json"));
    std::ifstream in(exported / (name + ".midi.json"));
    const nlohmann::json expected = nlohmann::json::parse(in);

    EventResult result = buildMusicEvents(music, setup);
    const std::vector<RenderedTrack> tracks = renderTracks(music, setup, result.events, result.tempo, result.warnings);
    REQUIRE(tracks.size() == expected.size());
    for (std::size_t t = 0; t < tracks.size(); ++t)
    {
        const RenderedTrack& track = tracks[t];
        const nlohmann::json& other = expected[t];
        INFO("track " << track.trackId << ", output " << track.outputId);
        CHECK(track.trackId == other.at("trackId").get<std::string>());
        CHECK(track.outputId == other.at("outputId").get<std::string>());
        CHECK(track.name == other.at("name").get<std::string>());
        const nlohmann::json& messages = other.at("messages");
        CHECK(track.messages.size() == messages.size());
        int different = 0;
        for (std::size_t i = 0; i < std::min(track.messages.size(), messages.size()); ++i)
        {
            const std::string actual = describe(track.messages[i]);
            const std::string wanted = describe(messages[i]);
            if (actual == wanted)
                continue;
            if (++different <= 5)
            {
                INFO("message " << i);
                CHECK(actual == wanted);
            }
        }
        CHECK(different == 0);
    }
}

// The whole chain, as play() in index.ts runs it, down to the file's bytes.
std::vector<std::uint8_t> playToFile(const MusicData& music, const PlaybackSetup& setup)
{
    EventResult result = buildMusicEvents(music, setup);
    const std::vector<RenderedTrack> tracks = renderTracks(music, setup, result.events, result.tempo, result.warnings);
    return writeMidiFile(music.title, music.tpq, result.tempo, result.played, measureTimeline(music), tracks);
}

// Compares the file C++ writes with the .mid the TS wrote, byte for byte, and says
// where they first differ.
void compareFileWithExported(const std::string& name)
{
    INFO(name);
    const MusicData music = loadMusicData(exported / (name + ".music.json"));
    const PlaybackSetup setup = loadPlaybackSetup(exported / (name + ".setup.json"));
    std::ifstream in(exported / (name + ".mid"), std::ios::binary);
    const std::vector<std::uint8_t> expected { std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };

    const std::vector<std::uint8_t> actual = playToFile(music, setup);
    CHECK(actual.size() == expected.size());
    const auto [mine, theirs] = std::mismatch(actual.begin(), actual.end(), expected.begin(), expected.end());
    INFO("first difference at byte " << (mine - actual.begin()));
    CHECK((mine == actual.end() && theirs == expected.end()));
}

} // namespace

TEST_CASE("simultaneous messages go releases, controllers, keyswitches, notes", "[playback][midi]")
{
    const auto order = [](MidiMessageType type, MidiSource source) {
        return messageOrder({ 0, type, 1, 0, 0, source });
    };
    CHECK(order(MidiMessageType::NoteOff, MidiSource::Note) < order(MidiMessageType::Cc, MidiSource::Curve));
    CHECK(order(MidiMessageType::Cc, MidiSource::Curve) < order(MidiMessageType::NoteOn, MidiSource::Keyswitch));
    CHECK(order(MidiMessageType::NoteOn, MidiSource::Keyswitch) < order(MidiMessageType::NoteOn, MidiSource::Note));
}

TEST_CASE("a slow ramp sends every value, when it is reached", "[playback][midi]")
{
    requireExported("melody.setup.json");
    const PlaybackSetup setup = loadPlaybackSetup(exported / "melody.setup.json");
    // 0 → 127 over four seconds: one step about every 31 ms.
    const std::vector<MidiMessage> messages = ramp(profileNamed(setup, "wind"), 4, 1);
    const std::vector<const MidiMessage*> cc = controllers(messages, 1);
    REQUIRE(cc.size() == 128);
    for (int i = 0; i < 128; ++i)
        CHECK(cc[i]->data2 == i);
    // 63.5 rounds up to 64, reached halfway; found within the 1 ms check.
    CHECK(std::abs(cc[64]->timeMs - 2000) <= 1);
}

TEST_CASE("a ramp spreads values over the output range along its curve", "[playback][midi]")
{
    requireExported("melody.setup.json");
    const PlaybackSetup setup = loadPlaybackSetup(exported / "melody.setup.json");
    InstrumentProfile curved = profileNamed(setup, "wind");
    curved.curveMappings = { CurveMapping {
        .parameter = BuiltInCurveParameter::Dynamics,
        .defaultValue = 0.5,
        .cc =
            std::vector<CurveCc> {
                { .number = 1, .mapping = { .low = 0, .high = 127, .curve = CurveShape { CurveType::Power, 1.0 } } } },
    } };
    const std::vector<MidiMessage> messages = ramp(curved, 4, 1);
    const std::vector<const MidiMessage*> cc = controllers(messages, 1);
    REQUIRE_FALSE(cc.empty());
    // Halfway up the ramp (0.5) is a quarter of the range: 0.5 squared.
    const auto half = std::find_if(cc.rbegin(), cc.rend(), [](const MidiMessage* m) { return m->timeMs <= 2000; });
    REQUIRE(half != cc.rend());
    CHECK((*half)->data2 == 32); // Math.round(127 * 0.25)
    CHECK(cc.back()->data2 == 127);
}

TEST_CASE("a fast ramp keeps 10 ms apart and still ends on the target", "[playback][midi]")
{
    requireExported("melody.setup.json");
    const PlaybackSetup setup = loadPlaybackSetup(exported / "melody.setup.json");
    // 0 → 127 in a quarter of a second: a step every 2 ms, too dense to send all.
    const std::vector<MidiMessage> messages = ramp(profileNamed(setup, "wind"), 0.25, 1);
    const std::vector<const MidiMessage*> cc = controllers(messages, 1);
    REQUIRE(cc.size() >= 2);
    CHECK(cc.size() <= 27);
    for (std::size_t i = 1; i + 1 < cc.size(); ++i)
        CHECK(cc[i]->timeMs - cc[i - 1]->timeMs >= 10 - 1e-6);
    CHECK(cc.back()->data2 == 127);
    CHECK(std::abs(cc.back()->timeMs - 250) < 0.01);
}

TEST_CASE("a keyswitch is pressed again when only its velocity changes", "[playback][midi]")
{
    requireExported("melody.setup.json");
    // Some libraries pick the articulation by how hard one key is pressed.
    const MusicData music = loadMusicData(exported / "melody.music.json");
    PlaybackSetup setup = loadPlaybackSetup(exported / "melody.setup.json");
    InstrumentProfile wind = profileNamed(setup, "wind");
    wind.techniqueMappings = {
        TechniqueMapping { .keyswitch = Keyswitch { 24, 100 } },
        TechniqueMapping { .matches = { TechniqueMatch { "core:staccato" } }, .keyswitch = Keyswitch { 24, 30 } },
    };
    setup.instrumentProfiles = { wind };
    EventResult result = buildMusicEvents(music, setup);
    const std::vector<RenderedTrack> tracks = renderTracks(music, setup, result.events, result.tempo, result.warnings);
    REQUIRE_FALSE(tracks.empty());
    std::vector<std::pair<int, int>> presses;
    for (const MidiMessage& m : tracks[0].messages)
        if (m.source == MidiSource::Keyswitch && m.type == MidiMessageType::NoteOn)
            presses.emplace_back(m.data1, m.data2);
    CHECK(presses == std::vector<std::pair<int, int>> { { 24, 100 }, { 24, 30 }, { 24, 100 } });
}

TEST_CASE("the example scores render as in TS", "[playback][midi]")
{
    for (const std::string name : { "melody", "band", "piano", "strings" })
    {
        requireExported(name + ".midi.json");
        compareWithExported(name);
    }
}

// The imported scores too, with the Cinematic Studio profiles. Run with:
// msd_tests "[exported]"
TEST_CASE("MIDI messages match every exported score", "[.][exported]")
{
    int compared = 0;
    if (std::filesystem::exists(exported))
        for (const auto& entry : std::filesystem::directory_iterator(exported))
        {
            const std::string file = entry.path().filename().string();
            const std::string suffix = ".midi.json";
            if (!file.ends_with(suffix))
                continue;
            compareWithExported(file.substr(0, file.size() - suffix.size()));
            ++compared;
        }
    if (compared == 0)
        SKIP("no " << exported.string() << "/*.midi.json; run pnpm midi in prototype-web first");
}

TEST_CASE("a pickup bar and a short last bar get their own time signature", "[playback][midi]")
{
    requireExported("melody.setup.json");
    const MusicData music = loadMusicData(exported / "melody.music.json");
    const PlaybackSetup setup = loadPlaybackSetup(exported / "melody.setup.json");
    const std::vector<std::uint8_t> file = playToFile(music, setup);

    // The conductor track is the first after the 14-byte header; its events carry no
    // running status, and its delta times here all fit in two bytes.
    std::vector<std::pair<int, std::string>> signatures;
    std::size_t at = 14 + 8;
    int tick = 0;
    for (;;)
    {
        int delta = 0;
        for (;;)
        {
            const std::uint8_t b = file.at(at++);
            delta = delta * 128 + (b & 0x7f);
            if (b < 0x80)
                break;
        }
        tick += delta;
        REQUIRE(file.at(at++) == 0xff);
        const std::uint8_t type = file.at(at++);
        const std::size_t length = file.at(at++);
        if (type == 0x58)
            signatures.emplace_back(tick, std::to_string(file.at(at)) + "/" + std::to_string(1 << file.at(at + 1)));
        at += length;
        if (type == 0x2f)
            break;
    }
    // Two eighths of pickup, three full bars, then the three beats that complete it.
    CHECK(signatures == std::vector<std::pair<int, std::string>> { { 0, "1/4" }, { 960, "4/4" }, { 12480, "3/4" } });
}

TEST_CASE("the example scores' MIDI files are the ones TS writes", "[playback][midi]")
{
    for (const std::string name : { "melody", "band", "piano", "strings" })
    {
        requireExported(name + ".mid");
        compareFileWithExported(name);
    }
}

// Run with: msd_tests "[exported]"
TEST_CASE("the MIDI file of every exported score is the one TS writes", "[.][exported]")
{
    int compared = 0;
    if (std::filesystem::exists(exported))
        for (const auto& entry : std::filesystem::directory_iterator(exported))
        {
            const std::string file = entry.path().filename().string();
            // *.visual.mid are written by another script.
            if (!file.ends_with(".mid") || file.ends_with(".visual.mid"))
                continue;
            compareFileWithExported(file.substr(0, file.size() - 4));
            ++compared;
        }
    if (compared == 0)
        SKIP("no " << exported.string() << "/*.mid; run pnpm midi in prototype-web first");
}
