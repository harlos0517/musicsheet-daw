// What each plugin of the rack plays, and what to send before playing from a point.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "core/json/Load.h"
#include "edit/NoteIndex.h"
#include "playback/Sequence.h"

using namespace msd;

namespace
{

const std::filesystem::path exported = MSD_EXPORTED_SCORES_DIR;

PluginSlot slot(const std::string& id, int channel, std::vector<std::string> names)
{
    return PluginSlot {
        .id = id, .name = id, .channel = channel, .instrumentNames = std::move(names), .outputIds = {}
    };
}

MidiMessage message(double timeMs, MidiMessageType type, int channel, int data1, int data2, MidiSource source)
{
    return { timeMs, type, channel, data1, data2, source };
}

} // namespace

TEST_CASE("a plugin plays its slots' outputs on their channels, moved by the track delay", "[playback][sequence]")
{
    if (!std::filesystem::exists(exported / "band.setup.json"))
        SKIP("run pnpm midi in prototype-web first");
    const MusicData music = loadMusicData(exported / "band.music.json");
    const PlaybackSetup setup = loadPlaybackSetup(exported / "band.setup.json");
    const Playback playback = play(music, setup);
    REQUIRE(playback.tracks.size() == 2);

    // The clarinet on channel 5 of one plugin; the drums have no slot.
    const PluginRack rack { .plugins = {
                                PluginInstance { .id = "k",
                                                 .name = "K",
                                                 .pluginFile = "k.vst3",
                                                 .slots = { slot("cl", 5, { "Clarinet in B\xe2\x99\xad" }) } } } };
    std::vector<std::string> warnings;
    const std::vector<PluginSequence> sequences = buildSequences(music, playback, setup, rack, warnings);
    REQUIRE(sequences.size() == 1);
    CHECK(sequences[0].pluginId == "k");
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0].find("no plugin slot") != std::string::npos);

    // Every clarinet message, its channel moved from 2 (the profile's) to 5 + 2 - 1, and
    // 60 ms earlier (the wind profile's trackDelayMs).
    const std::vector<MidiMessage>& rendered = playback.tracks[0].messages;
    const std::vector<MidiMessage>& played = sequences[0].messages;
    REQUIRE(played.size() == rendered.size());
    for (std::size_t i = 0; i < played.size(); ++i)
    {
        CHECK(played[i].channel == rendered[i].channel + 4);
        CHECK(played[i].timeMs == rendered[i].timeMs - 60);
        CHECK(played[i].data1 == rendered[i].data1);
    }
    CHECK(std::is_sorted(played.begin(), played.end(),
                         [](const MidiMessage& a, const MidiMessage& b) { return a.timeMs < b.timeMs; }));
}

TEST_CASE("playing from the middle sends the CCs and keyswitch in effect first", "[playback][sequence]")
{
    using enum MidiMessageType;
    const PluginSequence sequence { "k",
                                    {
                                        message(0, Cc, 1, 1, 40, MidiSource::Curve),
                                        message(0, NoteOn, 1, 24, 100, MidiSource::Keyswitch),
                                        message(10, NoteOff, 1, 24, 0, MidiSource::Keyswitch),
                                        message(20, NoteOn, 1, 60, 90, MidiSource::Note),
                                        message(500, Cc, 1, 1, 70, MidiSource::Curve),
                                        message(600, NoteOn, 1, 29, 100, MidiSource::Keyswitch),
                                        message(610, NoteOff, 1, 29, 0, MidiSource::Keyswitch),
                                        message(700, Cc, 2, 11, 5, MidiSource::TechniqueCc),
                                        message(800, NoteOn, 1, 45, 100, MidiSource::Keyswitch), // held: damp
                                        message(2000, NoteOff, 1, 45, 0, MidiSource::Keyswitch),
                                        message(3000, Cc, 1, 1, 127, MidiSource::Curve),
                                    } };
    const std::vector<MidiMessage> chase = chaseMessages(sequence, 1000);
    auto has = [&](MidiMessageType type, int channel, int data1, int data2) {
        return std::any_of(chase.begin(), chase.end(), [&](const MidiMessage& m) {
            return m.type == type && m.channel == channel && m.data1 == data1 && m.data2 == data2 && m.timeMs == 1000;
        });
    };
    CHECK(has(Cc, 1, 1, 70));
    CHECK_FALSE(has(Cc, 1, 1, 40));
    CHECK(has(Cc, 2, 11, 5));
    // The last keyswitch press is the held damp key: pressed, its release comes later.
    CHECK(has(NoteOn, 1, 45, 100));
    CHECK_FALSE(has(NoteOff, 1, 45, 0));
    CHECK_FALSE(has(NoteOn, 1, 29, 100)); // not the last press
    // No note is replayed.
    CHECK_FALSE(
        std::any_of(chase.begin(), chase.end(), [](const MidiMessage& m) { return m.source == MidiSource::Note; }));

    // Before the damp key: staccato (29), pressed and let go.
    const std::vector<MidiMessage> earlier = chaseMessages(sequence, 700);
    CHECK(std::count_if(earlier.begin(), earlier.end(), [](const MidiMessage& m) { return m.data1 == 29; }) == 2);
    CHECK(chaseMessages(sequence, 0).empty());
}

TEST_CASE("playing from a measure starts where it is first played", "[playback][sequence]")
{
    if (!std::filesystem::exists(exported / "band.setup.json"))
        SKIP("run pnpm midi in prototype-web first");
    const MusicData music = loadMusicData(exported / "band.music.json");
    const Playback playback = play(music, loadPlaybackSetup(exported / "band.setup.json"));
    REQUIRE(music.measures.size() > 2);
    CHECK(measureStartMs(playback, music.measures[0].id) == 0.0);
    const std::optional<double> second = measureStartMs(playback, music.measures[1].id);
    REQUIRE(second);
    CHECK(*second > 0);
    CHECK_FALSE(measureStartMs(playback, "no-such-measure"));

    // The measure of a note.
    const StaffMeasure& first = music.tracks[0].staves[0].measures[1];
    NodeId noteId;
    forEachNote(music, [&](const Note& note, int, const NodeId& measureId) {
        if (noteId.empty() && measureId == first.measureId)
            noteId = note.id;
    });
    REQUIRE_FALSE(noteId.empty());
    CHECK(measureOfNote(music, noteId) == first.measureId);
    CHECK(measureOfNote(music, "nothing").empty());
}
