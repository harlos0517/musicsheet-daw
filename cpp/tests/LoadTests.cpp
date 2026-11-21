#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <string>
#include <variant>

#include "core/json/Load.h"

using namespace msd;
using Catch::Matchers::ContainsSubstring;

namespace
{

// One measure of 3/4 on one staff: a triplet of eighths (the middle one tied to
// the next chord), a C–E chord with a staccato, and a quarter rest.
constexpr const char* smallScore = R"({
  "title": "Small", "composer": "Test", "tpq": 480,
  "measures": [{
    "id": "m1", "kind": "measure", "tempos": [], "markers": [], "jumps": [],
    "timeSig": { "id": "ts1", "kind": "time-signature", "beats": 3, "unit": 4 },
    "key": { "id": "k1", "kind": "key-signature", "accidentals": -1 }
  }],
  "tracks": [{
    "id": "t1", "kind": "track", "instrumentName": "Flute", "instrumentAbbr": "Fl.",
    "trackSpanners": [
      { "id": "sl1", "kind": "slur",
        "start": { "type": "element", "id": "c1" }, "end": { "type": "element", "id": "c4" } }
    ],
    "attachments": [
      { "id": "d1", "kind": "dynamic", "type": "mf", "at": { "type": "tick", "measureId": "m1", "tick": 0 } }
    ],
    "staves": [{
      "id": "s1", "kind": "staff", "staffSpanners": [],
      "measures": [{
        "id": "sm1", "kind": "staff-measure", "measureId": "m1",
        "clefs": [{ "tick": 0, "clef": { "id": "cl1", "kind": "clef", "type": "g", "line": 2, "octaveChange": 0 } }],
        "voices": [{
          "id": "v1", "kind": "voice", "index": 0,
          "events": [
            { "id": "tu1", "kind": "tuplet", "unit": { "type": -1, "dots": 0 }, "normal": 2, "actual": 3,
              "events": [
                { "id": "c1", "kind": "chord", "duration": { "type": -1, "dots": 0 }, "articulations": [],
                  "notes": [{ "id": "n1", "kind": "note", "pitch": { "step": "g", "alter": 0, "octave": 4 } }] },
                { "id": "c2", "kind": "chord", "duration": { "type": -1, "dots": 0 }, "articulations": [],
                  "notes": [{ "id": "n2", "kind": "note", "pitch": { "step": "a", "alter": 0, "octave": 4 },
                              "tie": [{ "to": "n3" }] }] },
                { "id": "c3", "kind": "chord", "duration": { "type": -1, "dots": 0 }, "articulations": [],
                  "notes": [{ "id": "n3", "kind": "note", "pitch": { "step": "a", "alter": 0, "octave": 4 } }] }
              ] },
            { "id": "c4", "kind": "chord", "duration": { "type": 0, "dots": 0 },
              "articulations": [{ "id": "a1", "kind": "articulation", "type": "staccato" }],
              "notes": [
                { "id": "n4", "kind": "note", "pitch": { "step": "c", "alter": 0, "octave": 5 } },
                { "id": "n5", "kind": "note", "pitch": { "step": "e", "alter": -1, "octave": 5 },
                  "accidentalDisplay": "cautionary" }
              ] },
            { "id": "r1", "kind": "rest", "duration": { "type": 0, "dots": 0 } }
          ]
        }]
      }]
    }]
  }],
  "globalSpanners": []
})";

} // namespace

TEST_CASE("a small score loads into the C++ types", "[load]")
{
    const MusicData music = parseMusicData(smallScore);

    CHECK(music.title == "Small");
    CHECK(music.tpq == 480);

    REQUIRE(music.measures.size() == 1);
    const Measure& measure = music.measures[0];
    REQUIRE(measure.timeSig.has_value());
    CHECK(measure.timeSig->beats == 3);
    CHECK(measure.key->accidentals == -1);
    CHECK_FALSE(measure.repeatStart.has_value()); // absent stays absent

    const Track& track = music.tracks.at(0);
    CHECK(std::holds_alternative<Slur>(track.trackSpanners.at(0)));
    const auto& dynamic = std::get<Dynamic>(track.attachments.at(0));
    CHECK(dynamic.type == DynamicType::Mf);
    CHECK(std::get<TickAnchor>(dynamic.at).measureId == "m1");

    const Staff& staff = track.staves.at(0);
    CHECK_FALSE(staff.lines.has_value());
    const StaffMeasure& staffMeasure = staff.measures.at(0);
    CHECK(staffMeasure.clefs.at(0).clef.type == ClefType::G);

    const auto& events = staffMeasure.voices.at(0).events;
    REQUIRE(events.size() == 3);

    const auto& tuplet = std::get<Tuplet>(events[0]);
    CHECK(tuplet.actual == 3);
    CHECK(tuplet.unit.type == DurationType::Eighth);
    REQUIRE(tuplet.events.size() == 3);
    const auto& tiedNote = std::get<Note>(std::get<Chord>(tuplet.events[1]).notes.at(0));
    REQUIRE(tiedNote.tie.has_value());
    CHECK(std::get<std::vector<TieTarget>>(*tiedNote.tie).at(0).to == "n3");

    const auto& chord = std::get<Chord>(events[1]);
    CHECK(chord.articulations.at(0).type == ArticulationType::Staccato);
    const auto& top = std::get<Note>(chord.notes.at(1));
    CHECK(top.pitch.step == Step::E);
    CHECK(top.pitch.alter == -1);
    CHECK(top.accidentalDisplay == AccidentalDisplay::Cautionary);

    CHECK(std::holds_alternative<Rest>(events[2]));
}

