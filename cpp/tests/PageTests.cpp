// Systems of several staves, transposing instruments, the reflow into Layout.flow, and
// pages laid out from a flow.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

#include "core/json/Load.h"
#include "layout/PageLayout.h"
#include "layout/SmuflFont.h"
#include "layout/SystemLayout.h"
#include "layout/Transpose.h"

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

// ── Small scores as JSON ───────────────────────────────────────────────────────

std::string chord(const std::string& id, int type, const char* step, int octave, int alter = 0)
{
    return R"({ "id": ")" + id + R"(", "kind": "chord", "duration": { "type": )" + std::to_string(type)
           + R"(, "dots": 0 }, "articulations": [], "notes": [{ "id": ")" + id + R"(n", "kind": "note",
           "pitch": { "step": ")"
           + step + R"(", "alter": )" + std::to_string(alter) + R"(, "octave": )" + std::to_string(octave) + " } }] }";
}

// n chords of one duration type on B4, ids <prefix>1, <prefix>2, …
std::string repeated(const std::string& prefix, int count, int type)
{
    std::string events;
    for (int i = 1; i <= count; ++i)
        events += (i > 1 ? "," : "") + chord(prefix + std::to_string(i), type, "b", 4);
    return events;
}

struct StaffSpec
{
    std::string id;
    std::vector<std::string> measures; // events per measure; missing ones get four quarters
    const char* clef = R"("type": "g", "line": 2)";
};

struct TrackSpec
{
    std::string id;
    std::vector<StaffSpec> staves;
    std::string extra = ""; // more fields, e.g. a transposition
};

// Measures m1, m2, … in 4/4 with the given key.
MusicData score(const std::vector<TrackSpec>& tracks, std::size_t measureCount, int key = 0)
{
    std::string measures;
    for (std::size_t i = 1; i <= measureCount; ++i)
    {
        measures += (i > 1 ? "," : "") + std::string(R"({ "id": "m)") + std::to_string(i)
                    + R"(", "kind": "measure", "tempos": [], "markers": [], "jumps": [])";
        if (i == 1)
            measures += R"(, "timeSig": { "id": "ts", "kind": "time-signature", "beats": 4, "unit": 4 },
                          "key": { "id": "key", "kind": "key-signature", "accidentals": )"
                        + std::to_string(key) + " }";
        measures += " }";
    }

    std::string trackJson;
    for (const TrackSpec& track : tracks)
    {
        std::string staves;
        for (const StaffSpec& staff : track.staves)
        {
            std::string staffMeasures;
            for (std::size_t i = 1; i <= measureCount; ++i)
            {
                const std::string id = staff.id + "-m" + std::to_string(i);
                const std::string events =
                    i <= staff.measures.size() ? staff.measures[i - 1] : repeated(id + "-c", 4, 0);
                staffMeasures +=
                    (i > 1 ? "," : "") + std::string(R"({ "id": ")") + id
                    + R"(", "kind": "staff-measure", "measureId": "m)" + std::to_string(i) + R"(", "clefs": [)"
                    + (i == 1 ? R"({ "tick": 0, "clef": { "id": ")" + staff.id + R"(-clef", "kind": "clef", )"
                                    + staff.clef + R"(, "octaveChange": 0 } })"
                              : std::string())
                    + R"(], "voices": [{ "id": ")" + id + R"(-v", "kind": "voice", "index": 0, "events": [)" + events
                    + "] }] }";
            }
            staves += (staves.empty() ? "" : ",") + std::string(R"({ "id": ")") + staff.id
                      + R"(", "kind": "staff", "staffSpanners": [], "measures": [)" + staffMeasures + "] }";
        }
        trackJson += (trackJson.empty() ? "" : ",") + std::string(R"({ "id": ")") + track.id
                     + R"(", "kind": "track", "instrumentName": ")" + track.id + R"(", "instrumentAbbr": ")" + track.id
                     + R"(", "trackSpanners": [], "attachments": [], "staves": [)" + staves + "]" + track.extra + " }";
    }

    return parseMusicData(R"({ "title": "t", "composer": "", "tpq": 480, "globalSpanners": [], "measures": [)"
                          + measures + R"(], "tracks": [)" + trackJson + "] }");
}

