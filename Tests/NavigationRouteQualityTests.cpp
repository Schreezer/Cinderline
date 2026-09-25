#include "Sim/Navigation.h"

#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace cinder;

namespace {

void check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

const Vec2 Start{1200, 1200}, Goal{2000, 1200};
const std::vector<NavCircle> DistantBases{{1, {700, 700}, 125}, {2, {4400, 4400}, 125}};

void checkLocalPath(const Navigation& navigation, const NavigationResult& route,
    float clearance, float maximumCost) {
    check(route.reached && !route.exhausted, "local detour was not reached");
    check(route.cost <= maximumCost, "route approached a distant obstacle instead of the local detour: cost "
        + std::to_string(route.cost) + " exceeds " + std::to_string(maximumCost));
    Vec2 previous = route.origin;
    float measuredCost = 0;
    for (Vec2 point : route.points) {
        check(navigation.segmentClear(previous, point, clearance), "route contains a blocked segment");
        check(point.x >= 1100 && point.x <= 2100 && point.y >= 900 && point.y <= 1500,
            "route leaves the local obstacle neighborhood");
        measuredCost += std::hypot(point.x - previous.x, point.y - previous.y);
        previous = point;
    }
    check(previous.x == Goal.x && previous.y == Goal.y, "route misses its exact commanded goal");
    check(std::fabs(measuredCost - route.cost) < 0.001f, "reported path cost does not match its segments");
}

void largeUnitsUseConnectedBoxCorners() {
    Navigation navigation;
    navigation.sync(5100, {{{1600, 1200}, {100, 150}}}, DistantBases);
    for (float clearance : {16.0f, 28.0f, 30.0f, 34.0f, 50.0f}) {
        // An independently constructed route around the expanded rectangle
        // provides a conservative upper bound; the rounded route may be shorter.
        const float pad = clearance + 1;
        const float upperBound = 2 * std::hypot(300 - pad, 150 + pad) + 200 + 2 * pad;
        checkLocalPath(navigation, navigation.route(Start, {Goal}, clearance), clearance, upperBound);
    }
}

void circleDetoursStayNearTheirAnalyticShortestLength() {
    Navigation navigation;
    auto circles = DistantBases;
    circles.push_back({3, {1600, 1200}, 125});
    navigation.sync(5100, {}, circles);
    for (float clearance : {16.0f, 28.0f, 34.0f, 50.0f}) {
        // Two tangents plus the intervening arc give the continuous shortest
        // route around one circle. Permit 1% for the discrete routing overlay.
        const double radius = 125 + clearance;
        const double shortest = 2 * std::sqrt(400 * 400 - radius * radius)
            + radius * (std::acos(-1.0) - 2 * std::acos(radius / 400));
        checkLocalPath(navigation, navigation.route(Start, {Goal}, clearance), clearance,
            static_cast<float>(shortest * 1.01));
    }
}

void multiSourceRoutingUsesTheSameLocalDetour() {
    for (bool box : {true, false}) {
        Navigation navigation;
        auto circles = DistantBases;
        std::vector<NavBox> boxes;
        if (box) boxes.push_back({{1600, 1200}, {100, 150}});
        else circles.push_back({3, {1600, 1200}, 125});
        navigation.sync(5100, boxes, circles);
        const float clearance = box ? 34.0f : 16.0f;
        const std::vector<Vec2> starts{{1200, 1180}, {1200, 1220}};
        const auto route = navigation.routeFromAny(starts, {Goal}, clearance);
        check((route.origin.x == starts[0].x && route.origin.y == starts[0].y)
            || (route.origin.x == starts[1].x && route.origin.y == starts[1].y),
            "route does not report its chosen source");
        checkLocalPath(navigation, route, clearance, 1000);
    }
}

void denserCircleSamplesPreserveANarrowPassage() {
    Navigation navigation;
    // The only crossing has 32.6 units between circle and upper wall: a
    // radius-16 worker fits, with just 0.6 units of additional clearance.
    // Inflating the ring to fix its chords would erase this passage.
    navigation.sync(5100, {
        {{2550, 500}, {2550, 560}},
        {{2550, 2000}, {2550, 642.4f}}
    }, {{3, {1600, 1200}, 125}});
    checkLocalPath(navigation, navigation.route(Start, {Goal}, 16), 16, 870);
}

} // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests{
        {"large units use connected box corners", largeUnitsUseConnectedBoxCorners},
        {"circle detours stay near their analytic shortest length", circleDetoursStayNearTheirAnalyticShortestLength},
        {"multi-source routing uses the same local detour", multiSourceRoutingUsesTheSameLocalDetour},
        {"denser circle samples preserve a narrow passage", denserCircleSamplesPreserveANarrowPassage},
    };
    int failed = 0;
    for (const auto& [name, test] : tests) {
        try { test(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) { ++failed; std::cerr << "FAIL " << name << ": " << error.what() << '\n'; }
    }
    std::cout << "RESULT passed=" << tests.size() - failed << " failed=" << failed << '\n';
    return failed ? 1 : 0;
}
