#pragma once

// JavaScript's arithmetic where it differs from C++'s, so that ported code gives the
// same numbers as the TS it came from.

#include <cmath>

namespace msd
{

// Math.round: halves go up (−2.5 → −2), where std::round goes away from zero (−3).
// Written this way, not as floor(x + 0.5), so that 0.49999999999999994 stays 0.
inline double jsRound(double x)
{
    const double down = std::floor(x);
    return x - down >= 0.5 ? down + 1 : down;
}

} // namespace msd