// One flute staff of `count` measures of four quarters.
MusicData oneStaff(std::size_t count) { return score({ { "fl", { { "s1" } } } }, count); }

const Layout& fullScore()
{
    static const Layout layout { .id = "score", .name = "Score", .layoutProfileId = "default" };
    return layout;
}

std::vector<const LayoutBox*> boxesOf(const SystemLayout& layout, Part part, const NodeId& id = {})
{
    std::vector<const LayoutBox*> found;
    for (const LayoutBox& box : layout.boxes)
        if (box.part == part && (id.empty() || box.nodeId == id))
            found.push_back(&box);
    return found;
}

const LayoutBox& one(const SystemLayout& layout, Part part, const NodeId& id)
{
    const auto found = boxesOf(layout, part, id);
    REQUIRE(found.size() == 1);
    return *found[0];
}

Point originOf(const LayoutBox& box) { return std::get<GlyphShape>(box.shape).origin; }

double contentWidth(const LayoutProfile& profile)
{
    const LayoutProfile::Page& page = profile.page;
    return (page.widthMm - page.margins.left - page.margins.right) / profile.staffSpaceMm;
}

std::vector<SystemStaff> staves(std::initializer_list<NodeId> ids)
{
    std::vector<SystemStaff> result;
    for (const NodeId& id : ids)
        result.push_back({ id, 0 });
    return result;
}

std::vector<const FlowSystem*> systemsOf(const Flow& flow)
{
    std::vector<const FlowSystem*> systems;
    for (const FlowPage& page : flow.pages)
        for (const FlowSystem& system : page.systems)
            systems.push_back(&system);
    return systems;
}

std::size_t measureIndex(const MusicData& music, const NodeId& id)
{
    const auto it = std::find_if(music.measures.begin(), music.measures.end(),
                                 [&](const Measure& measure) { return measure.id == id; });
    REQUIRE(it != music.measures.end());
    return static_cast<std::size_t>(it - music.measures.begin());
}

} // namespace

// ── Transposing instruments ────────────────────────────────────────────────────

TEST_CASE("transposing instruments are written in their own pitch and key", "[transpose]")
{
    const Transposition clarinet { .diatonic = -1, .chromatic = -2, .octave = 0 }; // B♭: sounds a tone lower
    const Transposition horn { .diatonic = -4, .chromatic = -7, .octave = 0 };     // F: a fifth lower

    const Pitch c5 { .step = Step::C, .alter = 0, .octave = 5 };
    const Pitch d5 = writtenPitch(c5, clarinet);
    CHECK((d5.step == Step::D && d5.alter == 0 && d5.octave == 5));
    const Pitch bFlat3 { .step = Step::B, .alter = -1, .octave = 3 };
    const Pitch c4 = writtenPitch(bFlat3, clarinet);
    CHECK((c4.step == Step::C && c4.alter == 0 && c4.octave == 4));
    const Pitch g4 = writtenPitch({ .step = Step::C, .alter = 0, .octave = 4 }, horn);
    CHECK((g4.step == Step::G && g4.alter == 0 && g4.octave == 4));
    const Pitch fSharp = writtenPitch({ .step = Step::E, .alter = 0, .octave = 4 }, clarinet);
    CHECK((fSharp.step == Step::F && fSharp.alter == 1));
    CHECK(writtenPitch(c5, std::nullopt).step == Step::C);

    CHECK(writtenKey(0, clarinet) == 2);  // C major → D major
    CHECK(writtenKey(-3, clarinet) == -1); // E♭ major → F major
    CHECK(writtenKey(0, horn) == 1);       // C major → G major
    CHECK(writtenKey(5, clarinet) == 7);   // B major → C♯ major
    CHECK(writtenKey(6, clarinet) == -4);  // F♯ major → G♯ major, 8 sharps: A♭ major instead
    CHECK(writtenKey(-2, std::nullopt) == -2);
}

