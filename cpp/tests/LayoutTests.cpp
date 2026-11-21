#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/json/Load.h"
#include "layout/SmuflFont.h"
#include "layout/StaffPosition.h"
#include "layout/SystemLayout.h"

using namespace msd;
using Catch::Approx;

namespace
{

const SmuflFont& leland()
{
    static const SmuflFont font = SmuflFont::load(std::filesystem::path(MSD_LELAND_DIR) / "leland_metadata.json");
    return font;
}

const LayoutProfile& defaultProfile()
{
    static const LayoutSetup setup = loadLayoutSetup(std::filesystem::path(MSD_RESOURCES_DIR) / "default.layout.json");
    return setup.layoutProfiles.at(0);
}

Pitch pitch(Step step, int octave) { return { .step = step, .alter = 0, .octave = octave }; }

const Clef treble { .id = "c", .type = ClefType::G, .line = 2, .octaveChange = 0 };
const Clef bass { .id = "c", .type = ClefType::F, .line = 4, .octaveChange = 0 };
const Clef alto { .id = "c", .type = ClefType::C, .line = 3, .octaveChange = 0 };
const Clef tenor { .id = "c", .type = ClefType::C, .line = 4, .octaveChange = 0 };

// Two measures of 4/4 in G major, treble clef:
//   m1: C4 quarter, E4 quarter, G4+B4 half
//   m2: quarter rest, A5 quarter, D5 half, final barline
std::string chord(const std::string& id, int type, const std::string& notes)
{
    return R"({ "id": ")" + id + R"(", "kind": "chord", "duration": { "type": )" + std::to_string(type)
           + R"(, "dots": 0 }, "articulations": [], "notes": [)" + notes + "] }";
}

std::string note(const std::string& id, const char* step, int octave)
{
    return R"({ "id": ")" + id + R"(", "kind": "note", "pitch": { "step": ")" + step + R"(", "alter": 0, "octave": )"
           + std::to_string(octave) + " } }";
}

const MusicData& twoMeasures()
{
    static const MusicData music = parseMusicData(R"({
      "title": "Two", "composer": "", "tpq": 480, "globalSpanners": [],
      "measures": [
        { "id": "m1", "kind": "measure", "tempos": [], "markers": [], "jumps": [],
          "timeSig": { "id": "ts", "kind": "time-signature", "beats": 4, "unit": 4 },
          "key": { "id": "key", "kind": "key-signature", "accidentals": 1 } },
        { "id": "m2", "kind": "measure", "tempos": [], "markers": [], "jumps": [], "barline": "final" }
      ],
      "tracks": [{ "id": "t1", "kind": "track", "instrumentName": "Flute", "instrumentAbbr": "Fl.",
        "trackSpanners": [], "attachments": [],
        "staves": [{ "id": "s1", "kind": "staff", "staffSpanners": [], "measures": [
          { "id": "sm1", "kind": "staff-measure", "measureId": "m1",
            "clefs": [{ "tick": 0, "clef": { "id": "clef", "kind": "clef", "type": "g", "line": 2, "octaveChange": 0 } }],
            "voices": [{ "id": "v1", "kind": "voice", "index": 0, "events": [)"
        + chord("c1", 0, note("n1", "c", 4)) + "," + chord("c2", 0, note("n2", "e", 4)) + ","
        + chord("c3", 1, note("n3", "g", 4) + "," + note("n4", "b", 4)) + R"(] }] },
          { "id": "sm2", "kind": "staff-measure", "measureId": "m2", "clefs": [],
            "voices": [{ "id": "v2", "kind": "voice", "index": 0, "events": [
              { "id": "r1", "kind": "rest", "duration": { "type": 0, "dots": 0 } },)"
        + chord("c5", 0, note("n5", "a", 5)) + "," + chord("c6", 1, note("n6", "d", 5)) + R"(] }] }
        ] }]
      }]
    })");
    return music;
}

SystemLayout layOut(std::size_t first, std::size_t count, std::optional<double> width = std::nullopt)
{
    return layoutSystem({ .music = twoMeasures(), .profile = defaultProfile(), .font = leland(),
                          .staffId = "s1", .firstMeasure = first, .measureCount = count, .width = width });
}

std::vector<const LayoutBox*> boxesOf(const SystemLayout& layout, Part part)
{
    std::vector<const LayoutBox*> found;
    for (const LayoutBox& box : layout.boxes)
        if (box.part == part)
            found.push_back(&box);
    return found;
}

const LayoutBox& boxFor(const SystemLayout& layout, const NodeId& id, Part part)
{
    for (const LayoutBox& box : layout.boxes)
        if (box.nodeId == id && box.part == part)
            return box;
    throw std::runtime_error("no box for " + id);
}

double originX(const LayoutBox& box) { return std::get<GlyphShape>(box.shape).origin.x; }
double originY(const LayoutBox& box) { return std::get<GlyphShape>(box.shape).origin.y; }

} // namespace

TEST_CASE("pitches sit on the right lines", "[layout][staff]")
{
    CHECK(pitchY(pitch(Step::G, 4), treble, 5) == 3);   // line 2
    CHECK(pitchY(pitch(Step::C, 4), treble, 5) == 5);   // first ledger line below
    CHECK(pitchY(pitch(Step::F, 5), treble, 5) == 0);   // top line
    CHECK(pitchY(pitch(Step::F, 3), bass, 5) == 1);     // line 4
    CHECK(pitchY(pitch(Step::C, 4), bass, 5) == -1);    // first ledger line above
    CHECK(pitchY(pitch(Step::C, 4), alto, 5) == 2);     // middle line
    CHECK(clefY(treble, 5) == 3);
    CHECK(clefY(bass, 5) == 1);
}

TEST_CASE("key signatures use the standard positions", "[layout][staff]")
{
    CHECK(keySignatureYs(3, treble, 5) == std::vector<double> { 0, 1.5, -0.5 });
    CHECK(keySignatureYs(-2, treble, 5) == std::vector<double> { 2, 0.5 });
    CHECK(keySignatureYs(-2, bass, 5) == std::vector<double> { 3, 1.5 });
    CHECK(keySignatureYs(1, alto, 5) == std::vector<double> { 0.5 });
    CHECK(keySignatureYs(2, tenor, 5) == std::vector<double> { 3, 1 });
    CHECK(keySignatureYs(0, treble, 5).empty());
}

TEST_CASE("Leland's metadata loads", "[layout][font]")
{
    const SmuflFont& font = leland();
    CHECK(font.name() == "Leland");
    CHECK(font.engravingDefaults().stemThickness == Approx(0.1));
    const Rect head = font.bbox("noteheadBlack").value();
    CHECK(head.right == Approx(1.3));
    CHECK(head.top == Approx(-0.528)); // flipped: up is negative
    const Point stemUp = font.anchor("noteheadBlack", "stemUpSE").value();
    CHECK(stemUp.x == Approx(1.3));
    CHECK(stemUp.y == Approx(-0.16));
    CHECK_FALSE(font.bbox("notAGlyph").has_value());
}

TEST_CASE("one system: what is drawn", "[layout]")
{
    const SystemLayout layout = layOut(0, 2);

    for (const LayoutBox& box : layout.boxes)
        CHECK_FALSE(box.nodeId.empty());

    CHECK(boxesOf(layout, Part::StaffLine).size() == 5);
    CHECK(boxesOf(layout, Part::Notehead).size() == 6);
    CHECK(boxesOf(layout, Part::Stem).size() == 5);
    CHECK(boxesOf(layout, Part::LedgerLine).size() == 2); // under C4, over A5
    CHECK(boxesOf(layout, Part::Rest).size() == 1);
    CHECK(boxesOf(layout, Part::Clef).size() == 1);
    CHECK(boxesOf(layout, Part::KeySignature).size() == 1);
    CHECK(boxesOf(layout, Part::TimeSignature).size() == 2);
    CHECK(boxesOf(layout, Part::Barline).size() == 3); // single, then thin + thick

    REQUIRE(layout.measures.size() == 2);
    CHECK(layout.measures[0].left == 0);
    CHECK(layout.measures[1].left == layout.measures[0].right);
    CHECK(layout.measures[1].right == Approx(layout.width));
}

TEST_CASE("one system: where things go", "[layout]")
{
    const SystemLayout layout = layOut(0, 2);
    const LayoutProfile& profile = defaultProfile();

    // Header: clef, key, time, in that order, the clef after its left margin.
    const LayoutBox& clef = boxFor(layout, "clef", Part::Clef);
    CHECK(originX(clef) == Approx(0.75));
    CHECK(originY(clef) == 3);
    const LayoutBox& sharp = boxFor(layout, "key", Part::KeySignature);
    CHECK(originY(sharp) == 0);
    CHECK(originX(sharp) > clef.bounds.right);
    CHECK(boxFor(layout, "ts", Part::TimeSignature).bounds.left > sharp.bounds.right);

    // Spacing: a quarter gets quarterSp, a half durationRatio times as much.
    const double c4 = originX(boxFor(layout, "n1", Part::Notehead));
    const double e4 = originX(boxFor(layout, "n2", Part::Notehead));
    const double g4 = originX(boxFor(layout, "n3", Part::Notehead));
    CHECK(e4 - c4 == Approx(profile.spacing.quarterSp));
    CHECK(g4 - e4 == Approx(profile.spacing.quarterSp));
    CHECK(layout.measures[0].right - g4 == Approx(profile.spacing.quarterSp * profile.spacing.durationRatio));
    CHECK(originX(boxFor(layout, "n4", Part::Notehead)) == g4); // same chord, same x

    // Second measure: padding after the barline, then the rest.
    CHECK(originX(boxFor(layout, "r1", Part::Rest)) == Approx(layout.measures[1].left + profile.spacing.measurePaddingSp));

    // C4 is below the middle: stem up, on the right, an octave long.
    const LayoutBox& lowStem = boxFor(layout, "c1", Part::Stem);
    CHECK(lowStem.bounds.left > c4 + 1);
    CHECK(lowStem.bounds.top == Approx(5 - 3.5));
    CHECK(boxFor(layout, "c1", Part::LedgerLine).bounds.top == Approx(5 - 0.08));

    // A5 is above the staff: stem down, on the left, reaching the middle line.
    const double a5 = originX(boxFor(layout, "n5", Part::Notehead));
    const LayoutBox& highStem = boxFor(layout, "c5", Part::Stem);
    CHECK(highStem.bounds.left == Approx(a5));
    CHECK(highStem.bounds.bottom == Approx(2.5));

    // The final barline's thick line ends at the system's right edge.
    CHECK(layout.boxes.back().bounds.right == Approx(layout.width));
}

TEST_CASE("a given width stretches only the space between notes", "[layout]")
{
    const SystemLayout natural = layOut(0, 2);
    const SystemLayout wide = layOut(0, 2, natural.width + 20);
    CHECK(wide.width == Approx(natural.width + 20));
    CHECK(wide.measures.back().right == Approx(wide.width));

    // The header doesn't move; the notes spread out evenly.
    CHECK(originX(boxFor(wide, "ts", Part::TimeSignature)) == Approx(originX(boxFor(natural, "ts", Part::TimeSignature))));
    const double naturalGap = originX(boxFor(natural, "n2", Part::Notehead)) - originX(boxFor(natural, "n1", Part::Notehead));
    const double wideGap = originX(boxFor(wide, "n2", Part::Notehead)) - originX(boxFor(wide, "n1", Part::Notehead));
    CHECK(wideGap > naturalGap);
}

TEST_CASE("a system that starts later repeats the clef and key, not the time", "[layout]")
{
    const SystemLayout layout = layOut(1, 1);
    CHECK(boxesOf(layout, Part::Clef).size() == 1);
    CHECK(boxesOf(layout, Part::KeySignature).size() == 1);
    CHECK(boxesOf(layout, Part::TimeSignature).empty());
}

TEST_CASE("bad requests throw", "[layout]")
{
    CHECK_THROWS_AS(layoutSystem({ .music = twoMeasures(), .profile = defaultProfile(), .font = leland(), .staffId = "nope" }),
                    std::invalid_argument);
    CHECK_THROWS_AS(layOut(1, 2), std::invalid_argument);
}

std::size_t countNotes(const std::vector<VoiceEvent>& events)
{
    std::size_t count = 0;
    for (const VoiceEvent& event : events)
    {
        if (const auto* chord = std::get_if<Chord>(&event))
            count += chord->notes.size();
        else if (const auto* tuplet = std::get_if<Tuplet>(&event))
            count += countNotes(tuplet->events);
    }
    return count;
}

// Lays out the first system (4 measures) of every staff of every exported score.
TEST_CASE("every exported score lays out", "[.][exported]")
{
    const std::filesystem::path folder = MSD_EXPORTED_SCORES_DIR;
    if (!std::filesystem::exists(folder))
        SKIP("no " << folder.string() << "; run pnpm midi in prototype-web first");

    for (const auto& entry : std::filesystem::directory_iterator(folder))
    {
        const std::string name = entry.path().filename().string();
        if (!name.ends_with(".music.json"))
            continue;
        INFO(name);
        const MusicData music = loadMusicData(entry.path());
        const std::size_t count = std::min<std::size_t>(4, music.measures.size());
        for (const Track& track : music.tracks)
            for (const Staff& staff : track.staves)
            {
                INFO(staff.id);
                SystemLayout layout;
                REQUIRE_NOTHROW(layout = layoutSystem({ .music = music, .profile = defaultProfile(), .font = leland(),
                                                        .staffId = staff.id, .measureCount = count, .width = 150.0 }));
                CHECK(layout.measures.size() == count);
                CHECK(layout.width == Approx(150.0));

                // Every note in those measures, tuplets and all voices included, has a notehead.
                std::size_t notes = 0;
                for (const StaffMeasure& staffMeasure : staff.measures)
                    for (std::size_t i = 0; i < count; ++i)
                        if (staffMeasure.measureId == music.measures[i].id)
                            for (const Voice& voice : staffMeasure.voices)
                                notes += countNotes(voice.events);
                CHECK(boxesOf(layout, Part::Notehead).size() == notes);

                for (const LayoutBox& box : layout.boxes)
                    CHECK((std::isfinite(box.bounds.left) && std::isfinite(box.bounds.top)
                           && std::isfinite(box.bounds.right) && std::isfinite(box.bounds.bottom)));
            }
    }
}
