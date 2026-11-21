// Accidentals, dots, flags and seconds in chords.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <limits>
#include <set>
#include <string>
#include <vector>

#include "core/json/Load.h"
#include "layout/Accidentals.h"
#include "layout/SmuflFont.h"
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

Pitch pitch(Step step, int alter, int octave) { return { .step = step, .alter = alter, .octave = octave }; }

// ── Small scores as JSON ───────────────────────────────────────────────────────

std::string note(const std::string& id, const char* step, int alter, int octave, const std::string& extra = "")
{
    return R"({ "id": ")" + id + R"(", "kind": "note", "pitch": { "step": ")" + step + R"(", "alter": )"
           + std::to_string(alter) + R"(, "octave": )" + std::to_string(octave) + " }" + extra + " }";
}

std::string chord(const std::string& id, int type, int dots, const std::string& notes)
{
    return R"({ "id": ")" + id + R"(", "kind": "chord", "duration": { "type": )" + std::to_string(type)
           + R"(, "dots": )" + std::to_string(dots) + R"( }, "articulations": [], "notes": [)" + notes + "] }";
}

std::string rest(const std::string& id, int type, int dots)
{
    return R"({ "id": ")" + id + R"(", "kind": "rest", "duration": { "type": )" + std::to_string(type) + R"(, "dots": )"
           + std::to_string(dots) + " } }";
}

