// The first steps of playback: written time, the played order of the measures, and
// the tempo map. Ported from the TS tests in playback.test.ts, and checked against the
// *.timing.json files `pnpm midi` writes.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/json/Load.h"
#include "playback/Repeats.h"
#include "playback/ScoreIndex.h"
#include "playback/Tempo.h"
#include "playback/Timeline.h"

using namespace msd;
using Catch::Matchers::WithinAbs;

namespace
{

const std::filesystem::path exported = MSD_EXPORTED_SCORES_DIR;

// The example scores come from `pnpm midi`; without its output the tests are skipped.
MusicData exportedScore(const std::string& name)
{
    const std::filesystem::path file = exported / (name + ".music.json");
    if (!std::filesystem::exists(file))
        SKIP("no " << file.string() << "; run pnpm midi in prototype-web first");
    return loadMusicData(file);
}

struct Timing
{
    std::vector<MeasureInfo> timeline;
    std::vector<PlayedMeasure> played;
    TempoMap tempo;
};

Timing timingOf(const MusicData& music)
{
    std::vector<MeasureInfo> timeline = measureTimeline(music);
    std::vector<PlayedMeasure> played = expandRepeats(music, timeline);
    const ScoreIndex index = indexScore(music, timeline);
    TempoMap tempo = buildTempoMap(music, played, index, timeline);
    return { std::move(timeline), std::move(played), std::move(tempo) };
}

} // namespace

TEST_CASE("durations and tuplets in ticks", "[playback][timing]")
{
    CHECK(durationTicks({ DurationType::Quarter, 0 }, 480) == 480);
    CHECK(durationTicks({ DurationType::Half, 1 }, 480) == 1440);
    CHECK(durationTicks({ DurationType::Eighth, 2 }, 480) == 420);

    // Seven sixteenths in the time of four: fractional ticks, as in TS.
    std::vector<VoiceEvent> septuplet;
    for (int i = 0; i < 7; ++i)
        septuplet.push_back(Chord { .id = "c" + std::to_string(i), .duration = { DurationType::Sixteenth, 0 } });
    const std::vector<VoiceEvent> voice {
        Tuplet { .id = "t", .unit = { DurationType::Sixteenth, 0 }, .normal = 4, .actual = 7, .events = septuplet },
        Rest { .id = "r", .duration = { DurationType::Quarter, 0 } }
    };
    const std::vector<TimedEvent> timed = timeVoice(voice, 480);
    REQUIRE(timed.size() == 9);
    CHECK(timed[0].ticks == 480);
    CHECK(timed[2].tick == 120.0 * 4 / 7);
    CHECK(timed[2].tuplets.size() == 1);
    CHECK(timed[8].tick == 480); // the rest, after the tuplet
}

TEST_CASE("expandRepeats plays repeats, voltas and D.S. al Coda in order", "[playback][timing]")
{
    const MusicData music = exportedScore("band");
    const std::vector<MeasureInfo> timeline = measureTimeline(music);
    std::vector<std::string> order;
    for (const PlayedMeasure& m : expandRepeats(music, timeline))
        order.push_back(music.measures[m.measureIndex].id.substr(5) + ":" + std::to_string(m.pass));
    CHECK(
        order
        == std::vector<std::string> { "m1:1", "m2:1", "m1:2", "m3:1", "m4:1", "m5:1", "m6:1", "m4:2", "m5:2", "m7:1" });
}

TEST_CASE("expandRepeats leaves a score without repeats as written", "[playback][timing]")
{
    const MusicData music = exportedScore("melody");
    std::vector<double> starts;
    for (const PlayedMeasure& m : expandRepeats(music, measureTimeline(music)))
        starts.push_back(m.startTick);
    CHECK(starts == std::vector<double> { 0, 960, 4800, 8640, 12480 });
}

