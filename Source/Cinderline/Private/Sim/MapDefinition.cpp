#include "Sim/MapDefinition.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <limits>

namespace cinder {
namespace {

bool finite(Vec2 p) { return std::isfinite(p.x) && std::isfinite(p.y); }
Vec2 add(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
Vec2 multiply(Vec2 p, float scale) { return {p.x * scale, p.y * scale}; }
Vec2 mirror(Vec2 p) { return {4800 - p.x, 4800 - p.y}; }
bool contains(const NavBox& box, Vec2 point) {
    return std::fabs(point.x - box.center.x) <= box.half.x &&
        std::fabs(point.y - box.center.y) <= box.half.y;
}
bool intersects(const NavBox& box, Vec2 point, float radius) {
    const float dx = std::max(std::fabs(point.x - box.center.x) - box.half.x, 0.0f);
    const float dy = std::max(std::fabs(point.y - box.center.y) - box.half.y, 0.0f);
    return dx * dx + dy * dy < radius * radius || (radius == 0 && contains(box, point));
}
bool rampCoordinates(const MapRamp& ramp, Vec2 point, float& along, float& across, float& length) {
    const float dx = ramp.low.x - ramp.high.x, dy = ramp.low.y - ramp.high.y;
    length = std::sqrt(dx * dx + dy * dy);
    if (length < 0.001f) return false;
    const float px = point.x - ramp.high.x, py = point.y - ramp.high.y;
    along = (px * dx + py * dy) / length;
    across = (px * -dy + py * dx) / length;
    return true;
}
bool rampContains(const MapRamp& ramp, Vec2 point, float radius = 0) {
    float along = 0, across = 0, length = 0;
    if (!rampCoordinates(ramp, point, along, across, length)) return false;
    const float endDistance = std::max({-along, along - length, 0.0f});
    const float sideDistance = std::max(std::fabs(across) - ramp.halfWidth, 0.0f);
    return endDistance * endDistance + sideDistance * sideDistance <= radius * radius;
}
bool positiveIntersection(const NavBox& a, const NavBox& b, NavBox& intersection) {
    const float left = std::max(a.center.x - a.half.x, b.center.x - b.half.x);
    const float right = std::min(a.center.x + a.half.x, b.center.x + b.half.x);
    const float bottom = std::max(a.center.y - a.half.y, b.center.y - b.half.y);
    const float top = std::min(a.center.y + a.half.y, b.center.y + b.half.y);
    if (left >= right || bottom >= top) return false;
    intersection = {{(left + right) * 0.5f, (bottom + top) * 0.5f},
        {(right - left) * 0.5f, (top - bottom) * 0.5f}};
    return true;
}
bool rampIntersectsArea(const MapRamp& ramp, const NavBox& area) {
    const float dx = ramp.low.x - ramp.high.x, dy = ramp.low.y - ramp.high.y;
    const float length = std::hypot(dx, dy);
    if (length < 0.001f || ramp.halfWidth <= 0) return false;
    const Vec2 along{dx / length, dy / length}, across{-along.y, along.x};
    const Vec2 center{(ramp.high.x + ramp.low.x) * 0.5f, (ramp.high.y + ramp.low.y) * 0.5f};
    const Vec2 delta{area.center.x - center.x, area.center.y - center.y};
    const float halfLength = length * 0.5f;
    // Separating-axis test for a positive-area AABB/oriented-ramp intersection.
    // All four axes matter: a cell may touch a rotated ramp's bounding box while
    // missing the ramp itself. Tangencies have no area and require no height gate.
    if (std::fabs(delta.x) >= area.half.x + std::fabs(along.x) * halfLength + std::fabs(across.x) * ramp.halfWidth ||
        std::fabs(delta.y) >= area.half.y + std::fabs(along.y) * halfLength + std::fabs(across.y) * ramp.halfWidth) return false;
    const float alongRadius = std::fabs(along.x) * area.half.x + std::fabs(along.y) * area.half.y;
    const float acrossRadius = std::fabs(across.x) * area.half.x + std::fabs(across.y) * area.half.y;
    return std::fabs(delta.x * along.x + delta.y * along.y) < halfLength + alongRadius &&
        std::fabs(delta.x * across.x + delta.y * across.y) < ramp.halfWidth + acrossRadius;
}
bool rampCoversArea(const MapRamp& ramp, const NavBox& area) {
    // Both footprints are convex. All four AABB corners inside this one ramp
    // prove full coverage without sampling away a narrow upper-ground sliver.
    for (float x : {-1.0f, 1.0f}) for (float y : {-1.0f, 1.0f})
        if (!rampContains(ramp, {area.center.x + x * area.half.x, area.center.y + y * area.half.y})) return false;
    return true;
}
float segmentDistance(Vec2 point, Vec2 a, Vec2 b) {
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float squared = dx * dx + dy * dy;
    const float alpha = squared > 0 ? std::clamp(((point.x - a.x) * dx + (point.y - a.y) * dy) / squared, 0.0f, 1.0f) : 0;
    return std::hypot(point.x - a.x - alpha * dx, point.y - a.y - alpha * dy);
}
Vec2 startOffset(int team, Vec2 local) {
    switch (team) {
        case 1: return {-local.x, -local.y};
        case 2: return {-local.y, local.x};
        case 3: return {local.y, -local.x};
        default: return local;
    }
}
void route(MapDefinition& map, MapRouteKind kind, float halfWidth, std::initializer_list<Vec2> points) {
    map.routes.push_back({kind, halfWidth, points});
}
void addObstacle(MapDefinition& map, NavBox box, MapObstacleKind kind = MapObstacleKind::RockMass) {
    map.obstacles.push_back(box);
    map.obstacleKinds.push_back(kind);
}

void legacyRoutes(MapDefinition& map) {
    // Former Landscape pathway guides, preserved exactly in standard-map coordinates.
    const auto main = MapRouteKind::Main, flank = MapRouteKind::Main, economy = MapRouteKind::Main;
    if (map.playerCount == 2) {
        if (map.map == 0) {
            route(map, main, 160, {{600,600},{1500,1450},{2400,2400},{3300,3350},{4200,4200}});
            route(map, flank, 160, {{600,900},{860,1800},{820,2400},{1250,3500},{2000,3950},{2900,4100}});
            route(map, flank, 160, {{4200,3900},{3940,3000},{3980,2400},{3600,1500},{2900,1150}});
            route(map, economy, 160, {{1900,1000},{2050,1700},{1950,2250},{1750,2900},{1200,3400}});
        } else if (map.map == 1) {
            route(map, main, 160, {{600,600},{2000,950},{3400,1750},{3900,3100},{4200,4200}});
            route(map, flank, 160, {{600,600},{880,1950},{1650,3050},{3000,3720},{4200,4200}});
            route(map, economy, 160, {{600,1100},{1600,1050},{2500,1550},{3400,1700},{4200,1900}});
            route(map, economy, 160, {{600,3150},{1600,3200},{2600,3300},{3100,3800},{4200,3900}});
        } else {
            route(map, main, 160, {{600,600},{1750,1200},{2380,2000},{2550,2950},{4200,4200}});
            route(map, flank, 160, {{600,900},{830,1900},{1150,2900},{1400,3700},{1900,4150}});
            route(map, flank, 160, {{4200,3900},{3970,2900},{3650,1900},{3400,1100},{2900,650}});
            route(map, economy, 160, {{750,2550},{1700,2450},{2400,2450},{2600,2950},{3900,3050}});
        }
    } else if (map.map == 0) {
        route(map, main, 160, {{600,600},{1500,1500},{2400,2400},{3300,3300},{4200,4200}});
        route(map, main, 160, {{4200,600},{3300,1500},{2400,2400},{1500,3300},{600,4200}});
        route(map, flank, 160, {{600,600},{2400,820},{4200,600}});
        route(map, flank, 160, {{600,4200},{2400,3980},{4200,4200}});
        route(map, flank, 160, {{600,600},{780,2400},{600,4200}});
        route(map, flank, 160, {{4200,600},{4020,2400},{4200,4200}});
    } else if (map.map == 1) {
        route(map, flank, 160, {{600,600},{950,1600},{950,2400},{1000,3400},{600,4200}});
        route(map, flank, 160, {{4200,600},{3900,1600},{3900,2400},{3850,3400},{4200,4200}});
        route(map, flank, 160, {{600,600},{1600,900},{2400,950},{3200,900},{4200,600}});
        route(map, flank, 160, {{600,4200},{1600,3900},{2400,3950},{3200,3900},{4200,4200}});
    } else {
        route(map, main, 160, {{600,600},{1700,950},{2400,2400},{3100,3850},{4200,4200}});
        route(map, main, 160, {{600,4200},{950,3100},{2400,2400},{3850,1700},{4200,600}});
        route(map, economy, 160, {{2400,1800},{3000,2400},{2400,3000},{1800,2400},{2400,1800}});
    }
    const auto drainage = MapRouteKind::Drainage;
    if (map.map == 0) {
        route(map, drainage, 60, {{200,1500},{1400,1750},{2450,1430},{3500,1760},{4600,1480}});
        route(map, drainage, 60, {{1150,4600},{1480,3400},{1820,2300},{2120,1200},{2300,150}});
        route(map, drainage, 60, {{4600,3320},{3400,3100},{2300,3420},{1200,3080},{150,3400}});
    } else if (map.map == 1) {
        route(map, drainage, 60, {{150,2650},{1250,2900},{2350,2650},{3450,2950},{4650,2700}});
        route(map, drainage, 60, {{900,150},{1150,1300},{1450,2450},{1250,3600},{1500,4650}});
        route(map, drainage, 60, {{4650,900},{3550,1250},{3150,2400},{3450,3500},{3200,4650}});
        route(map, drainage, 60, {{150,4100},{1300,3900},{2500,4150},{3700,3900},{4650,4150}});
    } else {
        route(map, drainage, 60, {{150,1250},{1300,1500},{2400,1200},{3500,1520},{4650,1250}});
        route(map, drainage, 60, {{150,3550},{1300,3300},{2400,3600},{3500,3280},{4650,3550}});
        route(map, drainage, 60, {{2050,150},{1850,1300},{2150,2400},{1900,3500},{2100,4650}});
        route(map, drainage, 60, {{3050,4650},{3300,3500},{3000,2400},{3250,1300},{3050,150}});
    }
}

MapDefinition legacyMap(int mapIndex, int players, MatchLength length, int revision) {
    MapDefinition map;
    map.map = mapIndex; map.playerCount = players; map.matchLength = length; map.revision = revision;
    const auto profile = matchLengthProfile(length);
    map.worldSize = profile.worldSize;
    if (players == 2) {
        if (mapIndex == 0) {
            map.obstacles = {{{2400,1570},{220,550}},{{2400,3230},{220,550}},{{1320,2400},{380,140}},{{3480,2400},{380,140}}};
        } else if (mapIndex == 1) {
            map.obstacles = {{{2400,2400},{630,500}},{{1450,1550},{180,330}},{{3350,3250},{180,330}},{{1400,3520},{420,120}},{{3400,1280},{420,120}}};
        } else {
            map.obstacles = {{{2400,1000},{150,600}},{{2400,3800},{150,600}},{{1600,2100},{600,120}},{{3200,2700},{600,120}},{{850,3300},{180,350}},{{3950,1500},{180,350}}};
        }
    } else if (mapIndex == 0) {
        map.obstacles = {{{2400,1500},{180,420}},{{2400,3300},{180,420}},{{1500,2400},{420,180}},{{3300,2400},{420,180}}};
    } else if (mapIndex == 1) {
        map.obstacles = {{{2400,2400},{520,520}},{{1500,1500},{150,300}},{{3300,1500},{300,150}},{{3300,3300},{150,300}},{{1500,3300},{300,150}}};
    } else {
        map.obstacles = {{{2400,1100},{130,500}},{{3700,2400},{500,130}},{{2400,3700},{130,500}},{{1100,2400},{500,130}},{{1550,1550},{120,280}},{{3250,1550},{280,120}},{{3250,3250},{120,280}},{{1550,3250},{280,120}}};
    }
    map.obstacleKinds.assign(map.obstacles.size(), MapObstacleKind::RockMass);
    const float scale = map.worldSize / 4800;
    for (auto& box : map.obstacles) { box.center = multiply(box.center, scale); box.half = multiply(box.half, scale); }
    for (int team = 0; team < players; ++team) {
        const bool right = team == 1 || team == 2, top = team == 1 || team == 3;
        const Vec2 base = multiply({right ? 4200.0f : 600.0f, top ? 4200.0f : 600.0f}, scale);
        map.starts.push_back(base);
        MapResourceSite site{MapSiteRole::Home, team, base, {}, profile.homeNodeOre};
        for (const Vec2 offset : {Vec2{-300,170}, Vec2{-230,290}, Vec2{-100,370}, Vec2{65,400}})
            site.nodes.push_back(add(base, startOffset(team, multiply(offset, scale))));
        map.sites.push_back(site);
    }
    if (players == 2) {
        const std::array<Vec2,4> clusters{{{1900,1000},{2900,3800},{1000,2800},{3800,2000}}};
        for (std::size_t index = 0; index < clusters.size(); ++index) {
            const float sign = index % 2 == 0 ? 1.0f : -1.0f;
            MapResourceSite site{index < 2 ? MapSiteRole::Natural : MapSiteRole::Third,
                static_cast<int>(index % 2), multiply(clusters[index], scale), {}, profile.expansionNodeOre};
            for (int i = 0; i < 3; ++i) {
                Vec2 node = add(site.center, multiply({sign * static_cast<float>(i - 1) * 120, sign * static_cast<float>((i % 2) * 90)}, scale));
                // Preserve the original radius-sensitive nudge after match-size scaling.
                bool blocked = false;
                for (const auto& box : map.obstacles) blocked = blocked || intersects(box, node, 45);
                if (blocked) node = add(node, multiply({0,sign * 250}, scale));
                site.nodes.push_back(node);
            }
            map.sites.push_back(site);
        }
    } else {
        struct Cluster { Vec2 center, tangent, inward; };
        const std::array<Cluster,4> clusters{{
            {{1500,1000},{1,0},{1,1}},{{3800,1500},{0,1},{-1,1}},
            {{3300,3800},{-1,0},{-1,-1}},{{1000,3300},{0,-1},{1,-1}}
        }};
        const int owners[] = {0,2,1,3};
        for (std::size_t index = 0; index < clusters.size(); ++index) {
            const auto& cluster = clusters[index];
            MapResourceSite site{MapSiteRole::Natural, owners[index], multiply(cluster.center, scale), {}, profile.expansionNodeOre};
            for (int i = 0; i < 3; ++i) {
                const float side = static_cast<float>(i - 1) * 180, inset = i == 1 ? 60.0f : 0.0f;
                site.nodes.push_back(multiply({cluster.center.x + cluster.tangent.x * side + cluster.inward.x * inset,
                    cluster.center.y + cluster.tangent.y * side + cluster.inward.y * inset}, scale));
            }
            map.sites.push_back(site);
        }
    }
    legacyRoutes(map);
    for (auto& guide : map.routes) {
        guide.halfWidth *= scale;
        for (auto& point : guide.points) point = multiply(point, scale);
    }
    return map;
}

MapDefinition shatteredRift() {
    MapDefinition map;
    map.revision = 1;
    const auto profile = matchLengthProfile(MatchLength::Standard);
    map.starts = {{720,680},{4080,4120}};
    const std::vector<Vec2> home{{400,800},{455,955},{605,1040},{785,1075}};
    const std::vector<Vec2> natural{{1980,690},{2150,600},{2330,650}};
    const std::vector<Vec2> third{{600,2770},{550,2940},{610,3110}};
    auto site = [&](MapSiteRole role, int owner, Vec2 center, const std::vector<Vec2>& nodes, float ore) {
        MapResourceSite result{role, owner, owner ? mirror(center) : center, {}, ore};
        for (auto node : nodes) result.nodes.push_back(owner ? mirror(node) : node);
        map.sites.push_back(result);
    };
    for (int team = 0; team < 2; ++team) site(MapSiteRole::Home, team, map.starts[0], home, profile.homeNodeOre);
    for (int team = 0; team < 2; ++team) site(MapSiteRole::Natural, team, {2260,950}, natural, profile.expansionNodeOre);
    for (int team = 0; team < 2; ++team) site(MapSiteRole::Third, team, {860,2920}, third, profile.expansionNodeOre);

    // The outer 180 cm belt is explicitly impassable scenery space. Terrace walls
    // meet it, so the 380 cm ramp corridor is the only ground exit from each main.
    addObstacle(map, {{90,2400},{90,2400}}, MapObstacleKind::Perimeter);
    addObstacle(map, {{4710,2400},{90,2400}}, MapObstacleKind::Perimeter);
    addObstacle(map, {{2400,90},{2220,90}}, MapObstacleKind::Perimeter);
    addObstacle(map, {{2400,4710},{2220,90}}, MapObstacleKind::Perimeter);
    const std::array<NavBox,5> walls{{
        {{925,1610},{745,60}},   // Entire north edge.
        {{1610,610},{60,430}},  // East edge below the entrance.
        {{1610,1485},{60,65}},  // East edge above the entrance.
        {{1770,1020},{280,20}}, // Rails enclose the slope from its high endpoint.
        {{1770,1440},{280,20}}
    }};
    for (int team = 0; team < 2; ++team) {
        map.plateaus.push_back({{team ? mirror({805,805}) : Vec2{805,805},{805,805}},180,1});
        map.ramps.push_back({team ? mirror({1490,1230}) : Vec2{1490,1230},
            team ? mirror({2050,1230}) : Vec2{2050,1230},190,180,0,1,0});
        for (auto wall : walls) { if (team) wall.center = mirror(wall.center); addObstacle(map, wall, MapObstacleKind::RetainingWall); }
        const Vec2 pad = team ? mirror({900,900}) : Vec2{900,900};
        map.surfaces.push_back({{pad,{610,610}},MapSurface::Paving});
        map.surfaces.push_back({{team ? mirror({2260,950}) : Vec2{2260,950},{430,390}},MapSurface::OreApron});
        map.surfaces.push_back({{team ? mirror({860,2920}) : Vec2{860,2920},{390,440}},MapSurface::OreApron});
        map.surfaces.push_back({{team ? mirror({2260,950}) : Vec2{2260,950},{150,150}},MapSurface::Paving});
    }
    addObstacle(map, {{1590,2125},{310,75}});
    addObstacle(map, {{3210,2675},{310,75}});
    // Back the expansion ore crescents with localized geology. Their open faces
    // remain toward the economy pads and flank roads, leaving every deposit usable.
    for (int team = 0; team < 2; ++team) {
        addObstacle(map, {team ? mirror({2260,350}) : Vec2{2260,350},{360,85}});
        addObstacle(map, {team ? mirror({330,2920}) : Vec2{330,2920},{90,410}});
    }
    route(map, MapRouteKind::Main, 130, {{720,680},{1280,960},{1490,1230},{2050,1230},{2220,1230},
        {2290,1730},{2400,2400},{2510,3070},{2580,3570},{2750,3570},{3310,3570},{3520,3840},{4080,4120}});
    route(map, MapRouteKind::Flank, 110, {{2050,1230},{2220,1230},{2220,1780},{1040,1830},{660,2300},
        {860,2920},{1280,3620},{2230,3570},{2750,3570}});
    MapRoute otherFlank = map.routes.back();
    for (auto& point : otherFlank.points) point = mirror(point);
    map.routes.push_back(otherFlank);
    route(map, MapRouteKind::Economy, 90, {{2050,1230},{2220,1230},{2260,950}});
    route(map, MapRouteKind::Economy, 90, {{2750,3570},{2580,3570},{2540,3850}});
    return map;
}

} // namespace

float MapDefinition::terrainHeight(Vec2 point) const {
    if (!finite(point)) return 0;
    for (const auto& ramp : ramps) if (rampContains(ramp, point)) {
        float along = 0, across = 0, length = 0;
        rampCoordinates(ramp, point, along, across, length);
        return ramp.highHeight + (ramp.lowHeight - ramp.highHeight) * std::clamp(along / length, 0.0f, 1.0f);
    }
    for (const auto& plateau : plateaus) if (contains(plateau.bounds, point)) return plateau.height;
    return 0;
}

float MapDefinition::terrainVisibilityHeight(const NavBox& area) const {
    if (!finite(area.center) || !finite(area.half) || area.half.x < 0 || area.half.y < 0)
        return std::numeric_limits<float>::infinity();
    if (area.half.x == 0 || area.half.y == 0) return 0;
    float required = 0;
    for (const auto& ramp : ramps)
        if (rampIntersectsArea(ramp, area)) required = std::max(required, ramp.lowHeight);
    for (const auto& plateau : plateaus) {
        NavBox intersection;
        if (!positiveIntersection(plateau.bounds, area, intersection)) continue;
        bool coveredByRamp = false;
        for (const auto& ramp : ramps) if (rampCoversArea(ramp, intersection)) {
            coveredByRamp = true;
            break;
        }
        if (!coveredByRamp) required = std::max(required, plateau.height);
    }
    return required;
}

int MapDefinition::terrainLevel(Vec2 point) const {
    if (!finite(point)) return 0;
    for (const auto& ramp : ramps) if (rampContains(ramp, point)) {
        float along = 0, across = 0, length = 0;
        rampCoordinates(ramp, point, along, across, length);
        return along < length * 0.5f ? ramp.highLevel : ramp.lowLevel;
    }
    for (const auto& plateau : plateaus) if (contains(plateau.bounds, point)) return plateau.level;
    return 0;
}

bool MapDefinition::onRamp(Vec2 point, float radius) const {
    if (!finite(point) || !std::isfinite(radius) || radius < 0) return false;
    for (const auto& ramp : ramps) if (rampContains(ramp, point, radius)) return true;
    return false;
}

bool MapDefinition::buildable(Vec2 point, float radius) const {
    if (!finite(point) || !std::isfinite(radius) || radius < 0 ||
        point.x < radius || point.y < radius || point.x > worldSize - radius || point.y > worldSize - radius) return false;
    for (const auto& obstacle : obstacles) if (intersects(obstacle, point, radius)) return false;
    if (onRamp(point, radius)) return false;
    for (const auto& plateau : plateaus) {
        const bool centerInside = contains(plateau.bounds, point);
        if (centerInside) {
            if (std::fabs(point.x - plateau.bounds.center.x) + radius > plateau.bounds.half.x ||
                std::fabs(point.y - plateau.bounds.center.y) + radius > plateau.bounds.half.y) return false;
        } else if (intersects(plateau.bounds, point, radius)) return false;
    }
    return true;
}

MapSurface MapDefinition::surfaceAt(Vec2 point) const {
    if (!finite(point)) return MapSurface::Soil;
    if (onRamp(point)) return MapSurface::Paving;
    for (auto region = surfaces.rbegin(); region != surfaces.rend(); ++region)
        if (contains(region->bounds, point)) return region->surface;
    for (const auto& guide : routes) {
        if (guide.kind == MapRouteKind::Drainage) continue;
        for (std::size_t i = 1; i < guide.points.size(); ++i)
            if (segmentDistance(point, guide.points[i - 1], guide.points[i]) <= guide.halfWidth) return MapSurface::PackedRoad;
    }
    return MapSurface::Soil;
}

const MapDefinition& mapDefinition(int map, int playerCount, MatchLength length, int revision) {
    static const std::array<MapDefinition,36> definitions = [] {
        std::array<MapDefinition,36> result;
        for (int rev = 0; rev < 2; ++rev) for (int group = 0; group < 2; ++group)
            for (int index = 0; index < 3; ++index) for (int duration = 0; duration < 3; ++duration) {
                auto& definition = result[rev * 18 + group * 9 + index * 3 + duration];
                definition = rev == 1 && group == 0 && index == 0 && duration == static_cast<int>(MatchLength::Standard)
                    ? shatteredRift() : legacyMap(index, group == 0 ? 2 : 4, static_cast<MatchLength>(duration), rev);
            }
        return result;
    }();
    map = std::clamp(map, 0, 2);
    const int group = playerCount == 4 ? 1 : 0;
    const int duration = static_cast<int>(matchLengthAt(static_cast<int>(length)));
    revision = validMapRevision(revision) ? revision : 0;
    return definitions[revision * 18 + group * 9 + map * 3 + duration];
}

} // namespace cinder
