#pragma once

// Plain geometry for layout. Units are staff spaces (sp) unless a name says otherwise;
// y grows downwards, as on screen.

#include <algorithm>

namespace msd
{

struct Point
{
    double x = 0;
    double y = 0;
};

inline Point operator+(Point a, Point b) { return { a.x + b.x, a.y + b.y }; }

struct Rect
{
    double left = 0;
    double top = 0;
    double right = 0;
    double bottom = 0;

    double width() const { return right - left; }
    double height() const { return bottom - top; }

    Rect translated(Point by) const { return { left + by.x, top + by.y, right + by.x, bottom + by.y }; }

    Rect united(const Rect& other) const
    {
        return { std::min(left, other.left), std::min(top, other.top), std::max(right, other.right),
                 std::max(bottom, other.bottom) };
    }
};

} // namespace msd
