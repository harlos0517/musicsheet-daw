#pragma once

// The JSON name of every enum value, in one table per enum. The loader reads with
// these, and a writer, logging or a debugger view can use the same tables later.
//
//   enumFromName<ClefType>("percussion")  → ClefType::Percussion
//   enumFromName<ClefType>("bass")        → std::nullopt
//   enumName(ClefType::G)                 → "g"
//
// DurationType has no table: it is a number in the JSON, as in TS.

#include <optional>
#include <string_view>
#include <utility>

#include "core/types/LayoutProfile.h"
#include "core/types/LayoutSetup.h"
#include "core/types/MusicData.h"

namespace msd
{

// Specialised once per enum below. A specialisation holds
//   static constexpr std::pair<E, std::string_view> entries[] = { ... };
template <class E>
struct EnumNames;

// True for enums that have a table here.
template <class E>
concept NamedEnum = requires { EnumNames<E>::entries; };

template <NamedEnum E>
constexpr std::optional<E> enumFromName(std::string_view name)
{
    for (const auto& [value, text] : EnumNames<E>::entries)
        if (text == name)
            return value;
    return std::nullopt;
}

template <NamedEnum E>
constexpr std::string_view enumName(E value)
{
    for (const auto& [v, text] : EnumNames<E>::entries)
        if (v == value)
            return text;
    return {};
}

// Writes a specialisation of EnumNames for one enum.
#define MSD_ENUM_NAMES(Enum, ...)                                                       \
    template <>                                                                         \
    struct EnumNames<Enum>                                                              \
    {                                                                                   \
        static constexpr std::pair<Enum, std::string_view> entries[] = { __VA_ARGS__ }; \
    };

// The tables keep one value per line; clang-format would pack them together.
// clang-format off

// ── MusicData ──────────────────────────────────────────────────────────────────

MSD_ENUM_NAMES(ArticulationType,
    { ArticulationType::Accent, "accent" },
    { ArticulationType::Tenuto, "tenuto" },
    { ArticulationType::Staccato, "staccato" },
    { ArticulationType::Staccatissimo, "staccatissimo" })

MSD_ENUM_NAMES(ClefType,
    { ClefType::G, "g" },
    { ClefType::F, "f" },
    { ClefType::C, "c" },
    { ClefType::Percussion, "percussion" })

MSD_ENUM_NAMES(Step,
    { Step::C, "c" },
    { Step::D, "d" },
    { Step::E, "e" },
    { Step::F, "f" },
    { Step::G, "g" },
    { Step::A, "a" },
    { Step::B, "b" })

MSD_ENUM_NAMES(BarlineStyle,
    { BarlineStyle::Single, "single" },
    { BarlineStyle::Double, "double" },
    { BarlineStyle::Dashed, "dashed" },
    { BarlineStyle::Final, "final" })

MSD_ENUM_NAMES(DynamicType,
    { DynamicType::Pppp, "pppp" },
    { DynamicType::Ppp, "ppp" },
    { DynamicType::Pp, "pp" },
    { DynamicType::P, "p" },
    { DynamicType::Mp, "mp" },
    { DynamicType::Mf, "mf" },
    { DynamicType::F, "f" },
    { DynamicType::Ff, "ff" },
    { DynamicType::Fff, "fff" },
    { DynamicType::Ffff, "ffff" },
    { DynamicType::Sfz, "sfz" },
    { DynamicType::Fp, "fp" },
    { DynamicType::Sf, "sf" },
    { DynamicType::Rfz, "rfz" })

MSD_ENUM_NAMES(BeamMode,
    { BeamMode::BreakBefore, "break-before" },
    { BeamMode::JoinPrevious, "join-previous" },
    { BeamMode::None, "none" })

MSD_ENUM_NAMES(AccidentalDisplay,
    { AccidentalDisplay::Show, "show" },
    { AccidentalDisplay::Hide, "hide" },
    { AccidentalDisplay::Cautionary, "cautionary" })

MSD_ENUM_NAMES(GracePlacement,
    { GracePlacement::Before, "before" },
    { GracePlacement::After, "after" })

MSD_ENUM_NAMES(MarkerType,
    { MarkerType::Segno, "segno" },
    { MarkerType::Coda, "coda" },
    { MarkerType::Fine, "fine" },
    { MarkerType::ToCoda, "to-coda" })

MSD_ENUM_NAMES(HairpinType,
    { HairpinType::Cresc, "cresc" },
    { HairpinType::Dim, "dim" })

MSD_ENUM_NAMES(PedalType,
    { PedalType::Sustain, "sustain" },
    { PedalType::Sostenuto, "sostenuto" },
    { PedalType::UnaCorda, "una-corda" })

MSD_ENUM_NAMES(TempoChangeType,
    { TempoChangeType::Rit, "rit" },
    { TempoChangeType::Accel, "accel" })

MSD_ENUM_NAMES(TechniqueScope,
    { TechniqueScope::This, "this" },
    { TechniqueScope::UntilNext, "until-next" })

MSD_ENUM_NAMES(Notehead,
    { Notehead::Normal, "normal" },
    { Notehead::X, "x" },
    { Notehead::CircleX, "circle-x" },
    { Notehead::Triangle, "triangle" },
    { Notehead::Diamond, "diamond" })

// ── Layout ─────────────────────────────────────────────────────────────────────

MSD_ENUM_NAMES(PlacedKind,
    { PlacedKind::Dynamic, "dynamic" },
    { PlacedKind::Hairpin, "hairpin" },
    { PlacedKind::Tempo, "tempo" },
    { PlacedKind::Text, "text" },
    { PlacedKind::Technique, "technique" },
    { PlacedKind::Pedal, "pedal" })

MSD_ENUM_NAMES(Side,
    { Side::Above, "above" },
    { Side::Below, "below" })

MSD_ENUM_NAMES(BreakType,
    { BreakType::System, "system" },
    { BreakType::Page, "page" })

MSD_ENUM_NAMES(StemDirection,
    { StemDirection::Up, "up" },
    { StemDirection::Down, "down" })

// clang-format on

#undef MSD_ENUM_NAMES

} // namespace msd
