// Curve shapes, curve values and interpretation profiles. Ported from the TS tests in
// playback.test.ts; the profiles and curves come from the files `pnpm midi` writes.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "core/json/Load.h"
#include "playback/Curves.h"
#include "playback/Interpretation.h"

using namespace msd;
using Catch::Matchers::WithinAbs;

namespace
{

const std::filesystem::path exported = MSD_EXPORTED_SCORES_DIR;

std::filesystem::path exportedFile(const std::string& name)
{
    const std::filesystem::path file = exported / name;
    if (!std::filesystem::exists(file))
        SKIP("no " << file.string() << "; run pnpm midi in prototype-web first");
    return file;
}

std::vector<std::string> idsOf(const auto& rules)
{
    std::vector<std::string> ids;
    for (const auto& rule : rules)
        ids.push_back(rule.id);
    return ids;
}

} // namespace

TEST_CASE("curve shapes always run from 0 to 1, and a = 0 is a straight line", "[playback][curves]")
{
    for (const CurveType type : { CurveType::Linear, CurveType::Exp, CurveType::Power, CurveType::Ease })
    {
        for (const std::optional<double> a : { std::optional<double> {}, std::optional<double> { -2.0 },
                                               std::optional<double> { 0.0 }, std::optional<double> { 2.0 } })
        {
            CHECK_THAT(shape(type, a, 0), WithinAbs(0, 1e-9));
            CHECK_THAT(shape(type, a, 1), WithinAbs(1, 1e-9));
        }
        CHECK_THAT(shape(type, 0.0, 0.3), WithinAbs(0.3, 1e-9));
    }
    CHECK(shape(CurveType::Hold, std::nullopt, 0.99) == 0);
}

TEST_CASE("curve shapes start slowly for positive a and fast for negative a", "[playback][curves]")
{
    CHECK(shape(CurveType::Exp, 3.0, 0.5) < 0.5);
    CHECK(shape(CurveType::Exp, -3.0, 0.5) > 0.5);
    CHECK_THAT(shape(CurveType::Power, 1.0, 0.5), WithinAbs(0.25, 1e-9));
    CHECK_THAT(shape(CurveType::Power, -1.0, 0.5), WithinAbs(0.75, 1e-9));
    // ease is symmetric around the middle
    CHECK_THAT(shape(CurveType::Ease, 1.0, 0.5), WithinAbs(0.5, 1e-9));
    CHECK_THAT(shape(CurveType::Ease, 1.0, 0.25), WithinAbs(0.125, 1e-9));
    CHECK_THAT(shape(CurveType::Ease, 1.0, 0.75), WithinAbs(0.875, 1e-9));
    CHECK_THAT(shape(CurveType::Ease, -1.0, 0.25), WithinAbs(0.375, 1e-9));
}

TEST_CASE("a curve has a base, a scale and an add layer", "[playback][curves]")
{
    const auto segment = [](double start, double ticks, double from, double to,
                            std::optional<CurveCombine> combine = std::nullopt) {
        return CurveEvent {
            .combine = combine, .startTick = start, .durationTicks = ticks, .fromValue = from, .toValue = to
        };
    };
    const std::vector<CurveEvent> curve {
        segment(0, 0, 0.5, 0.5),
        segment(100, 100, 0.5, 1),                       // a hairpin
        segment(150, 0, 0.8, 0.8, CurveCombine::Scale),  // a swell
        segment(150, 0, 0.1, 0.1, CurveCombine::Add),    // an accent
    };
    const CurveParameter dynamics = BuiltInCurveParameter::Dynamics;
    CHECK(evaluateCurve(curve, dynamics, 50, 0) == 0.5);
    CHECK(evaluateCurve(curve, dynamics, 150, 0, std::nullopt, CurveLayer::Base) == 0.75);
    CHECK_THAT(evaluateCurve(curve, dynamics, 150, 0), WithinAbs(0.75 * 0.8 + 0.1, 1e-12));
    CHECK(evaluateCurve(curve, BuiltInCurveParameter::Sustain, 150, 0.25) == 0.25);
}

TEST_CASE("sampling a curve in order is the same as evaluating it tick by tick", "[playback][curves]")
{
    std::vector<CurveEvent> curves;
    for (const MusicEvent& event : loadMusicEvents(exportedFile("melody.events.json")))
        if (const auto* curve = std::get_if<CurveEvent>(&event))
            curves.push_back(*curve);
    REQUIRE_FALSE(curves.empty());
    const CurveParameter dynamics = BuiltInCurveParameter::Dynamics;
    CurveSampler sample(curves, dynamics, 0.5);
    for (double tick = 0; tick < 16000; tick += 97)
        CHECK(sample(tick) == evaluateCurve(curves, dynamics, tick, 0.5));
}

TEST_CASE("a child profile replaces, disables and adds to its parent", "[playback][profiles]")
{
    const PlaybackSetup setup = loadPlaybackSetup(exportedFile("piano.setup.json"));
    const InterpretationProfile base = resolveInterpretationProfile(setup.interpretationProfiles, "default");
    const InterpretationProfile piano = resolveInterpretationProfile(setup.interpretationProfiles, "piano");

    std::vector<std::string> unslurred = idsOf(base.noteRules);
    std::erase_if(unslurred, [](const std::string& id) { return id.starts_with("slur"); });
    CHECK(idsOf(piano.noteRules) == unslurred);
    REQUIRE_FALSE(piano.noteRules.empty());
    CHECK(piano.noteRules[0].id == "detached");
    REQUIRE(piano.noteRules[0].actions.size() == 1);
    const auto* ratio = std::get_if<DurationAction>(&piano.noteRules[0].actions[0]);
    REQUIRE(ratio != nullptr);
    CHECK(ratio->op == DurationOp::SetRatio);
    CHECK(ratio->value == 1);
    REQUIRE(piano.dynamics);
    REQUIRE(base.dynamics);
    CHECK(piano.dynamics->size() == base.dynamics->size());

    const InterpretationProfile strings = resolveInterpretationProfile(setup.interpretationProfiles, "strings");
    REQUIRE(strings.dynamics);
    const auto sfz = std::find_if(strings.dynamics->begin(), strings.dynamics->end(),
                                  [](const DynamicMapping& d) { return d.type == DynamicType::Sfz; });
    REQUIRE(sfz != strings.dynamics->end());
    CHECK(sfz->value == 0.4);
    CHECK(strings.dynamics->size() == base.dynamics->size());
    const std::vector<std::string> curveRules = idsOf(strings.curveRules);
    CHECK(std::find(curveRules.begin(), curveRules.end(), "molto-vibrato") != curveRules.end());

    CHECK_THROWS(resolveInterpretationProfile(setup.interpretationProfiles, "nope"));
}