TEST_CASE("a transposing staff shows its written notes and key", "[layout][transpose]")
{
    const MusicData music = score({ { "fl", { { "fl1", { chord("f", 0, "c", 5) } } } },
                                    { "cl",
                                      { { "cl1", { chord("c", 0, "c", 5) } } },
                                      R"(, "transposition": { "diatonic": -1, "chromatic": -2, "octave": 0 })" } },
                                  1);
    const SystemLayout layout = layoutSystem(
        { .music = music, .profile = defaultProfile(), .font = leland(), .staves = { { "fl1", 0 }, { "cl1", 12 } } });

    CHECK(originOf(one(layout, Part::Notehead, "fn")).y == Approx(1.5));     // C5
    CHECK(originOf(one(layout, Part::Notehead, "cn")).y == Approx(12 + 1.0)); // written D5

    // Concert C major: nothing on the flute, two sharps on the clarinet.
    const auto keys = boxesOf(layout, Part::KeySignature);
    REQUIRE(keys.size() == 2);
    for (const LayoutBox* box : keys)
        CHECK(box->bounds.top > 10);
}

// ── Several staves ─────────────────────────────────────────────────────────────

TEST_CASE("notes sounding together line up on every staff", "[layout][staves]")
{
    // Flute quarters over piano eighths; the piano's right hand has a sharp on beat 2.
    const std::string rh = chord("r1", -1, "b", 4) + "," + chord("r2", -1, "b", 4) + "," + chord("r3", -1, "f", 5, 1)
                           + "," + repeated("rx", 5, -1);
    const MusicData music = score({ { "fl", { { "fl1", { repeated("f", 4, 0) } } } },
                                    { "pno", { { "rh", { rh } }, { "lh", {}, R"("type": "f", "line": 4)" } } } },
                                  1);
    const SystemLayout layout = layoutSystem({ .music = music,
                                               .profile = defaultProfile(),
                                               .font = leland(),
                                               .staves = { { "fl1", 0 }, { "rh", 12 }, { "lh", 22.5 } } });

    // Beat 2: the flute's second quarter, the piano's third eighth.
    const double flute = originOf(one(layout, Part::Notehead, "f2n")).x;
    CHECK(originOf(one(layout, Part::Notehead, "r3n")).x == Approx(flute));
    CHECK(originOf(one(layout, Part::Notehead, "lh-m1-c2n")).x == Approx(flute));

    // The sharp on the piano makes room before beat 2 for every staff.
    const LayoutBox& sharp = one(layout, Part::Accidental, "r3n");
    const double padding = defaultProfile().padding.collisionSp;
    CHECK(sharp.bounds.left - one(layout, Part::Notehead, "r2n").bounds.right >= padding - 1e-9);
}

TEST_CASE("barlines join the staves of one instrument, and a brace marks them", "[layout][staves]")
{
    const MusicData music =
        score({ { "fl", { { "fl1" } } }, { "pno", { { "rh" }, { "lh", {}, R"("type": "f", "line": 4)" } } } }, 2);
    const SystemLayout layout = layoutSystem({ .music = music,
                                               .profile = defaultProfile(),
                                               .font = leland(),
                                               .staves = { { "fl1", 0 }, { "rh", 12 }, { "lh", 22.5 } },
                                               .measureCount = 2 });

    // m2's barlines: the flute's alone, the piano's through both staves.
    const auto barlines = boxesOf(layout, Part::Barline, "m2");
    REQUIRE(barlines.size() == 2);
    CHECK(barlines[0]->bounds.top == Approx(0));
    CHECK(barlines[0]->bounds.bottom == Approx(4));
    CHECK(barlines[1]->bounds.top == Approx(12));
    CHECK(barlines[1]->bounds.bottom == Approx(26.5));

    // m1: the line at the system's start through every staff, then its own two.
    const auto first = boxesOf(layout, Part::Barline, "m1");
    REQUIRE(first.size() == 3);
    CHECK(first[0]->bounds.left == Approx(0));
    CHECK(first[0]->bounds.top == Approx(0));
    CHECK(first[0]->bounds.bottom == Approx(26.5));

    // The brace spans the piano's staves, left of the system.
    const LayoutBox& brace = one(layout, Part::Brace, "pno");
    CHECK(brace.bounds.top == Approx(12));
    CHECK(brace.bounds.bottom == Approx(26.5));
    CHECK(brace.bounds.right == Approx(-HeaderSpacing {}.braceGapSp));
    CHECK(std::get<GlyphShape>(brace.shape).scale > 3);
    CHECK(boxesOf(layout, Part::Brace).size() == 1);

    REQUIRE(layout.staves.size() == 3);
    CHECK(layout.staves[2].staffId == "lh");
    CHECK(layout.staves[2].y == 22.5);
}

