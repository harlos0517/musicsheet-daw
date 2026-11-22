// Building MusicEvents from a score and its setup, checked against the *.events.json
// files `pnpm midi` writes.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "core/json/EnumNames.h"
#include "core/json/Load.h"
#include "playback/BuildEvents.h"
#include "playback/Curves.h"

using namespace msd;

namespace
{

const std::filesystem::path exported = MSD_EXPORTED_SCORES_DIR;

// Enough digits to tell any two doubles apart; -0 prints as 0, as in JSON.
std::string number(double value)
{
    char text[32];
    std::snprintf(text, sizeof text, "%.17g", value + 0.0);
    return text;
}

template <class T>
std::string optional(const std::optional<T>& value, auto&& format)
{
    return value ? format(*value) : std::string("-");
}

// Every field of an event on one line, so that a mismatch shows both events in full.
std::string describe(const CurveEvent& event)
{
    std::string applies = "-";
    if (event.appliesTo)
    {
        applies.clear();
        for (const CurveTarget& target : *event.appliesTo)
            applies +=
                target.staffId + ":" + optional(target.voiceIndex, [](int v) { return std::to_string(v); }) + " ";
    }
    std::string sources;
    for (const NodeId& id : event.sourceIds)
        sources += id + " ";
    return event.id + " | sources " + sources + "| track " + event.trackId + " | pass " + std::to_string(event.pass)
           + " | segment " + std::to_string(event.segmentIndex) + " | " + curveKey(event.parameter) + " | applies "
           + applies + "| combine " + optional(event.combine, [](CurveCombine c) { return std::string(enumName(c)); })
           + " | ticks " + number(event.startTick) + " +" + number(event.durationTicks) + " | "
           + number(event.fromValue) + " → " + number(event.toValue) + " | "
           + optional(event.curveType, [](CurveType t) { return std::string(enumName(t)); }) + " "
           + optional(event.steepness, number);
}

// Without the velocity, which is compared on its own (see velocityTolerance).
std::string describe(const NoteEvent& event)
{
    std::string sources;
    for (const NodeId& id : event.sourceIds)
        sources += id + " ";
    std::string techniques;
    for (const std::string& id : event.techniques)
        techniques += id + " ";
    const std::string pitch = std::visit(
        [](const auto& p) {
            if constexpr (std::is_same_v<std::decay_t<decltype(p)>, int>)
                return std::to_string(p);
            else
                return "kit:" + p.sound;
        },
        event.pitch);
    return event.id + " | sources " + sources + "| track " + event.trackId + " | staff " + event.staffId + ":"
           + std::to_string(event.voiceIndex) + " | pass " + std::to_string(event.pass) + " | pitch " + pitch
           + " | ticks " + number(event.startTick) + " +" + number(event.durationTicks) + " (notated "
           + number(event.notatedTicks) + ") | " + techniques + "| offset " + optional(event.startOffsetMs, number)
           + " | slur " + optional(event.slur, [](SlurPosition s) { return std::string(enumName(s)); });
}

// The names of the scores `pnpm midi` wrote events for.
std::vector<std::string> exportedEventLists()
{
    std::vector<std::string> names;
    if (!std::filesystem::exists(exported))
        return names;
    for (const auto& entry : std::filesystem::directory_iterator(exported))
    {
        const std::string file = entry.path().filename().string();
        const std::string suffix = ".events.json";
        if (file.ends_with(suffix))
            names.push_back(file.substr(0, file.size() - suffix.size()));
    }
    return names;
}

// Compares two lists line by line. Shows the first few differences in full and counts
// the rest, so a systematic error does not print thousands of lines.
void compareLines(const std::vector<std::string>& actual, const std::vector<std::string>& expected)
{
    CHECK(actual.size() == expected.size());
    int different = 0;
    for (std::size_t i = 0; i < std::min(actual.size(), expected.size()); ++i)
    {
        if (actual[i] == expected[i])
            continue;
        if (++different <= 5)
        {
            INFO("event " << i);
            CHECK(actual[i] == expected[i]);
        }
    }
    CHECK(different == 0);
}

// Velocities are read from the dynamics curve, often inside an exp or power segment, and
// V8's exp and pow round a few results differently from the C runtime's (by 1 ulp). Far
// below anything that reaches MIDI (0–127), so velocities may differ by this much.
constexpr double velocityTolerance = 1e-12;

void compareVelocities(const std::vector<double>& actual, const std::vector<double>& expected)
{
    REQUIRE(actual.size() == expected.size());
    double largest = 0;
    int inexact = 0;
    for (std::size_t i = 0; i < actual.size(); ++i)
    {
        const double difference = std::abs(actual[i] - expected[i]);
        largest = std::max(largest, difference);
        inexact += difference > 0 ? 1 : 0;
    }
    INFO(inexact << " of " << actual.size() << " velocities differ in the last digits, by up to " << largest);
    CHECK(largest <= velocityTolerance);
}

// Builds the events of a score `pnpm midi` exported and compares them with the events
// the TS made: everything exactly, velocities to within velocityTolerance.
void compareWithExported(const std::string& name)
{
    INFO(name);
    const MusicData music = loadMusicData(exported / (name + ".music.json"));
    const PlaybackSetup setup = loadPlaybackSetup(exported / (name + ".setup.json"));
    const std::vector<MusicEvent> expected = loadMusicEvents(exported / (name + ".events.json"));
    const EventResult result = buildMusicEvents(music, setup);

    std::vector<std::string> lines[2];
    std::vector<double> velocities[2];
    for (const auto* events : { &result.events, &expected })
    {
        const std::size_t side = events == &result.events ? 0 : 1;
        for (const MusicEvent& event : *events)
        {
            if (const auto* note = std::get_if<NoteEvent>(&event))
            {
                lines[side].push_back(describe(*note));
                velocities[side].push_back(note->velocity);
            }
            else
                lines[side].push_back(describe(std::get<CurveEvent>(event)));
        }
    }
    compareLines(lines[0], lines[1]);
    compareVelocities(velocities[0], velocities[1]);
}

void requireExported(const std::string& name)
{
    if (!std::filesystem::exists(exported / (name + ".events.json")))
        SKIP("no " << (exported / (name + ".events.json")).string() << "; run pnpm midi in prototype-web first");
}

} // namespace

TEST_CASE("the example scores play as in TS", "[playback][events]")
{
    for (const std::string name : { "melody", "band", "piano", "strings" })
    {
        requireExported(name);
        compareWithExported(name);
    }
}

TEST_CASE("a track without profiles is skipped with a warning", "[playback][events]")
{
    requireExported("melody");
    const MusicData music = loadMusicData(exported / "melody.music.json");
    PlaybackSetup setup = loadPlaybackSetup(exported / "melody.setup.json");
    setup.trackProfiles.clear();
    const EventResult result = buildMusicEvents(music, setup);
    CHECK(result.events.empty());
    REQUIRE(result.warnings.size() == music.tracks.size());
    CHECK(result.warnings[0] == "track " + music.tracks[0].id + ": no profiles assigned, skipped");
}

// The imported scores too, some with tens of thousands of notes. Run with:
// msd_tests "[exported]"
TEST_CASE("events match every exported score", "[.][exported]")
{
    const std::vector<std::string> names = exportedEventLists();
    if (names.empty())
        SKIP("no " << exported.string() << "/*.events.json; run pnpm midi in prototype-web first");
    for (const std::string& name : names)
        compareWithExported(name);
}
