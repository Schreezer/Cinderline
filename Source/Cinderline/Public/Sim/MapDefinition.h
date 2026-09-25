#pragma once

#include "Sim/MatchLength.h"
#include "Sim/Navigation.h"

#include <vector>

namespace cinder {

inline constexpr int CurrentMapRevision = 1;
constexpr bool validMapRevision(int revision) { return revision >= 0 && revision <= CurrentMapRevision; }

enum class MapSiteRole { Home, Natural, Third };
enum class MapObstacleKind { RockMass, RetainingWall, Perimeter };
enum class MapRouteKind { Main, Flank, Economy, Drainage };
enum class MapSurface { Soil, Paving, PackedRoad, OreApron };

struct MapResourceSite {
    MapSiteRole role = MapSiteRole::Home;
    int owner = 0;
    Vec2 center{}; // Intended headquarters/processor pad, not the ore centroid.
    std::vector<Vec2> nodes;
    float nodeOre = 0;
};

struct MapPlateau {
    NavBox bounds{};
    float height = 0;
    int level = 0;
};

struct MapRamp {
    Vec2 high{}, low{}; // Centerline endpoints; linear height, high to low.
    float halfWidth = 0;
    float highHeight = 0, lowHeight = 0;
    int highLevel = 0, lowLevel = 0;
};

struct MapRoute {
    MapRouteKind kind = MapRouteKind::Main;
    float halfWidth = 0;
    std::vector<Vec2> points;
};

struct MapSurfaceRegion {
    NavBox bounds{};
    MapSurface surface = MapSurface::Soil;
};

// Immutable authoritative geometry in centimeters, already scaled to this variant.
// No entity/Unreal dependency: simulation, server and presentation use the same record.
struct MapDefinition {
    int map = 0, playerCount = 2, revision = 0;
    MatchLength matchLength = MatchLength::Standard;
    float worldSize = 4800;
    std::vector<Vec2> starts;
    // Home sites are first in team order. Expansion order preserves legacy entity IDs.
    std::vector<MapResourceSite> sites;
    std::vector<NavBox> obstacles;
    std::vector<MapObstacleKind> obstacleKinds; // Exactly parallel to obstacles.
    std::vector<MapPlateau> plateaus;
    std::vector<MapRamp> ramps;
    std::vector<MapRoute> routes;
    std::vector<MapSurfaceRegion> surfaces;

    bool authored() const { return !plateaus.empty(); }
    float terrainHeight(Vec2 point) const;
    // Observer height required to reveal the entire area. Sloped ramp surfaces
    // inherit their lower level; any positive-area upper plateau remainder keeps
    // its height gate. Multiple ramp coverage is deliberately conservative.
    float terrainVisibilityHeight(const NavBox& area) const;
    // Discrete terrain level; a ramp belongs to the upper level from its midpoint uphill.
    int terrainLevel(Vec2 point) const;
    bool onRamp(Vec2 point, float radius = 0) const;
    // Entire circular footprint must be in bounds, clear of obstacles and ramps,
    // and lie on one flat terrain level. Entity occupancy is checked by Simulation.
    bool buildable(Vec2 point, float radius) const;
    MapSurface surfaceAt(Vec2 point) const;
};

// Revision 0 reconstructs original layouts. Revision 1 changes only Standard duel
// Shattered Rift; all other variants retain their exact original geometry/economy.
// Callers accepting persisted/network input must reject !validMapRevision first.
const MapDefinition& mapDefinition(int map, int playerCount, MatchLength length,
    int revision = CurrentMapRevision);

} // namespace cinder