TEST_CASE("load errors say where they are", "[load]")
{
    SECTION("unknown kind")
    {
        std::string json = smallScore;
        json.replace(json.find(R"("kind": "rest")"), 14, R"("kind": "glissando")");
        CHECK_THROWS_WITH(parseMusicData(json), ContainsSubstring("tracks[0].staves[0].measures[0].voices[0].events[2]")
                                                    && ContainsSubstring("unknown kind \"glissando\""));
    }

    SECTION("unknown enum name")
    {
        std::string json = smallScore;
        json.replace(json.find(R"("step": "g")"), 11, R"("step": "h")");
        CHECK_THROWS_WITH(parseMusicData(json),
                          ContainsSubstring("events[0].events[0].notes[0].pitch.step: unknown name \"h\""));
    }

    SECTION("missing field")
    {
        std::string json = smallScore;
        json.replace(json.find(R"("tpq": 480,)"), 11, "");
        CHECK_THROWS_WITH(parseMusicData(json), ContainsSubstring("(root): missing field \"tpq\""));
    }

    SECTION("a fractional tick")
    {
        std::string json = smallScore;
        json.replace(json.find(R"("tick": 0 })"), 9, R"("tick": 0.5)"); // the dynamic's anchor
        CHECK_THROWS_WITH(parseMusicData(json),
                          ContainsSubstring("attachments[0].at.tick: expected an integer, got 0.5"));
    }

    SECTION("not JSON") { CHECK_THROWS_WITH(parseMusicData("{ nope"), ContainsSubstring("invalid JSON")); }
}

TEST_CASE("the default layout setup loads", "[load][layout]")
{
    const LayoutSetup setup = loadLayoutSetup(std::filesystem::path(MSD_RESOURCES_DIR) / "default.layout.json");
    REQUIRE(setup.layoutProfiles.size() == 1);
    const LayoutProfile& profile = setup.layoutProfiles[0];
    CHECK(profile.fonts.music == "Leland");
    CHECK(profile.spacing.durationRatio == 1.4);
    CHECK(profile.placement.at(PlacedKind::Dynamic) == Side::Below);
    CHECK(setup.layouts.at(0).layoutProfileId == "default");
    CHECK_FALSE(setup.layouts.at(0).flow.has_value());
}

TEST_CASE("layout elements load by kind", "[load][layout]")
{
    const LayoutSetup setup = parseLayoutSetup(R"({
      "layoutProfiles": [],
      "layouts": [{ "id": "score", "name": "Score", "layoutProfileId": "default",
        "elements": [
          { "id": "e1", "kind": "break", "nodeId": "m4", "type": "page" },
          { "id": "e2", "kind": "offset", "nodeId": "n1", "x": 0.5, "y": -1, "part": "accidental" }
        ],
        "flow": { "pages": [{ "systems": [{ "startMeasureId": "m1", "ySp": 0, "staffYSp": { "s1": 0, "s2": 12.5 } }] }] }
      }]
    })");
    const Layout& layout = setup.layouts.at(0);
    CHECK(std::get<BreakElement>(layout.elements.at(0)).type == BreakType::Page);
    CHECK(std::get<OffsetElement>(layout.elements.at(1)).part == "accidental");
    CHECK(layout.flow->pages.at(0).systems.at(0).staffYSp.at("s2") == 12.5);
}

// Loads every score `pnpm midi` wrote. Hidden by default ([.]) because the big
// scores take a while in a Debug build. Run with: msd_tests "[exported]"
TEST_CASE("every exported score loads", "[.][exported]")
{
    const std::filesystem::path folder = MSD_EXPORTED_SCORES_DIR;
    if (!std::filesystem::exists(folder))
        SKIP("no " << folder.string() << "; run pnpm midi in prototype-web first");

    int loaded = 0;
    for (const auto& entry : std::filesystem::directory_iterator(folder))
    {
        const std::string name = entry.path().filename().string();
        if (!name.ends_with(".music.json"))
            continue;
        INFO(name);
        MusicData music;
        REQUIRE_NOTHROW(music = loadMusicData(entry.path()));
        CHECK_FALSE(music.measures.empty());
        CHECK_FALSE(music.tracks.empty());
        ++loaded;
    }
    CHECK(loaded > 0);
}
