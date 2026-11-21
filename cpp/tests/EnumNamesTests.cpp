#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string_view>

#include "core/json/EnumNames.h"

using namespace msd;

// Every name maps back to its value, and no two values share a name.
TEMPLATE_TEST_CASE("enum names round-trip", "[enum]", ArticulationType, ClefType, Step, BarlineStyle, DynamicType,
                   BeamMode, AccidentalDisplay, GracePlacement, MarkerType, HairpinType, PedalType, TempoChangeType,
                   TechniqueScope, Notehead, PlacedKind, Side, BreakType, StemDirection, CurveType, SlurPosition,
                   CurveCombine, BuiltInCurveParameter, ChangeMode, Phrasing, BeatPlacement, DynamicCombine, DurationOp,
                   ChannelMode)
{
    std::set<std::string_view> seen;
    for (const auto& [value, name] : EnumNames<TestType>::entries)
    {
        CHECK(seen.insert(name).second);
        CHECK(enumFromName<TestType>(name) == value);
        CHECK(enumName(value) == name);
    }
}

TEST_CASE("unknown enum names give nothing", "[enum]")
{
    CHECK_FALSE(enumFromName<ClefType>("bass").has_value());
    CHECK(enumFromName<MarkerType>("to-coda") == MarkerType::ToCoda);
}