TEST_CASE("a system's natural width is the sum of its measures' widths", "[layout][staves]")
{
    const MusicData music = score({ { "fl", { { "fl1", { repeated("a", 8, -1), repeated("b", 2, 1) } } } } }, 3, 3);
    const SystemInput all {
        .music = music, .profile = defaultProfile(), .font = leland(), .staves = staves({ "fl1" }), .measureCount = 3
    };
    const std::vector<MeasureWidth> widths = measureWidths(all);
    REQUIRE(widths.size() == 3);
    CHECK(widths[0].first > widths[0].other); // the clef and key

    CHECK(layoutSystem(all).width == Approx(widths[0].first + widths[1].other + widths[2].other));
    SystemInput later = all;
    later.firstMeasure = 1;
    later.measureCount = 2;
    CHECK(layoutSystem(later).width == Approx(widths[1].first + widths[2].other));
}

// ── Reflow ─────────────────────────────────────────────────────────────────────

TEST_CASE("reflow fills each system as far as the page's width allows", "[pages]")
{
    const MusicData music = oneStaff(60);
    const LayoutProfile& profile = defaultProfile();
    const Flow flow = reflow({ .music = music, .profile = profile, .font = leland(), .layout = fullScore() });
    const auto systems = systemsOf(flow);
    REQUIRE(systems.size() > 2);

    const std::vector<MeasureWidth> widths = measureWidths(
        { .music = music, .profile = profile, .font = leland(), .staves = staves({ "s1" }), .measureCount = 60 });
    for (std::size_t k = 0; k < systems.size(); ++k)
    {
        INFO("system " << k);
        const std::size_t start = measureIndex(music, systems[k]->startMeasureId);
        const std::size_t end = k + 1 < systems.size() ? measureIndex(music, systems[k + 1]->startMeasureId) : 60;
        const double available = contentWidth(profile) - (k == 0 ? profile.distances.firstIndentSp : 0);
        double natural = widths[start].first;
        for (std::size_t i = start + 1; i < end; ++i)
            natural += widths[i].other;
        CHECK(natural <= available);
        if (end < 60)
            CHECK(natural + widths[end].other > available); // the next measure wouldn't fit
    }
    CHECK(systems.front()->startMeasureId == "m1");
}

TEST_CASE("reflow keeps the user's breaks", "[pages]")
{
    const MusicData music = oneStaff(30);
    Layout layout = fullScore();
    layout.elements = {
        BreakElement { .id = "b1", .nodeId = "m2", .type = BreakType::System },
        BreakElement { .id = "b2", .nodeId = "m4", .type = BreakType::Page },
    };
    const Flow plain = reflow({ .music = music, .profile = defaultProfile(), .font = leland(), .layout = fullScore() });
    const std::size_t naturalFirst = measureIndex(music, systemsOf(plain).at(1)->startMeasureId);
    REQUIRE(naturalFirst > 2);

    const Flow flow = reflow({ .music = music, .profile = defaultProfile(), .font = leland(), .layout = layout });
    REQUIRE(flow.pages.size() >= 2);
    REQUIRE(flow.pages[0].systems.size() == 2);
    CHECK(flow.pages[0].systems[0].startMeasureId == "m1");
    CHECK(flow.pages[0].systems[1].startMeasureId == "m3");
    CHECK(flow.pages[1].systems[0].startMeasureId == "m5");

    // A no-break keeps a measure with the next even past the page's width.
    const NodeId& last = music.measures[naturalFirst - 1].id;
    layout.elements = { NoBreakElement { .id = "n1", .nodeId = last } };
    const Flow kept = reflow({ .music = music, .profile = defaultProfile(), .font = leland(), .layout = layout });
    CHECK(measureIndex(music, systemsOf(kept).at(1)->startMeasureId) > naturalFirst);
}