TEST_CASE("the tempo map converts ticks to time and back", "[playback][timing]")
{
    const Timing timing = timingOf(exportedScore("melody"));
    CHECK_THAT(timing.tempo.tickToMs(960), WithinAbs(625, 1e-9)); // quarter = 96
    CHECK_THAT(timing.tempo.msToTick(625), WithinAbs(960, 1e-9));
}

TEST_CASE("the tempo slows down through a rit.", "[playback][timing]")
{
    const Timing timing = timingOf(exportedScore("band"));
    const double coda = timing.played.back().startTick;
    const auto at = [&](int n) { return timing.tempo.tickToMs(coda + n * 960); };
    const auto beat = [&](int n) { return at(n + 1) - at(n); };
    CHECK_THAT(timing.tempo.tickToMs(coda), WithinAbs(9 * 2000, 1e-6)); // nine bars at 120
    CHECK(beat(0) > 500);
    CHECK(beat(3) > beat(0));
    CHECK(timing.tempo.points().back().qpm == 90);
    CHECK_THAT(timing.tempo.msToTick(timing.tempo.tickToMs(coda + 1234)), WithinAbs(coda + 1234, 1e-6));
}

TEST_CASE("a jump uses the tempo written at the place it jumps to", "[playback][timing]")
{
    MusicData music = exportedScore("band");
    music.measures[5].tempos.push_back(
        Tempo { .id = "x", .unit = { DurationType::Quarter, 0 }, .bpm = 60 }); // bar 6, just before the D.S.
    const Timing timing = timingOf(music);
    const auto bar = [](int n) { return (n - 1) * 3840.0; };
    const auto barMs = [&](int n) { return timing.tempo.tickToMs(bar(n + 1)) - timing.tempo.tickToMs(bar(n)); };
    CHECK_THAT(barMs(6), WithinAbs(2000, 1e-6)); // bar 5, at 120
    CHECK_THAT(barMs(7), WithinAbs(4000, 1e-6)); // bar 6, at 60
    CHECK_THAT(barMs(8), WithinAbs(2000, 1e-6)); // bar 4 again: 120, as written there
}

// Compares the played order and the tempo points with what the TS wrote, exactly: the
// same arithmetic in the same order gives the same doubles. Run with: msd_tests "[exported]"
TEST_CASE("played order and tempo match every exported score", "[.][exported]")
{
    if (!std::filesystem::exists(exported))
        SKIP("no " << exported.string() << "; run pnpm midi in prototype-web first");

    int compared = 0;
    for (const auto& entry : std::filesystem::directory_iterator(exported))
    {
        const std::string file = entry.path().filename().string();
        if (!file.ends_with(".timing.json"))
            continue;
        const std::string name = file.substr(0, file.size() - std::string(".timing.json").size());
        INFO(name);
        std::ifstream in(entry.path());
        const nlohmann::json expected = nlohmann::json::parse(in);
        const MusicData music = loadMusicData(exported / (name + ".music.json"));
        const Timing timing = timingOf(music);

        const nlohmann::json& played = expected.at("played");
        REQUIRE(timing.played.size() == played.size());
        for (std::size_t i = 0; i < played.size(); ++i)
        {
            INFO("played[" << i << "]");
            CHECK(timing.played[i].measureIndex == played[i].at("measureIndex").get<int>());
            CHECK(timing.played[i].pass == played[i].at("pass").get<int>());
            CHECK(timing.played[i].startTick == played[i].at("startTick").get<double>());
        }

        const nlohmann::json& tempo = expected.at("tempo");
        REQUIRE(timing.tempo.points().size() == tempo.size());
        for (std::size_t i = 0; i < tempo.size(); ++i)
        {
            INFO("tempo[" << i << "]");
            CHECK(timing.tempo.points()[i].tick == tempo[i].at("tick").get<double>());
            CHECK(timing.tempo.points()[i].qpm == tempo[i].at("qpm").get<double>());
        }
        ++compared;
    }
    CHECK(compared > 0);
}