// One treble staff in 4/4 with the given key; one string of comma-separated events per measure.
MusicData score(int keyAccidentals, const std::vector<std::string>& measures)
{
    std::string ms;
    std::string sms;
    for (std::size_t i = 0; i < measures.size(); ++i)
    {
        const std::string n = std::to_string(i + 1);
        if (i > 0)
        {
            ms += ",";
            sms += ",";
        }
        ms += R"({ "id": "m)" + n + R"(", "kind": "measure", "tempos": [], "markers": [], "jumps": [])";
        if (i == 0)
            ms += R"(, "timeSig": { "id": "ts", "kind": "time-signature", "beats": 4, "unit": 4 },
                      "key": { "id": "key", "kind": "key-signature", "accidentals": )"
                  + std::to_string(keyAccidentals) + " }";
        ms += " }";
        sms +=
            R"({ "id": "sm)" + n + R"(", "kind": "staff-measure", "measureId": "m)" + n + R"(", "clefs": [)"
            + (i == 0
                   ? R"({ "tick": 0, "clef": { "id": "clef", "kind": "clef", "type": "g", "line": 2, "octaveChange": 0 } })"
                   : "")
            + R"(], "voices": [{ "id": "v)" + n + R"(", "kind": "voice", "index": 0, "events": [)" + measures[i]
            + "] }] }";
    }
    return parseMusicData(
        R"({ "title": "t", "composer": "", "tpq": 480, "globalSpanners": [], "measures": [)" + ms
        + R"(], "tracks": [{ "id": "t1", "kind": "track", "instrumentName": "Flute", "instrumentAbbr": "Fl.",
                               "trackSpanners": [], "attachments": [], "staves": [{ "id": "s1", "kind": "staff",
                               "staffSpanners": [], "measures": [)"
        + sms + "] }] }] }");
}

SystemLayout layOut(const MusicData& music)
{
    return layoutSystem({ .music = music,
                          .profile = defaultProfile(),
                          .font = leland(),
                          .staffId = "s1",
                          .measureCount = music.measures.size() });
}

std::vector<const LayoutBox*> boxes(const SystemLayout& layout, const NodeId& id, Part part)
{
    std::vector<const LayoutBox*> found;
    for (const LayoutBox& box : layout.boxes)
        if (box.nodeId == id && box.part == part)
            found.push_back(&box);
    return found;
}

const LayoutBox& one(const SystemLayout& layout, const NodeId& id, Part part)
{
    const auto found = boxes(layout, id, part);
    REQUIRE(found.size() == 1);
    return *found[0];
}

const std::string& glyphOf(const LayoutBox& box) { return std::get<GlyphShape>(box.shape).glyph; }
Point originOf(const LayoutBox& box) { return std::get<GlyphShape>(box.shape).origin; }

// The horizontal range covered by every box of the given nodes.
std::pair<double, double> extent(const SystemLayout& layout, const std::set<NodeId>& ids)
{
    double left = std::numeric_limits<double>::max();
    double right = std::numeric_limits<double>::lowest();
    for (const LayoutBox& box : layout.boxes)
        if (ids.contains(box.nodeId))
        {
            left = std::min(left, box.bounds.left);
            right = std::max(right, box.bounds.right);
        }
    return { left, right };
}

} // namespace

// ── The accidental rules, without layout ───────────────────────────────────────

TEST_CASE("key signatures alter their steps", "[accidentals]")
{
    CHECK(keyAlter(Step::F, 1) == 1);  // G major
    CHECK(keyAlter(Step::C, 1) == 0);
    CHECK(keyAlter(Step::B, -1) == -1); // F major
    CHECK(keyAlter(Step::E, -2) == -1); // B♭ major
    CHECK(keyAlter(Step::A, -2) == 0);
    CHECK(keyAlter(Step::G, 0) == 0);
    CHECK(accidentalGlyph(0) == "accidentalNatural");
    CHECK(accidentalGlyph(-2) == "accidentalDoubleFlat");
}

TEST_CASE("an accidental lasts for its step and octave until the barline", "[accidentals]")
{
    MeasureAccidentals g(1); // G major: F is sharp
    CHECK_FALSE(g.next(pitch(Step::F, 1, 4), {}, false).has_value()); // F♯ is in the key
    CHECK(g.next(pitch(Step::F, 0, 4), {}, false)->alter == 0);        // F♮ needs a natural
    CHECK_FALSE(g.next(pitch(Step::F, 0, 4), {}, false).has_value()); // and then not again
    CHECK(g.next(pitch(Step::F, 0, 5), {}, false)->alter == 0);        // another octave needs its own
    CHECK(g.next(pitch(Step::F, 1, 4), {}, false)->alter == 1);        // back to F♯: restated
}

TEST_CASE("a tied-over note shows nothing and changes nothing", "[accidentals]")
{
    MeasureAccidentals c(0);
    CHECK_FALSE(c.next(pitch(Step::C, 1, 4), {}, true).has_value());   // continues a tie from before
    CHECK(c.next(pitch(Step::C, 1, 4), {}, false)->alter == 1);        // the next C♯ still needs its sharp
}

TEST_CASE("accidentalDisplay overrides the rules", "[accidentals]")
{
    MeasureAccidentals g(1);
    const auto shown = g.next(pitch(Step::F, 1, 4), AccidentalDisplay::Show, false);
    REQUIRE(shown.has_value());
    CHECK_FALSE(shown->parentheses);
    CHECK_FALSE(g.next(pitch(Step::C, 1, 4), AccidentalDisplay::Hide, false).has_value());
    CHECK_FALSE(g.next(pitch(Step::C, 1, 4), {}, false).has_value()); // hidden, but still in force
    const auto cautionary = g.next(pitch(Step::F, 1, 4), AccidentalDisplay::Cautionary, false);
    REQUIRE(cautionary.has_value());
    CHECK(cautionary->parentheses);
}

// ── In the layout ──────────────────────────────────────────────────────────────

TEST_CASE("accidentals are drawn where the rules ask", "[layout][notes]")
{
    // G major: F♮ F♮ F♯ F♮, then a C♯ tied into the next measure, then C♯ again.
    const MusicData music =
        score(1, {
                     chord("c1", 0, 0, note("n1", "f", 0, 4)) + "," + chord("c2", 0, 0, note("n2", "f", 0, 4)) + ","
                         + chord("c3", 0, 0, note("n3", "f", 1, 4)) + "," + chord("c4", 0, 0, note("n4", "f", 0, 4)),
                     chord("c5", 1, 0, note("n5", "c", 1, 5, R"(, "tie": [{ "to": "n6" }])")),
                     chord("c6", 0, 0, note("n6", "c", 1, 5)) + "," + chord("c7", 0, 0, note("n7", "c", 1, 5)),
                 });
    const SystemLayout layout = layOut(music);

    CHECK(glyphOf(one(layout, "n1", Part::Accidental)) == "accidentalNatural");
    CHECK(boxes(layout, "n2", Part::Accidental).empty());
    CHECK(glyphOf(one(layout, "n3", Part::Accidental)) == "accidentalSharp");
    CHECK(glyphOf(one(layout, "n4", Part::Accidental)) == "accidentalNatural");
    CHECK(glyphOf(one(layout, "n5", Part::Accidental)) == "accidentalSharp");
    CHECK(boxes(layout, "n6", Part::Accidental).empty()); // tied over the barline
    CHECK(glyphOf(one(layout, "n7", Part::Accidental)) == "accidentalSharp");

    // Left of its notehead, at the notehead's height.
    const LayoutBox& sharp = one(layout, "n3", Part::Accidental);
    const LayoutBox& head = one(layout, "n3", Part::Notehead);
    CHECK(sharp.bounds.right == Approx(head.bounds.left - NoteSpacing {}.accidentalGapSp));
    CHECK(originOf(sharp).y == originOf(head).y);
}

TEST_CASE("cautionary accidentals get parentheses", "[layout][notes]")
{
    const MusicData music =
        score(0, { chord("c1", 0, 0, note("n1", "b", -1, 4, R"(, "accidentalDisplay": "cautionary")")) });
    const SystemLayout layout = layOut(music); // keep it: `pieces` points into it
    const auto pieces = boxes(layout, "n1", Part::Accidental);
    REQUIRE(pieces.size() == 3);
    CHECK(glyphOf(*pieces[0]) == "accidentalParensLeft");
    CHECK(glyphOf(*pieces[1]) == "accidentalFlat");
    CHECK(glyphOf(*pieces[2]) == "accidentalParensRight");
    CHECK(pieces[0]->bounds.right <= pieces[1]->bounds.left + 1e-9);
}

TEST_CASE("accidentals in a chord stack in columns only when they would clash", "[layout][notes]")
{
    // F♯4 + A♯4, a third apart: two columns. In the next measure (so both sharps are
    // needed again), F♯4 + F♯5, an octave apart: one.
    const MusicData music = score(0, {
                                         chord("c1", 0, 0, note("n1", "f", 1, 4) + "," + note("n2", "a", 1, 4)),
                                         chord("c2", 0, 0, note("n3", "f", 1, 4) + "," + note("n4", "f", 1, 5)),
                                     });
    const SystemLayout layout = layOut(music);
    const LayoutBox& lowThird = one(layout, "n1", Part::Accidental);
    const LayoutBox& highThird = one(layout, "n2", Part::Accidental);
    CHECK(highThird.bounds.right
          == Approx(one(layout, "n2", Part::Notehead).bounds.left - 0.2)); // the top one is nearest
    CHECK(lowThird.bounds.right < highThird.bounds.left); // the other steps left
    CHECK(originOf(one(layout, "n3", Part::Accidental)).x == Approx(originOf(one(layout, "n4", Part::Accidental)).x));
}

TEST_CASE("dots go in the space of their note", "[layout][notes]")
{
    // G4 on a line, dotted half; A4 in a space, double-dotted quarter; a dotted quarter rest.
    const MusicData music = score(0, {
                                         chord("c1", 1, 1, note("n1", "g", 0, 4)) + ","
                                             + chord("c2", 0, 2, note("n2", "a", 0, 4)) + "," + rest("r1", 0, 1),
                                     });
    const SystemLayout layout = layOut(music);

    const LayoutBox& dot = one(layout, "n1", Part::Dot);
    CHECK(originOf(dot).y == Approx(2.5)); // G4 is on y = 3: the space above
    CHECK(dot.bounds.left == Approx(one(layout, "n1", Part::Notehead).bounds.right + NoteSpacing {}.dotGapSp));

    const auto dots = boxes(layout, "n2", Part::Dot);
    REQUIRE(dots.size() == 2);
    CHECK(originOf(*dots[0]).y == Approx(2.5)); // A4 is in a space: same y
    CHECK(dots[1]->bounds.left > dots[0]->bounds.right);

    CHECK(boxes(layout, "r1", Part::Dot).size() == 1);
}

TEST_CASE("short notes get flags, and the font decides the stem length", "[layout][notes]")
{
    // G4 eighth (stem up), D5 sixteenth (stem down), G4 32nd (stem up).
    const MusicData music =
        score(0, {
                     chord("c1", -1, 0, note("n1", "g", 0, 4)) + "," + chord("c2", -2, 0, note("n2", "d", 0, 5)) + ","
                         + chord("c3", -3, 0, note("n3", "g", 0, 4)),
                 });
    const SystemLayout layout = layOut(music);

    CHECK(glyphOf(one(layout, "c1", Part::Flag)) == "flag8thUp");
    CHECK(glyphOf(one(layout, "c2", Part::Flag)) == "flag16thDown");
    CHECK(glyphOf(one(layout, "c3", Part::Flag)) == "flag32ndUp");

    // The flag hangs from the top of the stem, against its left edge.
    const LayoutBox& stem = one(layout, "c1", Part::Stem);
    const LayoutBox& flag = one(layout, "c1", Part::Flag);
    CHECK(originOf(flag).x == Approx(stem.bounds.left));
    CHECK(originOf(flag).y == Approx(3 - 3.5));

    // Leland's 32nd flag asks for a longer stem than the eighth's.
    const double eighthTop = stem.bounds.top;
    const double thirtySecondTop = one(layout, "c3", Part::Stem).bounds.top;
    CHECK(thirtySecondTop < eighthTop - 0.5);
}

TEST_CASE("notes a second apart sit on both sides of the stem", "[layout][notes]")
{
    // G4 + A4 (stem up): A4 moves right. C5 + D5 (stem down): C5 moves left.
    const MusicData music = score(0, {
                                         chord("c1", 0, 0, note("n1", "g", 0, 4) + "," + note("n2", "a", 0, 4)) + ","
                                             + chord("c2", 0, 0, note("n3", "c", 0, 5) + "," + note("n4", "d", 0, 5)),
                                     });
    const SystemLayout layout = layOut(music);
    const double width = leland().bbox("noteheadBlack")->right;
    const double stem = leland().engravingDefaults().stemThickness;

    CHECK(originOf(one(layout, "n2", Part::Notehead)).x
          == Approx(originOf(one(layout, "n1", Part::Notehead)).x + width - stem));
    CHECK(originOf(one(layout, "n3", Part::Notehead)).x
          == Approx(originOf(one(layout, "n4", Part::Notehead)).x - width + stem));
}

TEST_CASE("crowded notes are spaced so that nothing touches", "[layout][notes]")
{
    // Sixteenths with flags and accidentals ask for more room than their duration gives.
    const MusicData music =
        score(0, {
                     chord("c1", -2, 0, note("n1", "f", 1, 4)) + "," + chord("c2", -2, 0, note("n2", "g", 1, 4)) + ","
                         + chord("c3", -2, 0, note("n3", "a", 1, 4)) + "," + chord("c4", -2, 0, note("n4", "b", 1, 4)),
                 });
    const SystemLayout layout = layOut(music);
    const double padding = defaultProfile().padding.collisionSp;
    for (int i = 1; i < 4; ++i)
    {
        const std::string a = std::to_string(i);
        const std::string b = std::to_string(i + 1);
        const auto [leftA, rightA] = extent(layout, { "c" + a, "n" + a });
        const auto [leftB, rightB] = extent(layout, { "c" + b, "n" + b });
        INFO("between chords " << a << " and " << b);
        CHECK(leftB - rightA >= padding - 1e-9);
    }
}

TEST_CASE("squeezing stops where symbols would touch", "[layout][notes]")
{
    const MusicData music =
        score(0, {
                     chord("c1", -2, 0, note("n1", "f", 1, 4)) + "," + chord("c2", -2, 0, note("n2", "g", 1, 4)) + ","
                         + chord("c3", 0, 0, note("n3", "a", 1, 4)) + "," + chord("c4", 1, 0, note("n4", "b", 1, 4)),
                 });
    auto layOutAt = [&](std::optional<double> width) {
        return layoutSystem(
            { .music = music, .profile = defaultProfile(), .font = leland(), .staffId = "s1", .width = width });
    };
    const double padding = defaultProfile().padding.collisionSp;
    auto noTouching = [&](const SystemLayout& layout) {
        for (int i = 1; i < 4; ++i)
        {
            const std::string a = std::to_string(i);
            const std::string b = std::to_string(i + 1);
            INFO("between chords " << a << " and " << b);
            CHECK(extent(layout, { "c" + b, "n" + b }).first - extent(layout, { "c" + a, "n" + a }).second
                  >= padding - 1e-9);
        }
    };

    const double natural = layOutAt(std::nullopt).width;

    // A little narrower: the system fits exactly.
    const SystemLayout snug = layOutAt(natural - 2);
    CHECK(snug.width == Approx(natural - 2));
    noTouching(snug);

    // Far too narrow: the system comes out wider than asked, with nothing touching.
    const SystemLayout overfull = layOutAt(5.0);
    CHECK(overfull.width > 5.0);
    CHECK(overfull.width < natural);
    noTouching(overfull);
}
