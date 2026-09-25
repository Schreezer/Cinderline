#include "Sim/MapDefinition.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using namespace cinder;

namespace {

void check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
bool near(float a, float b, float tolerance = 0.001f) { return std::fabs(a - b) < tolerance; }
bool same(Vec2 a, Vec2 b) { return near(a.x, b.x) && near(a.y, b.y); }
Vec2 mirrored(Vec2 p, float world = 4800) { return {world - p.x, world - p.y}; }
float distance(Vec2 a, Vec2 b) { return std::hypot(a.x - b.x, a.y - b.y); }
bool sameBox(const NavBox& a, const NavBox& b) { return same(a.center, b.center) && same(a.half, b.half); }

void legacyMatrix() {
    check(validMapRevision(0) && validMapRevision(1) && !validMapRevision(-1) && !validMapRevision(2),
        "only explicitly supported map revisions are accepted");
    const int counts[2][3] = {{4,5,6},{4,5,8}};
    const int pathCounts[2][3] = {{4,4,4},{6,4,3}};
    for (int group = 0; group < 2; ++group) for (int index = 0; index < 3; ++index)
        for (int length = 0; length < 3; ++length) {
            const int players = group == 0 ? 2 : 4;
            const auto preset = static_cast<MatchLength>(length);
            const auto profile = matchLengthProfile(preset);
            const auto& old = mapDefinition(index, players, preset, 0);
            const auto& current = mapDefinition(index, players, preset, 1);
            check(&old == &mapDefinition(index, players, preset, 0), "map lookup has stable immutable storage");
            check(!old.authored() && old.ramps.empty() && old.plateaus.empty(), "legacy revision stays flat");
            check(old.obstacles.size() == static_cast<std::size_t>(counts[group][index]), "legacy barrier count preserved");
            check(old.obstacleKinds.size() == old.obstacles.size(), "legacy barrier classifications parallel geometry");
            check(old.starts.size() == static_cast<std::size_t>(players), "one legacy start per player");
            check(old.sites.size() == static_cast<std::size_t>(players + 4), "legacy economy site count preserved");
            check(same(old.starts[0], {600 * profile.worldSize / 4800,600 * profile.worldSize / 4800}), "legacy southwest start preserved");
            check(same(old.starts[1], mirrored(old.starts[0], old.worldSize)), "legacy opposed start preserved");
            float ore = 0;
            for (std::size_t i = 0; i < old.sites.size(); ++i) {
                const auto& site = old.sites[i];
                const bool home = i < static_cast<std::size_t>(players);
                check(site.nodes.size() == (home ? 4u : 3u), "legacy home/expansion deposit counts preserved");
                check(site.nodeOre == (home ? profile.homeNodeOre : profile.expansionNodeOre), "legacy per-node reserves preserved");
                ore += site.nodeOre * static_cast<float>(site.nodes.size());
            }
            check(ore == players * 4 * profile.homeNodeOre + 12 * profile.expansionNodeOre, "legacy ore total preserved");
            const auto drainage = std::count_if(old.routes.begin(), old.routes.end(), [](const MapRoute& r) { return r.kind == MapRouteKind::Drainage; });
            check(drainage == (index == 0 ? 3 : 4), "legacy drainage guide count preserved");
            check(old.routes.size() - drainage == static_cast<std::size_t>(pathCounts[group][index]), "legacy pathway guide count preserved");
            for (const auto& guide : old.routes)
                check(near(guide.halfWidth, (guide.kind == MapRouteKind::Drainage ? 60 : 160) * profile.worldSize / 4800), "legacy route widths preserved");
            if (index == 0 && players == 2 && preset == MatchLength::Standard) continue;
            check(!current.authored() && current.revision == 1, "unmigrated variants retain flat geometry with explicit revision");
            check(current.obstacles.size() == old.obstacles.size() && current.sites.size() == old.sites.size() &&
                current.routes.size() == old.routes.size(), "unmigrated revision has exact collection sizes");
            for (std::size_t i = 0; i < old.obstacles.size(); ++i) check(sameBox(current.obstacles[i], old.obstacles[i]), "unmigrated obstacle coordinates/order unchanged");
            for (std::size_t i = 0; i < old.sites.size(); ++i) {
                check(same(current.sites[i].center, old.sites[i].center), "unmigrated economy centers unchanged");
                for (std::size_t n = 0; n < old.sites[i].nodes.size(); ++n)
                    check(same(current.sites[i].nodes[n], old.sites[i].nodes[n]), "unmigrated deposit coordinates/order unchanged");
            }
        }
    const auto& old = mapDefinition(0,2,MatchLength::Standard,0);
    check(sameBox(old.obstacles[0], {{2400,1570},{220,550}}), "legacy Shattered Rift first barrier is frozen");
    check(same(old.sites[0].nodes[0], {300,770}) && same(old.sites[1].nodes[3], {4135,3800}), "legacy home deposit arcs are frozen");
    check(same(old.sites[2].nodes[0], {1780,1000}) && same(old.sites[3].nodes[0], {3020,3800}), "legacy mirrored expansion arcs are frozen");
}

void symmetryAndEconomy() {
    const auto& map = mapDefinition(0,2,MatchLength::Standard);
    check(map.authored() && map.revision == 1 && map.worldSize == 4800, "fresh Standard duel uses authored Shattered Rift");
    check(map.sites.size() == 6 && map.plateaus.size() == 2 && map.ramps.size() == 2, "two mains/naturals/thirds and two terraces exist");
    check(map.obstacles.size() == 20 && map.obstacleKinds.size() == map.obstacles.size(), "all geometry has presentation classification");
    Navigation navigation; navigation.sync(map.worldSize, map.obstacles, {});
    float ore = 0;
    for (std::size_t i = 0; i < map.sites.size(); i += 2) {
        const auto& first = map.sites[i]; const auto& second = map.sites[i + 1];
        check(first.owner == 0 && second.owner == 1 && first.role == second.role, "economy pairs have equivalent ownership/role");
        check(same(second.center, mirrored(first.center)), "economy pads rotate exactly");
        check(map.buildable(first.center,125) && map.buildable(second.center,125), "each economy pad fits the largest building footprint");
        check(first.nodes.size() == (first.role == MapSiteRole::Home ? 4u : 3u), "home four-node and expansion three-node budget retained");
        for (std::size_t node = 0; node < first.nodes.size(); ++node) {
            check(same(second.nodes[node],mirrored(first.nodes[node])), "ore crescent node order rotates exactly");
            check(navigation.pointClear(first.nodes[node],45) && navigation.pointClear(second.nodes[node],45), "ore deposits clear terrain by their full radius");
            check(distance(first.nodes[node],first.center) > 125 + 45, "ore leaves an unoccupied headquarters pad");
        }
        ore += 2 * first.nodeOre * static_cast<float>(first.nodes.size());
    }
    check(ore == 70400, "authored map retains 70400 total Standard ore");
    for (std::size_t i = 0; i < map.obstacles.size(); ++i) {
        auto counterpart = map.obstacles[i]; counterpart.center = mirrored(counterpart.center);
        bool matched = false;
        for (std::size_t j = 0; j < map.obstacles.size(); ++j)
            matched = matched || (sameBox(counterpart,map.obstacles[j]) && map.obstacleKinds[i] == map.obstacleKinds[j]);
        check(matched, "every impassable box and role has a rotational counterpart");
    }
    for (float y = 211; y < 4600; y += 107) for (float x = 213; x < 4600; x += 109) {
        const Vec2 point{x,y}, partner = mirrored(point);
        check(near(map.terrainHeight(point),map.terrainHeight(partner)), "terrain height rotates without advantage");
        check(map.buildable(point,104) == map.buildable(partner,104), "largest production footprint has symmetric terrain access");
        check(map.surfaceAt(point) == map.surfaceAt(partner), "ground surface hierarchy rotates exactly");
    }
}

void rampsAndFootprints() {
    const auto& map = mapDefinition(0,2,MatchLength::Standard);
    for (const auto& ramp : map.ramps) {
        const float length = distance(ramp.high,ramp.low);
        const Vec2 direction{(ramp.low.x-ramp.high.x)/length,(ramp.low.y-ramp.high.y)/length};
        check(ramp.halfWidth * 2 == 380, "main entrance has explicit 380 cm width independent of unit scale");
        check(near(map.terrainHeight(ramp.high),180) && near(map.terrainHeight(ramp.low),0), "ramp endpoints meet terrace and basin");
        for (int i = 0; i <= 20; ++i) {
            const float t = static_cast<float>(i) / 20;
            const Vec2 point{ramp.high.x+(ramp.low.x-ramp.high.x)*t,ramp.high.y+(ramp.low.y-ramp.high.y)*t};
            check(near(map.terrainHeight(point),180*(1-t)), "ramp height is a continuous linear slope");
            check(!map.buildable(point,48), "even smallest building is rejected anywhere on the ramp");
        }
        check(near(map.terrainHeight({ramp.high.x-direction.x,ramp.high.y-direction.y}),180), "uphill end joins flat high ground");
        check(near(map.terrainHeight({ramp.low.x+direction.x,ramp.low.y+direction.y}),0), "downhill end joins flat low ground");
        check(map.onRamp({ramp.low.x+direction.x*100,ramp.low.y+direction.y*100},125), "building overlapping only the ramp end is detected");
        check(!map.buildable({ramp.low.x+direction.x*100,ramp.low.y+direction.y*100},125), "building center outside ramp cannot overlap its slope");
    }
    check(map.surfaceAt(map.starts[0]) == MapSurface::Paving, "main headquarters is grounded on paving");
    for (const auto& site : map.sites) {
        if (site.role == MapSiteRole::Natural) {
            check(map.surfaceAt(site.nodes[1]) == MapSurface::OreApron, "natural ore stays on its distinct apron");
            for (int spoke=0;spoke<24;++spoke) {
                const float angle=static_cast<float>(spoke)*6.28318530718f/24;
                check(map.surfaceAt({site.center.x+std::cos(angle)*125,site.center.y+std::sin(angle)*125}) == MapSurface::Paving,
                    "the full natural headquarters footprint fits its paved center pad");
            }
        } else if (site.role == MapSiteRole::Third)
            check(map.surfaceAt(site.center) == MapSurface::OreApron, "exposed third retains an open ore apron");
    }
    check(map.surfaceAt({2400,2400}) == MapSurface::PackedRoad, "central route has compacted surface");
    check(!map.buildable({1600,900},125) && !map.buildable({100,2600},125), "cliff seam and peripheral scenery reject placement");
    check(!map.buildable({std::numeric_limits<float>::quiet_NaN(),0},50) && !map.buildable({2400,2400},-1), "invalid placement query fails closed");
    check(near(map.terrainHeight({std::numeric_limits<float>::infinity(),0}),0), "invalid height query returns safe flat fallback");
}

void visibilityHeight() {
    const auto& map = mapDefinition(0,2,MatchLength::Standard);
    struct Probe { NavBox area; float expected; const char* description; };
    const std::array<Probe,9> probes{{
        {{{720,680},{37.5f,37.5f}},180,"whole upper plateau"},
        {{{2400,2400},{37.5f,37.5f}},0,"whole central lowland"},
        {{{1770,1230},{37.5f,37.5f}},0,"whole sloped ramp"},
        {{{2062.5f,1237.5f},{37.5f,37.5f}},0,"mixed ramp bottom and lowland"},
        {{{1537.5f,1237.5f},{37.5f,37.5f}},0,"upper ramp wholly replacing plateau"},
        {{{1462.5f,1237.5f},{37.5f,37.5f}},180,"crest cell retaining upper plateau"},
        {{{1537.5f,1012.5f},{37.5f,37.5f}},180,"ramp side cell retaining upper plateau"},
        {{{1647.49f,1647.49f},{37.5f,37.5f}},180,"tiny upper corner missed by nine point sampling"},
        {{{1527.49f,1237.5f},{37.5f,37.5f}},180,"tiny upper strip before ramp high endpoint"}
    }};
    for (const auto& probe : probes) for (int rotation=0;rotation<2;++rotation) {
        NavBox area = probe.area;
        if (rotation) area.center = mirrored(area.center);
        check(near(map.terrainVisibilityHeight(area),probe.expected),
            std::string(probe.description) + " has the required visibility gate in both rotations");
    }
    check(map.terrainHeight({1537.5f,1237.5f})>160 &&
        map.terrainVisibilityHeight({{1537.5f,1237.5f},{37.5f,37.5f}})==0,
        "seeing an upper ramp from below does not change its actual traversal height");
    check(map.terrainVisibilityHeight({{1647.5f,600},{37.5f,37.5f}})==0,
        "touching a plateau boundary without positive upper area does not hide adjacent lowland");
    check(std::isinf(map.terrainVisibilityHeight({{0,0},{-1,1}})) &&
        std::isinf(map.terrainVisibilityHeight({{std::numeric_limits<float>::quiet_NaN(),0},{1,1}})),
        "invalid visibility areas fail closed");
    check(map.terrainVisibilityHeight({map.starts[0],{0,10}})==0,"an empty visibility area has no height gate");
    for (int players : {2,4}) for (int index=0;index<3;++index) for (int length=0;length<3;++length) {
        const auto& legacy=mapDefinition(index,players,static_cast<MatchLength>(length),0);
        check(legacy.terrainVisibilityHeight({{legacy.worldSize*.5f,legacy.worldSize*.5f},{legacy.worldSize*.5f,legacy.worldSize*.5f}})==0,
            "every legacy map preserves height-independent vision");
    }

    // Exercise the oriented-footprint math beyond the current horizontal ramps.
    // The slope crosses empty space at height 80+, so its lower-level gate must
    // survive even where no plateau intersects the queried cell.
    MapDefinition elevated;
    elevated.ramps.push_back({{200,200},{400,400},20,180,80,2,1});
    check(elevated.terrainVisibilityHeight({{300,300},{5,5}})==80,
        "an elevated diagonal ramp retains its lower-height gate outside plateau bounds");
    check(elevated.terrainVisibilityHeight({{190,410},{2,2}})==0,
        "overlap with only a rotated ramp bounding box does not impose a false gate");
    check(elevated.terrainVisibilityHeight({{414.1421356f,385.8578644f},{2,2}})==80,
        "positive overlap near a diagonal ramp corner still inherits the lower height");
    elevated.plateaus.push_back({{{210,210},{20,20}},180,2});
    check(elevated.terrainVisibilityHeight({{215,215},{3,3}})==80,
        "four-corner containment subtracts a diagonal ramp from upper plateau coverage");
    check(elevated.terrainVisibilityHeight({{225,195},{3,3}})==180,
        "an upper sliver beside a diagonal ramp retains the upper gate");

    MapDefinition split;
    split.plateaus.push_back({{{100,100},{50,50}},180,1});
    split.ramps.push_back({{50,75},{150,75},25,180,0,1,0});
    split.ramps.push_back({{50,125},{150,125},25,180,0,1,0});
    check(split.terrainVisibilityHeight({{100,100},{20,20}})==180,
        "future cells covered jointly by different ramps conservatively retain upper privacy");
}

void connectivityAndEntrances() {
    const auto& map = mapDefinition(0,2,MatchLength::Standard);
    Navigation navigation; navigation.sync(map.worldSize,map.obstacles,{});
    constexpr float LargestGroundRadius = 34;
    for (const auto& site : map.sites) {
        const auto forward = navigation.route(map.starts[0],{site.center},LargestGroundRadius);
        const auto reverse = navigation.route(map.starts[1],{mirrored(site.center)},LargestGroundRadius);
        check(forward.reached && reverse.reached, "largest ground unit can reach every economy site from both rotational starts");
        check(near(forward.cost,reverse.cost,0.1f), "opposed sites have equal navigation distance");
    }
    for (const auto& ramp : map.ramps) {
        check(navigation.segmentClear(ramp.high,ramp.low,LargestGroundRadius), "largest ground unit traverses ramp centerline");
        const Vec2 midpoint{(ramp.high.x+ramp.low.x)/2,(ramp.high.y+ramp.low.y)/2};
        check(!navigation.segmentClear(midpoint,{midpoint.x,midpoint.y+300},16) &&
            !navigation.segmentClear(midpoint,{midpoint.x,midpoint.y-300},16), "ramp rails prevent lateral cliff exits");
    }
    check(!navigation.segmentClear({1000,1400},{1000,1900},16) &&
        !navigation.segmentClear({1400,700},{1900,700},16), "main cannot bypass its ramp across retaining walls");
    check(!navigation.segmentClear({300,1400},{160,1800},16), "main cannot escape around the wall at the world border");
    auto sealed = map.obstacles;
    for (const auto& ramp : map.ramps)
        sealed.push_back({{(ramp.high.x+ramp.low.x)/2,(ramp.high.y+ramp.low.y)/2},{20,ramp.halfWidth+20}});
    navigation.sync(map.worldSize,sealed,{});
    check(!navigation.route(map.starts[0],{{2400,2400}},16).reached &&
        !navigation.route(map.starts[1],{{2400,2400}},16).reached, "sealing each ramp disconnects its main; no unintended second exit exists");

    navigation.sync(map.worldSize,map.obstacles,{});
    for (const auto& guide : map.routes) for (std::size_t i = 1; i < guide.points.size(); ++i)
        check(navigation.segmentClear(guide.points[i-1],guide.points[i],LargestGroundRadius),
            "road segment must clear terrain: " + std::to_string(guide.points[i-1].x) + "," +
            std::to_string(guide.points[i-1].y) + " to " + std::to_string(guide.points[i].x) + "," + std::to_string(guide.points[i].y));
    const auto flankCount = std::count_if(map.routes.begin(),map.routes.end(),[](const MapRoute& guide){return guide.kind==MapRouteKind::Flank;});
    check(flankCount == 2, "two explicit alternate flank guides reconnect the ramp approaches");

    std::vector<NavCircle> occupied;
    Id id = 1;
    for (const auto& start : map.starts) occupied.push_back({id++,start,125});
    for (const auto& site : map.sites) for (const auto& node : site.nodes) occupied.push_back({id++,node,45});
    navigation.sync(map.worldSize,map.obstacles,occupied);
    for (const auto& site : map.sites) {
        const int team=site.owner;
        const Vec2 worker{map.starts[team].x+(team?-155:155),map.starts[team].y+(team?-55:55)};
        const Vec2 dropoff{site.center.x+(team?-155:155),site.center.y+(team?-55:55)};
        for (const auto& node : site.nodes) {
            std::vector<Vec2> approaches;
            for (int spoke=0;spoke<16;++spoke) {
                const float angle=static_cast<float>(spoke)*6.28318530718f/16;
                approaches.push_back({node.x+std::cos(angle)*70,node.y+std::sin(angle)*70});
            }
            const auto outward=navigation.route(worker,approaches,16);
            check(outward.reached,"worker reaches every home and expansion deposit despite pocket cliffs and real HQ/ore footprints");
            const Vec2 chosen=outward.points.empty()?worker:outward.points.back();
            check(navigation.route(chosen,{dropoff},16).reached,"every deposit has a clear return route to its local delivery pad");
        }
    }

    // A completed expansion processor must not close the approach between its
    // ore crescent and new back cliff. Test each site with its real footprint.
    for (const auto& site : map.sites) if (site.role != MapSiteRole::Home) {
        auto expanded=occupied;
        expanded.push_back({id,site.center,76});
        navigation.sync(map.worldSize,map.obstacles,expanded);
        const float sign=site.owner?-1.0f:1.0f;
        const Vec2 entry{site.center.x+sign*180,site.center.y+sign*180};
        for (const auto& node : site.nodes) {
            std::vector<Vec2> oreApproaches,deliveryApproaches;
            for (int spoke=0;spoke<24;++spoke) {
                const float angle=static_cast<float>(spoke)*6.28318530718f/24;
                oreApproaches.push_back({node.x+std::cos(angle)*70,node.y+std::sin(angle)*70});
                deliveryApproaches.push_back({site.center.x+std::cos(angle)*101,site.center.y+std::sin(angle)*101});
            }
            const auto harvest=navigation.route(entry,oreApproaches,16);
            check(harvest.reached,"a completed expansion processor leaves all three ore deposits reachable");
            const Vec2 chosen=harvest.points.empty()?entry:harvest.points.back();
            check(navigation.route(chosen,deliveryApproaches,16).reached,"ore workers can return to a completed processor without crossing back cliffs");
        }
    }
}

} // namespace

int main() {
    try {
        legacyMatrix(); symmetryAndEconomy(); rampsAndFootprints(); visibilityHeight(); connectivityAndEntrances();
        std::cout << "Map definitions: legacy variants, economy symmetry, ramps, visibility, footprints and navigation passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Map definition failure: " << error.what() << '\n';
        return 1;
    }
}
