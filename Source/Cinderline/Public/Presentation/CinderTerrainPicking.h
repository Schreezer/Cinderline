#pragma once

#include <algorithm>
#include <cmath>

namespace CinderTerrainPicking
{
struct Point3
{
    double X = 0, Y = 0, Z = 0;
};

constexpr int MarchSteps = 16;
constexpr int RefinementSteps = 16;
constexpr double HeightTolerance = 0.05; // Centimeters.

inline bool IsFinite(Point3 Point)
{
    return std::isfinite(Point.X) && std::isfinite(Point.Y) && std::isfinite(Point.Z);
}

// Intersect a downward camera ray with the surface actually presented to the
// player. Height bounds delimit the search independently of camera altitude.
// HeightAt accepts world X/Y and returns world Z. No actor/physics/entity query
// is required. On invalid input or no surface crossing, Out remains unchanged.
template <typename HeightQuery>
bool Intersect(Point3 Origin, Point3 Direction, double MinimumHeight,
    double MaximumHeight, HeightQuery&& HeightAt, Point3& Out)
{
    if (!IsFinite(Origin) || !IsFinite(Direction) || Direction.Z >= -0.001
        || !std::isfinite(MinimumHeight) || !std::isfinite(MaximumHeight)
        || MinimumHeight > MaximumHeight) return false;

    const double Begin = std::max(0.0, (MaximumHeight - Origin.Z) / Direction.Z);
    const double End = (MinimumHeight - Origin.Z) / Direction.Z;
    if (!std::isfinite(Begin) || !std::isfinite(End) || End < Begin) return false;

    const auto Sample = [&](double Distance, Point3& Point, double& Gap)
    {
        Point = {Origin.X + Direction.X * Distance, Origin.Y + Direction.Y * Distance,
            Origin.Z + Direction.Z * Distance};
        if (!IsFinite(Point)) return false;
        const double Height = HeightAt(Point.X, Point.Y);
        if (!std::isfinite(Height) || Height < MinimumHeight - HeightTolerance
            || Height > MaximumHeight + HeightTolerance) return false;
        Gap = Point.Z - Height;
        return true;
    };

    Point3 Point;
    double Gap = 0;
    if (!Sample(Begin, Point, Gap) || Gap < -HeightTolerance) return false;
    if (std::abs(Gap) <= HeightTolerance) { Out = Point; return true; }

    double Previous = Begin;
    for (int Step = 1; Step <= MarchSteps; ++Step)
    {
        const double Next = Begin + (End - Begin) * (static_cast<double>(Step) / MarchSteps);
        if (!Sample(Next, Point, Gap)) return false;
        if (std::abs(Gap) <= HeightTolerance) { Out = Point; return true; }
        if (Gap < 0)
        {
            double Above = Previous, Below = Next;
            for (int Refinement = 0; Refinement < RefinementSteps; ++Refinement)
            {
                const double Middle = (Above + Below) * 0.5;
                if (!Sample(Middle, Point, Gap)) return false;
                if (std::abs(Gap) <= HeightTolerance) { Out = Point; return true; }
                if (Gap > 0) Above = Middle;
                else Below = Middle;
            }
            // A discontinuous cliff edge is not a point on the walkable surface.
            // Never return an XY from a ray whose final Z disagrees with ground.
            return false;
        }
        Previous = Next;
    }
    return false;
}
}
