#pragma once

#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

namespace cinder {

using Id = std::uint32_t;

struct Vec2 {
    float x = 0;
    float y = 0;
};

struct NavCircle {
    Id id = 0;
    Vec2 center;
    float radius = 0;
};

struct NavBox {
    Vec2 center;
    Vec2 half;
};

struct NavigationResult {
    bool reached = false;
    bool exhausted = false;
    std::vector<Vec2> points;
    float cost = 0;
    int expanded = 0;
};

// Portable deterministic routing for the simulation and authoritative server.
// Geometry and clearance layers are shared by copies until a copy is changed.
class Navigation {
public:
    Navigation();
    Navigation(const Navigation& other);
    Navigation(Navigation&& other) noexcept;
    Navigation& operator=(const Navigation& other);
    Navigation& operator=(Navigation&& other) noexcept;
    ~Navigation();

    // Reuses existing derived data when the normalized geometry is unchanged.
    void sync(float worldSize, const std::vector<NavBox>& boxes,
        const std::vector<NavCircle>& circles);

    // Detaches a copied Navigation and invalidates its derived layers. This is
    // intended for speculative placement checks.
    void addCircle(NavCircle circle);

    bool pointClear(Vec2 point, float clearance, Id ignore = 0) const;
    bool segmentClear(Vec2 from, Vec2 to, float clearance, Id ignore = 0) const;

    // Chooses the shortest reachable exact goal. An already reached goal has
    // an empty point list and zero cost. Search work is capped per request.
    NavigationResult route(Vec2 from, const std::vector<Vec2>& goals,
        float clearance, Id ignore = 0) const;

    // Stable fingerprint of normalized source geometry, suitable for retry
    // invalidation after save/load. Derived layers are not part of the value.
    std::uint64_t geometryVersion() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace cinder