TEST_CASE("reflow stacks staves and systems by the profile's distances", "[pages]")
{
    const MusicData music =
        score({ { "fl", { { "fl1" } } }, { "pno", { { "rh" }, { "lh", {}, R"("type": "f", "line": 4)" } } } }, 40);
    LayoutProfile profile = defaultProfile();
    profile.page.heightMm = 160; // two systems per page
    Layout layout = fullScore();
    layout.elements = { StaffGapElement { .id = "g", .nodeId = "m1", .staffId = "rh", .extraSp = 2 } };
    const Flow flow = reflow({ .music = music, .profile = profile, .font = leland(), .layout = layout });

    const LayoutProfile::Distances& d = profile.distances;
    REQUIRE(flow.pages.size() > 1);
    const FlowSystem& first = flow.pages[0].systems.at(0);
    CHECK(first.ySp == 0);
    CHECK(first.staffYSp.at("fl1") == 0);
    CHECK(first.staffYSp.at("rh") == Approx(4 + d.trackSp));
    CHECK(first.staffYSp.at("lh") == Approx(4 + d.trackSp + 4 + d.staffSp + 2));

    const double height = first.staffYSp.at("lh") + 4;
    const double contentHeight =
        (profile.page.heightMm - profile.page.margins.top - profile.page.margins.bottom) / profile.staffSpaceMm;
    for (const FlowPage& page : flow.pages)
        for (std::size_t k = 0; k < page.systems.size(); ++k)
        {
            CHECK(page.systems[k].ySp == Approx(k * (height + d.systemSp)));
            CHECK(page.systems[k].ySp + height <= contentHeight);
        }
    // Each page is as full as it can be.
    CHECK(flow.pages[0].systems.size() * (height + d.systemSp) + height > contentHeight);
}

// ── Pages from a flow ──────────────────────────────────────────────────────────

TEST_CASE("pages place each system where the flow says", "[pages]")
{
    const MusicData music = oneStaff(30);
    const LayoutProfile& profile = defaultProfile();
    const ScoreInput input { .music = music, .profile = profile, .font = leland(), .layout = fullScore() };
    const Flow flow = reflow(input);
    const std::vector<PageLayout> pages = layoutPages(input, flow);

    REQUIRE(pages.size() == flow.pages.size());
    const double mm = profile.staffSpaceMm;
    const double left = profile.page.margins.left / mm;
    const double top = profile.page.margins.top / mm;
    std::vector<NodeId> measures;
    for (std::size_t p = 0; p < pages.size(); ++p)
    {
        CHECK(pages[p].widthSp == Approx(profile.page.widthMm / mm));
        REQUIRE(pages[p].systems.size() == flow.pages[p].systems.size());
        for (std::size_t s = 0; s < pages[p].systems.size(); ++s)
        {
            const PlacedSystem& placed = pages[p].systems[s];
            const bool first = p == 0 && s == 0;
            const bool last = p + 1 == pages.size() && s + 1 == pages[p].systems.size();
            CHECK(placed.origin.x == Approx(left + (first ? profile.distances.firstIndentSp : 0)));
            CHECK(placed.origin.y == Approx(top + flow.pages[p].systems[s].ySp));
            CHECK(placed.system.measures.front().measureId == flow.pages[p].systems[s].startMeasureId);
            if (!last)
                CHECK(placed.system.width
                      == Approx(contentWidth(profile) - (first ? profile.distances.firstIndentSp : 0)));
            for (const MeasureSlot& slot : placed.system.measures)
                measures.push_back(slot.measureId);
        }
    }
    REQUIRE(measures.size() == 30); // every measure once, in order
    for (std::size_t i = 0; i < 30; ++i)
        CHECK(measures[i] == music.measures[i].id);
}

