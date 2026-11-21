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
    REQUIRE(setup.layoutProfiles.size() == 2); // default, large-score
    const LayoutProfile& profile = setup.layoutProfiles[0];
    CHECK(profile.id == "default");
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

TEST_CASE("a playback setup loads, each union by its tag", "[load][playback]")
{
    const PlaybackSetup setup = parsePlaybackSetup(R"({
      "techniques": [{ "id": "core:pizz", "group": "bowing" }],
      "interpretationProfiles": [{
        "id": "default", "name": "Default", "defaultPhrasing": "connected",
        "dynamics": [{ "type": "sfz", "mode": "add", "combine": "add", "value": 0.3,
          "then": { "afterQn": 0.5, "mode": "add", "value": 0, "curveType": "linear" } }],
        "noteRules": [{ "id": "short", "match": { "kind": "articulation", "type": "staccato" },
          "when": { "maxNotatedQn": 1, "hairpin": "dim" },
          "actions": [
            { "op": "scale", "for": "duration", "value": 0.5 },
            { "op": "scale", "for": "velocity", "value": 1.1 },
            { "op": "setRatio", "for": "duration", "value": 0.9 },
            { "op": "addTechnique", "techniqueId": "core:short" },
            { "op": "rollAttack", "together": false, "qn": 0.25 }
          ] }],
        "curveRules": [{ "id": "ped", "match": { "kind": "pedal", "type": "sostenuto" },
          "parameter": { "kind": "custom", "name": "brightness" },
          "points": [{ "time": 0, "value": 1 }] }]
      }],
      "instrumentProfiles": [{
        "id": "kit", "name": "Kit", "trackDelayMs": -60, "defaultChannel": 10,
        "velocity": { "mapping": { "low": 10, "high": 127, "curve": { "curveType": "ease", "steepness": 1 } } },
        "techniqueMappings": [{
          "matches": [{ "kind": "curve", "parameter": "dynamics", "min": 0, "max": 0.5 },
                      { "kind": "slur", "position": "inner" }],
          "cc": [{ "number": 1, "value": 64 },
                 { "number": 2, "value": { "shortMs": 100, "longMs": 400, "atShort": 0, "atLong": 127 } }],
          "durationMs": { "mode": "set", "value": 120 } }],
        "curveMappings": [{ "parameter": "sustain", "defaultValue": 0,
          "cc": [{ "number": 64, "mapping": { "low": 0, "high": 127 } }] }],
        "kit": { "snare": [38, 40], "kick": 36 }
      }],
      "trackProfiles": { "t1": { "interpretationProfileId": "default",
        "outputs": [{ "id": "main", "instrumentProfileId": "kit", "sounds": { "low-tom": "tom-4" } }] } },
      "overrides": [
        { "kind": "note-event-override", "id": "o1", "sourceId": "n1", "pitch": { "sound": "rim" },
          "techniques": { "add": ["core:accent"] } },
        { "kind": "curve-event-override", "id": "o2", "sourceId": "d1", "segmentIndex": 1,
          "toValue": { "mode": "scale", "value": 0.8 } }
      ]
    })");

    CHECK(setup.techniques.at(0).group == "bowing");

    const InterpretationProfile& interpretation = setup.interpretationProfiles.at(0);
    CHECK(interpretation.defaultPhrasing == Phrasing::Connected);
    CHECK(interpretation.dynamics->at(0).then->afterQn == 0.5);
    const NoteRule& rule = interpretation.noteRules.at(0);
    CHECK(std::get<ArticulationMatch>(rule.match).type == ArticulationType::Staccato);
    CHECK(rule.when->hairpin == HairpinType::Dim);
    REQUIRE(rule.actions.size() == 5);
    CHECK(std::get<DurationAction>(rule.actions[0]).op == DurationOp::Scale);
    CHECK(std::get<VelocityAction>(rule.actions[1]).op == ChangeMode::Scale);
    CHECK(std::get<DurationAction>(rule.actions[2]).op == DurationOp::SetRatio);
    CHECK(std::get<AddTechniqueAction>(rule.actions[3]).techniqueId == "core:short");
    CHECK(std::get<RollAttackAction>(rule.actions[4]).qn == 0.25);
    const CurveRule& curveRule = interpretation.curveRules.at(0);
    CHECK(std::get<PedalMatch>(curveRule.match).type == PedalType::Sostenuto);
    CHECK(curveRule.parameter == CurveParameter { CustomCurveParameter { "brightness" } });

    const InstrumentProfile& instrument = setup.instrumentProfiles.at(0);
    CHECK(std::get<MappedVelocity>(instrument.velocity).mapping.low == 10);
    const TechniqueMapping& mapping = instrument.techniqueMappings.at(0);
    CHECK(std::get<CurveRangeMatch>(mapping.matches.at(0)).max == 0.5);
    CHECK(std::get<SlurMatch>(mapping.matches.at(1)).position == SlurPosition::Inner);
    CHECK(std::get<int>(mapping.cc->at(0).value) == 64);
    CHECK(std::get<ByDuration>(mapping.cc->at(1).value).longMs == 400);
    CHECK(instrument.curveMappings.at(0).parameter == CurveParameter { BuiltInCurveParameter::Sustain });
    CHECK(std::get<std::vector<int>>(instrument.kit->at("snare")) == std::vector { 38, 40 });
    CHECK(std::get<int>(instrument.kit->at("kick")) == 36);

    CHECK(setup.trackProfiles.at("t1").outputs.at(0).sounds->at("low-tom") == "tom-4");
    CHECK(std::get<KitSound>(*std::get<NoteEventOverride>(setup.overrides.at(0)).pitch).sound == "rim");
    CHECK(std::get<CurveEventOverride>(setup.overrides.at(1)).toValue->mode == ChangeMode::Scale);
}

