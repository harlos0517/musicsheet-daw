#pragma once

// Interpretation profiles: defaults, and flattening an `extends` chain. Ported from
// prototype-web/src/core/playback/interpretation.ts.

#include <string>
#include <vector>

#include "core/types/InterpretationProfile.h"

namespace msd
{

// Dynamic level before the first dynamic marking, when the profile does not set one.
constexpr double defaultInitialDynamics = 0.5;

// Grace note timing when the profile does not set it: an appoggiatura takes half its
// written length out of the main note, an acciaccatura a quarter of it before the beat.
inline const GraceNoteSettings defaultGraceNotes { .appoggiatura = { .ratio = 0.5 },
                                                   .acciaccatura = { .ratio = 0.25 } };

double applyMode(ChangeMode mode, double current, double value);

// Flattens an `extends` chain into one profile: a child's dynamics replace the parent's
// by type, its rules replace the parent's by id (keeping the parent's position), and
// rules switched off with `enabled: false` are dropped. Throws std::runtime_error for
// an unknown ID.
InterpretationProfile resolveInterpretationProfile(const std::vector<InterpretationProfile>& profiles,
                                                   const std::string& id);

} // namespace msd