TEST_CASE("the last system stretches only when it is nearly full", "[pages]")
{
    const LayoutProfile& profile = defaultProfile();
    const MusicData shortScore = oneStaff(2);
    const ScoreInput input { .music = shortScore, .profile = profile, .font = leland(), .layout = fullScore() };
    const std::vector<PageLayout> pages = layoutPages(input, reflow(input));
    const double available = contentWidth(profile) - profile.distances.firstIndentSp;
    const double natural = layoutSystem({ .music = shortScore,
                                          .profile = profile,
                                          .font = leland(),
                                          .staves = staves({ "s1" }),
                                          .measureCount = 2 })
                               .width;
    REQUIRE(natural < profile.breaking.lastSystemFillMin * available);
    CHECK(pages.at(0).systems.at(0).system.width == Approx(natural));
}

TEST_CASE("pages keep their systems when measures are added or deleted", "[pages]")
{
    const MusicData before = oneStaff(30);
    const ScoreInput beforeInput {
        .music = before, .profile = defaultProfile(), .font = leland(), .layout = fullScore()
    };
    const Flow flow = reflow(beforeInput);
    const auto starts = systemsOf(flow);
    REQUIRE(starts.size() >= 3);
    const std::size_t secondStart = measureIndex(before, starts[1]->startMeasureId);

    auto systemCount = [](const std::vector<PageLayout>& pages) {
        std::size_t count = 0;
        for (const PageLayout& page : pages)
            count += page.systems.size();
        return count;
    };
    auto firstSystem = [](const std::vector<PageLayout>& pages) -> const SystemLayout& {
        return pages.at(0).systems.at(0).system;
    };

    SECTION("a measure added inside the first system squeezes into it")
    {
        MusicData after = before;
        const auto at = static_cast<std::ptrdiff_t>(secondStart - 1);
        Measure added = after.measures[static_cast<std::size_t>(at)];
        added.id = "new";
        after.measures.insert(after.measures.begin() + at + 1, added);
        StaffMeasure addedStaff = after.tracks[0].staves[0].measures[static_cast<std::size_t>(at)];
        addedStaff.measureId = "new";
        addedStaff.id = "new-sm";
        after.tracks[0].staves[0].measures.insert(after.tracks[0].staves[0].measures.begin() + at + 1, addedStaff);

        const ScoreInput input { .music = after, .profile = defaultProfile(), .font = leland(), .layout = fullScore() };
        const std::vector<PageLayout> pages = layoutPages(input, flow);
        CHECK(systemCount(pages) == starts.size());
        CHECK(firstSystem(pages).measures.size() == secondStart + 1);
        CHECK(firstSystem(pages).measures.back().measureId == "new");
        CHECK(firstSystem(pages).width == Approx(firstSystem(layoutPages(beforeInput, flow)).width).epsilon(0.05));
    }

    SECTION("a system whose first measure is deleted joins the one before")
    {
        MusicData after = before;
        after.measures.erase(after.measures.begin() + static_cast<std::ptrdiff_t>(secondStart));
        const ScoreInput input { .music = after, .profile = defaultProfile(), .font = leland(), .layout = fullScore() };
        const std::vector<PageLayout> pages = layoutPages(input, flow);
        CHECK(systemCount(pages) == starts.size() - 1);
        CHECK(firstSystem(pages).measures.size() > secondStart);
    }
}

TEST_CASE("a staff the flow doesn't know goes below the others", "[pages]")
{
    const MusicData one = score({ { "fl", { { "fl1" } } } }, 4);
    const MusicData two = score({ { "fl", { { "fl1" } } }, { "ob", { { "ob1" } } } }, 4);
    const Flow flow = reflow({ .music = one, .profile = defaultProfile(), .font = leland(), .layout = fullScore() });
    const std::vector<PageLayout> pages =
        layoutPages({ .music = two, .profile = defaultProfile(), .font = leland(), .layout = fullScore() }, flow);
    const SystemLayout& system = pages.at(0).systems.at(0).system;
    REQUIRE(system.staves.size() == 2);
    CHECK(system.staves[1].staffId == "ob1");
    CHECK(system.staves[1].y == Approx(4 + defaultProfile().distances.trackSp));
}

