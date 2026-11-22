#pragma once

// What the user has selected in the score.

#include <vector>

#include "core/types/MusicData.h"

namespace msd
{

struct Selection
{
    std::vector<NodeId> notes; // in the order they were selected
};

} // namespace msd