TEST_CASE("music events load", "[load][playback]")
{
    const std::vector<MusicEvent> events = parseMusicEvents(R"([
      { "kind": "note-event", "id": "n1@1", "sourceIds": ["n1"], "trackId": "t1", "staffId": "s1",
        "voiceIndex": 0, "pass": 1, "pitch": { "sound": "snare" }, "startTick": 0, "notatedTicks": 480,
        "durationTicks": 432.5, "velocity": 0.5, "techniques": ["core:accent"], "slur": "first" },
      { "kind": "curve-event", "id": "d1@1#0", "sourceIds": ["d1"], "trackId": "t1", "pass": 1,
        "segmentIndex": 0, "parameter": "dynamics", "combine": "scale", "startTick": 0,
        "durationTicks": 960, "fromValue": 0.5, "toValue": 0.7, "curveType": "exp", "steepness": 2 }
    ])");
    REQUIRE(events.size() == 2);
    const auto& note = std::get<NoteEvent>(events[0]);
    CHECK(std::get<KitSound>(note.pitch).sound == "snare");
    CHECK(note.durationTicks == 432.5);
    CHECK(note.slur == SlurPosition::First);
    const auto& curve = std::get<CurveEvent>(events[1]);
    CHECK(curve.combine == CurveCombine::Scale);
    CHECK(curve.curveType == CurveType::Exp);
}

TEST_CASE("playback load errors say where they are", "[load][playback]")
{
    const auto profileWith = [](std::string_view action) {
        return R"({ "techniques": [], "instrumentProfiles": [], "trackProfiles": {}, "overrides": [],
          "interpretationProfiles": [{ "id": "p", "name": "P", "curveRules": [],
            "noteRules": [{ "id": "r", "match": { "kind": "any" }, "actions": [)"
               + std::string(action) + "] }] }] }";
    };
    CHECK_THROWS_WITH(parsePlaybackSetup(profileWith(R"({ "op": "setQn", "for": "velocity", "value": 1 })")),
                      ContainsSubstring("noteRules[0].actions[0].op: unknown name \"setQn\""));
    CHECK_THROWS_WITH(parsePlaybackSetup(profileWith(R"({ "op": "scale", "value": 1 })")),
                      ContainsSubstring("actions[0]: missing string field \"for\""));
}

// Loads every setup and event list `pnpm midi` wrote. Run with: msd_tests "[exported]"
TEST_CASE("every exported setup and event list loads", "[.][exported]")
{
    const std::filesystem::path folder = MSD_EXPORTED_SCORES_DIR;
    if (!std::filesystem::exists(folder))
        SKIP("no " << folder.string() << "; run pnpm midi in prototype-web first");

    int setups = 0;
    int eventLists = 0;
    for (const auto& entry : std::filesystem::directory_iterator(folder))
    {
        const std::string name = entry.path().filename().string();
        INFO(name);
        if (name.ends_with(".setup.json"))
        {
            PlaybackSetup setup;
            REQUIRE_NOTHROW(setup = loadPlaybackSetup(entry.path()));
            CHECK_FALSE(setup.instrumentProfiles.empty());
            CHECK_FALSE(setup.trackProfiles.empty());
            ++setups;
        }
        else if (name.ends_with(".events.json"))
        {
            std::vector<MusicEvent> events;
            REQUIRE_NOTHROW(events = loadMusicEvents(entry.path()));
            CHECK_FALSE(events.empty());
            ++eventLists;
        }
    }
    CHECK(setups > 0);
    CHECK(eventLists > 0);
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