TEST_CASE("a layout shows only its own tracks", "[pages]")
{
    const MusicData music = score({ { "fl", { { "fl1" } } }, { "ob", { { "ob1" } } } }, 2);
    Layout part = fullScore();
    part.trackIds = std::vector<NodeId> { "ob" };
    CHECK(layoutStaffIds(music, part) == std::vector<NodeId> { "ob1" });
    const Flow flow = reflow({ .music = music, .profile = defaultProfile(), .font = leland(), .layout = part });
    CHECK(flow.pages.at(0).systems.at(0).staffYSp.size() == 1);
}

TEST_CASE("large scores get 1 mm staves on A4", "[pages]")
{
    const LayoutSetup setup = loadLayoutSetup(std::filesystem::path(MSD_RESOURCES_DIR) / "default.layout.json");
    REQUIRE(setup.layoutProfiles.size() == 2);
    const LayoutProfile& large = setup.layoutProfiles[1];
    CHECK(large.id == "large-score");
    CHECK(large.staffSpaceMm == 1.0);
    CHECK(large.page.widthMm == 210);
    CHECK(large.page.heightMm == 297);

    // A system's height: staves, and the distances between them.
    const MusicData quartet =
        score({ { "fl", { { "fl1" } } }, { "pno", { { "rh" }, { "lh", {}, R"("type": "f", "line": 4)" } } } }, 1);
    const LayoutProfile::Distances& d = defaultProfile().distances;
    CHECK(systemHeightSp(quartet, fullScore(), defaultProfile()) == Approx(3 * 4 + d.trackSp + d.staffSp));
    CHECK(systemFitsPage(quartet, fullScore(), defaultProfile()));

    // 20 one-staff instruments: too tall for the default's 1.75 mm staves, not for 1 mm.
    std::vector<TrackSpec> tracks;
    for (int i = 0; i < 20; ++i)
        tracks.push_back({ "t" + std::to_string(i), { { "s" + std::to_string(i) } } });
    const MusicData twenty = score(tracks, 1);
    CHECK_FALSE(systemFitsPage(twenty, fullScore(), defaultProfile()));
    CHECK(systemFitsPage(twenty, fullScore(), large));
}

// Reflows every exported score and lays out all its pages.
TEST_CASE("every exported score lays out on pages", "[.][exported]")
{
    const std::filesystem::path folder = MSD_EXPORTED_SCORES_DIR;
    if (!std::filesystem::exists(folder))
        SKIP("no " << folder.string() << "; run pnpm midi in prototype-web first");

    const LayoutProfile& profile = defaultProfile();
    for (const auto& entry : std::filesystem::directory_iterator(folder))
    {
        const std::string name = entry.path().filename().string();
        if (!name.ends_with(".music.json"))
            continue;
        INFO(name);
        const MusicData music = loadMusicData(entry.path());
        const ScoreInput input { .music = music, .profile = profile, .font = leland(), .layout = fullScore() };
        Flow flow;
        REQUIRE_NOTHROW(flow = reflow(input));
        std::vector<PageLayout> pages;
        REQUIRE_NOTHROW(pages = layoutPages(input, flow));

        std::size_t measure = 0;
        for (const PageLayout& page : pages)
            for (const PlacedSystem& placed : page.systems)
            {
                const SystemLayout& system = placed.system;
                const double available = contentWidth(profile) - (measure == 0 ? profile.distances.firstIndentSp : 0);
                // Wider than the page only when one measure can't fit.
                if (system.measures.size() > 1)
                    CHECK(system.width <= available + 1e-6);
                for (const MeasureSlot& slot : system.measures)
                {
                    REQUIRE(measure < music.measures.size());
                    CHECK(slot.measureId == music.measures[measure].id);
                    ++measure;
                }
                for (const LayoutBox& box : system.boxes)
                    CHECK((std::isfinite(box.bounds.left) && std::isfinite(box.bounds.top)
                           && std::isfinite(box.bounds.right) && std::isfinite(box.bounds.bottom)));
            }
        CHECK(measure == music.measures.size());
    }
}
