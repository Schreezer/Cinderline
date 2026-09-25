#include "Sim/Navigation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <deque>
#include <memory>
#include <unordered_map>
#include <utility>

namespace cinder {
namespace {

constexpr float NavGridCell = 16.0f;
constexpr float NavAdaptiveSpacing = 28.0f;
constexpr float NavAdaptiveLink = 42.0f;
constexpr float NavAttachmentRange = 42.0f;
constexpr int NavMaxAdaptiveNodes = 8192;
// A route cannot consume an entire simulation step on a large obstructed map.
constexpr int NavMaxExpandedNodes = 40000;
constexpr std::size_t NavMaxGoalAttachmentCacheEntries = 2048;
constexpr std::size_t NavMaxGoalAttachmentCacheBytes = 4 * 1024 * 1024;
constexpr float NavPi = 3.14159265358979323846f;
constexpr float NavInfinity = std::numeric_limits<float>::infinity();

bool navFinite(float value) { return std::isfinite(value); }
bool navFinite(Vec2 point) { return navFinite(point.x) && navFinite(point.y); }
float navSquare(float value) { return value * value; }
float navDistanceSquared(Vec2 a, Vec2 b) { return navSquare(a.x - b.x) + navSquare(a.y - b.y); }
float navDistance(Vec2 a, Vec2 b) { return std::sqrt(navDistanceSquared(a, b)); }
Vec2 navAdd(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
Vec2 navSubtract(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
Vec2 navScale(Vec2 value, float amount) { return {value.x * amount, value.y * amount}; }

float navPointSegmentDistanceSquared(Vec2 point, Vec2 a, Vec2 b)
{
    const Vec2 ab = navSubtract(b, a);
    const float lengthSquared = ab.x * ab.x + ab.y * ab.y;
    if (lengthSquared <= 1.0e-12f) return navDistanceSquared(point, a);
    const Vec2 ap = navSubtract(point, a);
    const float t = std::clamp((ap.x * ab.x + ap.y * ab.y) / lengthSquared, 0.0f, 1.0f);
    return navDistanceSquared(point, navAdd(a, navScale(ab, t)));
}

float navOrientation(Vec2 a, Vec2 b, Vec2 c)
{
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

bool navWithin(float value, float a, float b)
{
    return value >= std::min(a, b) - 1.0e-5f && value <= std::max(a, b) + 1.0e-5f;
}

bool navSegmentsIntersect(Vec2 a, Vec2 b, Vec2 c, Vec2 d)
{
    const float abC = navOrientation(a, b, c);
    const float abD = navOrientation(a, b, d);
    const float cdA = navOrientation(c, d, a);
    const float cdB = navOrientation(c, d, b);
    if (((abC > 0 && abD < 0) || (abC < 0 && abD > 0))
        && ((cdA > 0 && cdB < 0) || (cdA < 0 && cdB > 0))) return true;
    if (std::fabs(abC) <= 1.0e-5f && navWithin(c.x, a.x, b.x) && navWithin(c.y, a.y, b.y)) return true;
    if (std::fabs(abD) <= 1.0e-5f && navWithin(d.x, a.x, b.x) && navWithin(d.y, a.y, b.y)) return true;
    if (std::fabs(cdA) <= 1.0e-5f && navWithin(a.x, c.x, d.x) && navWithin(a.y, c.y, d.y)) return true;
    return std::fabs(cdB) <= 1.0e-5f && navWithin(b.x, c.x, d.x) && navWithin(b.y, c.y, d.y);
}

float navSegmentSegmentDistanceSquared(Vec2 a, Vec2 b, Vec2 c, Vec2 d)
{
    if (navSegmentsIntersect(a, b, c, d)) return 0;
    return std::min({navPointSegmentDistanceSquared(a, c, d), navPointSegmentDistanceSquared(b, c, d),
        navPointSegmentDistanceSquared(c, a, b), navPointSegmentDistanceSquared(d, a, b)});
}

float navSegmentBoxDistanceSquared(Vec2 from, Vec2 to, const NavBox& box)
{
    const float left = box.center.x - box.half.x;
    const float right = box.center.x + box.half.x;
    const float bottom = box.center.y - box.half.y;
    const float top = box.center.y + box.half.y;
    auto inside = [&](Vec2 point) {
        return point.x >= left && point.x <= right && point.y >= bottom && point.y <= top;
    };
    if (inside(from) || inside(to)) return 0;
    const Vec2 bl{left, bottom}, br{right, bottom}, tr{right, top}, tl{left, top};
    return std::min({navSegmentSegmentDistanceSquared(from, to, bl, br),
        navSegmentSegmentDistanceSquared(from, to, br, tr),
        navSegmentSegmentDistanceSquared(from, to, tr, tl),
        navSegmentSegmentDistanceSquared(from, to, tl, bl)});
}

float navCanonical(float value) { return value == 0 ? 0.0f : value; }

std::uint32_t navFloatBits(float value)
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

struct NavFingerprint {
    std::uint64_t value = 1469598103934665603ULL;
    void byte(std::uint8_t byte) { value = (value ^ byte) * 1099511628211ULL; }
    void integer(std::uint64_t number)
    {
        for (int i = 0; i < 8; ++i) {
            byte(static_cast<std::uint8_t>(number & 255));
            number >>= 8;
        }
    }
    void real(float number) { integer(navFloatBits(number)); }
};

std::int64_t navBucketKey(int x, int y)
{
    const std::uint64_t key = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32)
        | static_cast<std::uint32_t>(y);
    return static_cast<std::int64_t>(key);
}

} // namespace

struct Navigation::Impl {
    struct Geometry {
        float worldSize = 0;
        std::vector<NavBox> boxes;
        std::vector<NavCircle> circles;
        std::uint64_t fingerprint = 0;
    };

    struct Layer {
        float clearance = 0;
        int width = 0;
        int height = 0;
        int gridCount = 0;
        std::vector<std::uint8_t> clear;
        std::vector<std::uint8_t> edges;
        std::vector<Vec2> adaptive;
        std::vector<std::vector<int>> adaptiveEdges;
        std::vector<std::vector<int>> adaptiveGrid;
        std::vector<int> gridAdaptiveOffsets;
        std::vector<int> gridAdaptive;
        std::vector<int> component;

        int nodeCount() const { return gridCount + static_cast<int>(adaptive.size()); }
        Vec2 gridPoint(int node) const
        {
            return {(node % width + 0.5f) * NavGridCell, (node / width + 0.5f) * NavGridCell};
        }
        Vec2 point(int node) const
        {
            return node < gridCount ? gridPoint(node) : adaptive[node - gridCount];
        }
    };

    struct State {
        Geometry geometry;
        mutable std::vector<std::shared_ptr<const Layer>> layers;
    };

    struct OpenNode {
        float score = 0;
        float cost = 0;
        int node = -1;
    };

    struct OpenGreater {
        bool operator()(const OpenNode& a, const OpenNode& b) const
        {
            if (a.score != b.score) return a.score > b.score;
            if (a.cost != b.cost) return a.cost > b.cost;
            return a.node > b.node;
        }
    };

    struct Attachment {
        int node = -1;
        float cost = 0;
        int goal = -1;
    };

    struct CachedGoalAttachment {
        int node = -1;
        float cost = 0;
    };

    struct GoalAttachmentKey {
        std::uint32_t x = 0;
        std::uint32_t y = 0;
        std::uint32_t clearance = 0;

        bool operator==(const GoalAttachmentKey& other) const
        {
            return x == other.x && y == other.y && clearance == other.clearance;
        }
    };

    struct GoalAttachmentKeyHash {
        std::size_t operator()(const GoalAttachmentKey& key) const
        {
            std::uint64_t value = 1469598103934665603ULL;
            auto add = [&](std::uint32_t part) {
                for (int byte = 0; byte < 4; ++byte) {
                    value = (value ^ static_cast<std::uint8_t>(part & 255)) * 1099511628211ULL;
                    part >>= 8;
                }
            };
            add(key.x); add(key.y); add(key.clearance);
            return static_cast<std::size_t>(value);
        }
    };

    static_assert(sizeof(CachedGoalAttachment) == 8,
        "goal attachment cache accounting assumes one 8-byte node/cost pair");

    std::shared_ptr<State> state = std::make_shared<State>();

    // Search arrays use stamps so a route does not clear the full grid.
    mutable std::vector<float> costs;
    mutable std::vector<int> parents;
    mutable std::vector<int> sourceIndex;
    mutable std::vector<std::uint32_t> seen;
    mutable std::vector<std::uint32_t> closed;
    mutable std::vector<float> goalSuffix;
    mutable std::vector<int> goalIndex;
    mutable std::vector<std::uint32_t> hasGoal;
    mutable std::uint32_t searchStamp = 0;
    mutable std::vector<OpenNode> open;
    mutable std::unordered_map<GoalAttachmentKey, std::vector<CachedGoalAttachment>,
        GoalAttachmentKeyHash> goalAttachmentCache;
    mutable std::deque<GoalAttachmentKey> goalAttachmentFIFO;
    mutable std::size_t goalAttachmentCacheBytes = 0;

    void clearGoalAttachmentCache()
    {
        goalAttachmentCache.clear();
        goalAttachmentFIFO.clear();
        goalAttachmentCacheBytes = 0;
    }

    static bool sameGeometry(const Geometry& a, const Geometry& b)
    {
        if (navFloatBits(a.worldSize) != navFloatBits(b.worldSize)
            || a.boxes.size() != b.boxes.size() || a.circles.size() != b.circles.size()) return false;
        for (std::size_t i = 0; i < a.boxes.size(); ++i) {
            const auto& x = a.boxes[i]; const auto& y = b.boxes[i];
            if (navFloatBits(x.center.x) != navFloatBits(y.center.x)
                || navFloatBits(x.center.y) != navFloatBits(y.center.y)
                || navFloatBits(x.half.x) != navFloatBits(y.half.x)
                || navFloatBits(x.half.y) != navFloatBits(y.half.y)) return false;
        }
        for (std::size_t i = 0; i < a.circles.size(); ++i) {
            const auto& x = a.circles[i]; const auto& y = b.circles[i];
            if (x.id != y.id || navFloatBits(x.center.x) != navFloatBits(y.center.x)
                || navFloatBits(x.center.y) != navFloatBits(y.center.y)
                || navFloatBits(x.radius) != navFloatBits(y.radius)) return false;
        }
        return true;
    }

    static Geometry normalize(float worldSize, const std::vector<NavBox>& boxes,
        const std::vector<NavCircle>& circles)
    {
        Geometry result;
        result.worldSize = navFinite(worldSize) && worldSize > 0 ? navCanonical(worldSize) : 0;
        for (NavBox box : boxes) {
            if (!navFinite(box.center) || !navFinite(box.half) || box.half.x < 0 || box.half.y < 0) continue;
            box.center.x = navCanonical(box.center.x); box.center.y = navCanonical(box.center.y);
            box.half.x = navCanonical(box.half.x); box.half.y = navCanonical(box.half.y);
            result.boxes.push_back(box);
        }
        for (NavCircle circle : circles) {
            if (!navFinite(circle.center) || !navFinite(circle.radius) || circle.radius < 0) continue;
            circle.center.x = navCanonical(circle.center.x); circle.center.y = navCanonical(circle.center.y);
            circle.radius = navCanonical(circle.radius);
            result.circles.push_back(circle);
        }
        auto boxLess = [](const NavBox& a, const NavBox& b) {
            return std::array<std::uint32_t, 4>{navFloatBits(a.center.x), navFloatBits(a.center.y),
                       navFloatBits(a.half.x), navFloatBits(a.half.y)}
                < std::array<std::uint32_t, 4>{navFloatBits(b.center.x), navFloatBits(b.center.y),
                       navFloatBits(b.half.x), navFloatBits(b.half.y)};
        };
        auto circleLess = [](const NavCircle& a, const NavCircle& b) {
            if (a.id != b.id) return a.id < b.id;
            return std::array<std::uint32_t, 3>{navFloatBits(a.center.x), navFloatBits(a.center.y), navFloatBits(a.radius)}
                < std::array<std::uint32_t, 3>{navFloatBits(b.center.x), navFloatBits(b.center.y), navFloatBits(b.radius)};
        };
        std::sort(result.boxes.begin(), result.boxes.end(), boxLess);
        std::sort(result.circles.begin(), result.circles.end(), circleLess);
        NavFingerprint hash;
        hash.integer(0x43494e4445524e41ULL);
        hash.real(result.worldSize);
        hash.integer(result.boxes.size());
        for (const auto& box : result.boxes) {
            hash.real(box.center.x); hash.real(box.center.y); hash.real(box.half.x); hash.real(box.half.y);
        }
        hash.integer(result.circles.size());
        for (const auto& circle : result.circles) {
            hash.integer(circle.id); hash.real(circle.center.x); hash.real(circle.center.y); hash.real(circle.radius);
        }
        result.fingerprint = hash.value;
        return result;
    }

    bool pointClear(Vec2 point, float clearance, Id ignore) const
    {
        const Geometry& geometry = state->geometry;
        if (!navFinite(point) || !navFinite(clearance) || clearance < 0 || geometry.worldSize <= 0
            || point.x < clearance || point.y < clearance
            || point.x > geometry.worldSize - clearance || point.y > geometry.worldSize - clearance) return false;
        const float clearanceSquared = clearance * clearance;
        for (const NavBox& box : geometry.boxes) {
            const float dx = std::max(std::fabs(point.x - box.center.x) - box.half.x, 0.0f);
            const float dy = std::max(std::fabs(point.y - box.center.y) - box.half.y, 0.0f);
            if ((dx == 0 && dy == 0) || dx * dx + dy * dy < clearanceSquared) return false;
        }
        for (const NavCircle& circle : geometry.circles) {
            if (ignore && circle.id == ignore) continue;
            const float combined = clearance + circle.radius;
            if (navDistanceSquared(point, circle.center) < combined * combined) return false;
        }
        return true;
    }

    bool segmentClear(Vec2 from, Vec2 to, float clearance, Id ignore) const
    {
        if (!pointClear(from, clearance, ignore) || !pointClear(to, clearance, ignore)) return false;
        return segmentInteriorClear(from, to, clearance, ignore);
    }

    // Both endpoints must already be clear for this clearance/ignore pair.
    // Keep the exact segment predicates shared with the public validated path.
    bool segmentInteriorClear(Vec2 from, Vec2 to, float clearance, Id ignore) const
    {
        const Geometry& geometry = state->geometry;
        const float clearanceSquared = clearance * clearance;
        for (const NavBox& box : geometry.boxes) {
            const float separation = navSegmentBoxDistanceSquared(from, to, box);
            if (separation == 0 || separation < clearanceSquared) return false;
        }
        for (const NavCircle& circle : geometry.circles) {
            if (ignore && circle.id == ignore) continue;
            const float combined = clearance + circle.radius;
            if (navPointSegmentDistanceSquared(circle.center, from, to) < combined * combined) return false;
        }
        return true;
    }

    void addAdaptiveCandidate(Layer& layer, Vec2 point) const
    {
        if (static_cast<int>(layer.adaptive.size()) >= NavMaxAdaptiveNodes) return;
        if (pointClear(point, layer.clearance, 0)) layer.adaptive.push_back(point);
    }

    void addBoxCandidates(Layer& layer, const NavBox& box) const
    {
        const float margin = layer.clearance + std::max(0.25f, layer.clearance * 0.002f);
        const float left = box.center.x - box.half.x;
        const float right = box.center.x + box.half.x;
        const float bottom = box.center.y - box.half.y;
        const float top = box.center.y + box.half.y;
        auto addRange = [&](bool vertical, float fixed, float first, float last) {
            const int count = std::max(1, static_cast<int>(std::ceil((last - first) / NavAdaptiveSpacing)));
            for (int i = 0; i <= count; ++i) {
                const float value = first + (last - first) * static_cast<float>(i) / count;
                addAdaptiveCandidate(layer, vertical ? Vec2{fixed, value} : Vec2{value, fixed});
            }
        };
        addRange(true, left - margin, bottom, top);
        addRange(true, right + margin, bottom, top);
        addRange(false, bottom - margin, left, right);
        addRange(false, top + margin, left, right);
        const std::array<Vec2, 4> corners{{{left, bottom}, {right, bottom}, {right, top}, {left, top}}};
        const std::array<float, 4> starts{{NavPi, -0.5f * NavPi, 0, 0.5f * NavPi}};
        for (int corner = 0; corner < 4; ++corner) {
            for (int sample = 0; sample <= 6; ++sample) {
                const float angle = starts[corner] + 0.5f * NavPi * static_cast<float>(sample) / 6;
                addAdaptiveCandidate(layer, navAdd(corners[corner], {std::cos(angle) * margin, std::sin(angle) * margin}));
            }
        }
    }

    void addCircleCandidates(Layer& layer, const NavCircle& circle) const
    {
        const float radius = circle.radius + layer.clearance
            + std::max(0.25f, layer.clearance * 0.002f);
        const int samples = std::clamp(static_cast<int>(std::ceil(2 * NavPi * radius / NavAdaptiveSpacing)), 16, 40);
        for (int sample = 0; sample < samples; ++sample) {
            const float angle = 2 * NavPi * static_cast<float>(sample) / samples;
            addAdaptiveCandidate(layer, navAdd(circle.center, {std::cos(angle) * radius, std::sin(angle) * radius}));
        }
    }

    std::shared_ptr<const Layer> buildLayer(float clearance) const
    {
        auto layer = std::make_shared<Layer>();
        layer->clearance = clearance;
        layer->width = static_cast<int>(std::ceil(state->geometry.worldSize / NavGridCell));
        layer->height = layer->width;
        layer->gridCount = layer->width * layer->height;
        layer->clear.assign(layer->gridCount, 0);
        layer->edges.assign(layer->gridCount, 0);
        for (int node = 0; node < layer->gridCount; ++node) {
            const Vec2 point = layer->gridPoint(node);
            layer->clear[node] = point.x >= clearance && point.y >= clearance
                && point.x <= state->geometry.worldSize - clearance
                && point.y <= state->geometry.worldSize - clearance;
        }
        auto visitGridBounds = [&](float left, float right, float bottom, float top, auto&& visitor) {
            const int firstX = std::clamp(static_cast<int>(std::floor(left / NavGridCell)) - 1, 0, layer->width - 1);
            const int lastX = std::clamp(static_cast<int>(std::floor(right / NavGridCell)) + 1, 0, layer->width - 1);
            const int firstY = std::clamp(static_cast<int>(std::floor(bottom / NavGridCell)) - 1, 0, layer->height - 1);
            const int lastY = std::clamp(static_cast<int>(std::floor(top / NavGridCell)) + 1, 0, layer->height - 1);
            for (int y = firstY; y <= lastY; ++y) for (int x = firstX; x <= lastX; ++x) {
                visitor(y * layer->width + x);
            }
        };
        const float clearanceSquared = clearance * clearance;
        for (const NavBox& box : state->geometry.boxes) {
            visitGridBounds(box.center.x - box.half.x - clearance,
                box.center.x + box.half.x + clearance,
                box.center.y - box.half.y - clearance,
                box.center.y + box.half.y + clearance, [&](int node) {
                    const Vec2 point = layer->gridPoint(node);
                    const float x = std::max(std::fabs(point.x - box.center.x) - box.half.x, 0.0f);
                    const float y = std::max(std::fabs(point.y - box.center.y) - box.half.y, 0.0f);
                    if ((x == 0 && y == 0) || x * x + y * y < clearanceSquared) layer->clear[node] = 0;
                });
        }
        for (const NavCircle& circle : state->geometry.circles) {
            const float combined = clearance + circle.radius;
            visitGridBounds(circle.center.x - combined, circle.center.x + combined,
                circle.center.y - combined, circle.center.y + combined, [&](int node) {
                    if (navDistanceSquared(layer->gridPoint(node), circle.center) < combined * combined) layer->clear[node] = 0;
                });
        }

        constexpr std::array<int, 8> dx{{1, 1, 0, -1, -1, -1, 0, 1}};
        constexpr std::array<int, 8> dy{{0, 1, 1, 1, 0, -1, -1, -1}};
        for (int node = 0; node < layer->gridCount; ++node) {
            if (!layer->clear[node]) continue;
            const int x = node % layer->width, y = node / layer->width;
            for (int direction = 0; direction < 4; ++direction) {
                const int nx = x + dx[direction], ny = y + dy[direction];
                if (nx < 0 || nx >= layer->width || ny < 0 || ny >= layer->height) continue;
                const int next = ny * layer->width + nx;
                if (!layer->clear[next]) continue;
                layer->edges[node] |= static_cast<std::uint8_t>(1u << direction);
                layer->edges[next] |= static_cast<std::uint8_t>(1u << ((direction + 4) % 8));
            }
        }
        auto clearObstacleEdges = [&](float left, float right, float bottom, float top, auto&& collides) {
            visitGridBounds(left - NavGridCell, right + NavGridCell, bottom - NavGridCell, top + NavGridCell, [&](int node) {
                const int x = node % layer->width, y = node / layer->width;
                for (int direction = 0; direction < 4; ++direction) {
                    if (!(layer->edges[node] & (1u << direction))) continue;
                    const int nx = x + dx[direction], ny = y + dy[direction];
                    if (nx < 0 || nx >= layer->width || ny < 0 || ny >= layer->height) continue;
                    const int next = ny * layer->width + nx;
                    if (!collides(layer->gridPoint(node), layer->gridPoint(next))) continue;
                    layer->edges[node] &= static_cast<std::uint8_t>(~(1u << direction));
                    layer->edges[next] &= static_cast<std::uint8_t>(~(1u << ((direction + 4) % 8)));
                }
            });
        };
        for (const NavBox& box : state->geometry.boxes) {
            clearObstacleEdges(box.center.x - box.half.x - clearance,
                box.center.x + box.half.x + clearance,
                box.center.y - box.half.y - clearance,
                box.center.y + box.half.y + clearance, [&](Vec2 from, Vec2 to) {
                    const float separation = navSegmentBoxDistanceSquared(from, to, box);
                    return separation == 0 || separation < clearanceSquared;
                });
        }
        for (const NavCircle& circle : state->geometry.circles) {
            const float combined = clearance + circle.radius;
            clearObstacleEdges(circle.center.x - combined, circle.center.x + combined,
                circle.center.y - combined, circle.center.y + combined, [&](Vec2 from, Vec2 to) {
                    return navPointSegmentDistanceSquared(circle.center, from, to) < combined * combined;
                });
        }

        for (const NavBox& box : state->geometry.boxes) addBoxCandidates(*layer, box);
        for (const NavCircle& circle : state->geometry.circles) addCircleCandidates(*layer, circle);
        std::sort(layer->adaptive.begin(), layer->adaptive.end(), [](Vec2 a, Vec2 b) {
            return a.x != b.x ? a.x < b.x : a.y < b.y;
        });
        layer->adaptive.erase(std::unique(layer->adaptive.begin(), layer->adaptive.end(), [](Vec2 a, Vec2 b) {
            return navDistanceSquared(a, b) < 0.01f;
        }), layer->adaptive.end());

        const int adaptiveCount = static_cast<int>(layer->adaptive.size());
        layer->adaptiveEdges.resize(adaptiveCount);
        layer->adaptiveGrid.resize(adaptiveCount);
        std::vector<std::pair<int, int>> gridLinks;
        const int gridRange = static_cast<int>(std::ceil(NavAdaptiveLink / NavGridCell));
        for (int adaptive = 0; adaptive < adaptiveCount; ++adaptive) {
            const Vec2 point = layer->adaptive[adaptive];
            const int centerX = static_cast<int>(point.x / NavGridCell);
            const int centerY = static_cast<int>(point.y / NavGridCell);
            for (int y = centerY - gridRange; y <= centerY + gridRange; ++y) {
                for (int x = centerX - gridRange; x <= centerX + gridRange; ++x) {
                    if (x < 0 || x >= layer->width || y < 0 || y >= layer->height) continue;
                    const int grid = y * layer->width + x;
                    if (!layer->clear[grid] || navDistanceSquared(point, layer->gridPoint(grid)) > navSquare(NavAdaptiveLink)
                        || !segmentClear(point, layer->gridPoint(grid), clearance, 0)) continue;
                    gridLinks.emplace_back(grid, adaptive);
                    layer->adaptiveGrid[adaptive].push_back(grid);
                }
            }
        }

        std::unordered_map<std::int64_t, std::vector<int>> buckets;
        buckets.reserve(static_cast<std::size_t>(adaptiveCount) * 2);
        for (int adaptive = 0; adaptive < adaptiveCount; ++adaptive) {
            const Vec2 point = layer->adaptive[adaptive];
            const int bx = static_cast<int>(std::floor(point.x / NavAdaptiveLink));
            const int by = static_cast<int>(std::floor(point.y / NavAdaptiveLink));
            for (int y = by - 1; y <= by + 1; ++y) {
                for (int x = bx - 1; x <= bx + 1; ++x) {
                    const auto found = buckets.find(navBucketKey(x, y));
                    if (found == buckets.end()) continue;
                    for (int other : found->second) {
                        if (navDistanceSquared(point, layer->adaptive[other]) <= navSquare(NavAdaptiveLink)
                            && segmentClear(point, layer->adaptive[other], clearance, 0)) {
                            layer->adaptiveEdges[adaptive].push_back(other);
                            layer->adaptiveEdges[other].push_back(adaptive);
                        }
                    }
                }
            }
            buckets[navBucketKey(bx, by)].push_back(adaptive);
        }
        for (auto& edges : layer->adaptiveEdges) std::sort(edges.begin(), edges.end());

        std::sort(gridLinks.begin(), gridLinks.end());
        layer->gridAdaptiveOffsets.assign(layer->gridCount + 1, 0);
        for (const auto& link : gridLinks) ++layer->gridAdaptiveOffsets[link.first + 1];
        for (int i = 1; i <= layer->gridCount; ++i) {
            layer->gridAdaptiveOffsets[i] += layer->gridAdaptiveOffsets[i - 1];
        }
        layer->gridAdaptive.resize(gridLinks.size());
        std::vector<int> offsets = layer->gridAdaptiveOffsets;
        for (const auto& link : gridLinks) layer->gridAdaptive[offsets[link.first]++] = link.second;

        layer->component.assign(layer->nodeCount(), -1);
        std::vector<int> queue;
        queue.reserve(layer->nodeCount());
        int component = 0;
        for (int first = 0; first < layer->nodeCount(); ++first) {
            if (layer->component[first] >= 0 || (first < layer->gridCount && !layer->clear[first])) continue;
            queue.clear(); queue.push_back(first); layer->component[first] = component;
            for (std::size_t cursor = 0; cursor < queue.size(); ++cursor) {
                const int node = queue[cursor];
                auto visit = [&](int next) {
                    if (layer->component[next] < 0) {
                        layer->component[next] = component;
                        queue.push_back(next);
                    }
                };
                if (node < layer->gridCount) {
                    const int x = node % layer->width, y = node / layer->width;
                    for (int direction = 0; direction < 8; ++direction) {
                        if (!(layer->edges[node] & (1u << direction))) continue;
                        visit((y + dy[direction]) * layer->width + x + dx[direction]);
                    }
                    for (int i = layer->gridAdaptiveOffsets[node]; i < layer->gridAdaptiveOffsets[node + 1]; ++i) {
                        visit(layer->gridCount + layer->gridAdaptive[i]);
                    }
                } else {
                    const int adaptive = node - layer->gridCount;
                    for (int next : layer->adaptiveEdges[adaptive]) visit(layer->gridCount + next);
                    for (int next : layer->adaptiveGrid[adaptive]) visit(next);
                }
            }
            ++component;
        }
        return layer;
    }

    std::shared_ptr<const Layer> layer(float clearance) const
    {
        const std::uint32_t key = navFloatBits(clearance);
        for (const auto& candidate : state->layers) {
            if (navFloatBits(candidate->clearance) == key) return candidate;
        }
        auto result = buildLayer(clearance);
        // The simulation uses fewer than twelve distinct ground clearances.
        if (state->layers.size() >= 12) state->layers.erase(state->layers.begin());
        state->layers.push_back(result);
        return result;
    }

    bool nodeClear(const Layer& layer, int node, Id ignore) const
    {
        if (node >= layer.gridCount) return true;
        return layer.clear[node] || (ignore && pointClear(layer.gridPoint(node), layer.clearance, ignore));
    }

    std::vector<Attachment> attachments(const Layer& layer, Vec2 point, float clearance,
        Id ignore, int goal) const
    {
        // route/routeFromAny validate point before requesting attachments.
        // Grid targets pass nodeClear; adaptive targets were validated when
        // this clearance layer was built (ignoring a circle cannot block them).
        // Rechecking both endpoints for every candidate repeats full obstacle
        // scans, especially when attaching many cold mining destinations.
        std::vector<Attachment> result;
        const int range = static_cast<int>(std::ceil(NavAttachmentRange / NavGridCell));
        const int centerX = static_cast<int>(point.x / NavGridCell);
        const int centerY = static_cast<int>(point.y / NavGridCell);
        for (int y = centerY - range; y <= centerY + range; ++y) {
            for (int x = centerX - range; x <= centerX + range; ++x) {
                if (x < 0 || x >= layer.width || y < 0 || y >= layer.height) continue;
                const int node = y * layer.width + x;
                const Vec2 target = layer.gridPoint(node);
                const float cost = navDistance(point, target);
                if (cost <= NavAttachmentRange && nodeClear(layer, node, ignore)
                    && segmentInteriorClear(point, target, clearance, ignore)) result.push_back({node, cost, goal});
            }
        }
        for (int adaptive = 0; adaptive < static_cast<int>(layer.adaptive.size()); ++adaptive) {
            const float cost = navDistance(point, layer.adaptive[adaptive]);
            // Boundary nodes form a sparse visibility overlay. Long exact-clear
            // attachments avoid expanding thousands of open lattice nodes just
            // to reach the first obstacle corner.
            if (segmentInteriorClear(point, layer.adaptive[adaptive], clearance, ignore)) {
                result.push_back({layer.gridCount + adaptive, cost, goal});
            }
        }
        std::sort(result.begin(), result.end(), [](const Attachment& a, const Attachment& b) {
            return a.node != b.node ? a.node < b.node : a.cost < b.cost;
        });
        result.erase(std::unique(result.begin(), result.end(), [](const Attachment& a, const Attachment& b) {
            return a.node == b.node;
        }), result.end());
        return result;
    }

    std::vector<Attachment> cachedGoalAttachments(const Layer& layer, Vec2 point,
        float clearance, int goal) const
    {
        const GoalAttachmentKey key{
            navFloatBits(point.x), navFloatBits(point.y), navFloatBits(clearance)};
        const auto found = goalAttachmentCache.find(key);
        if (found != goalAttachmentCache.end()) {
            std::vector<Attachment> result;
            result.reserve(found->second.size());
            for (const CachedGoalAttachment& attachment : found->second) {
                result.push_back({attachment.node, attachment.cost, goal});
            }
            return result;
        }

        std::vector<Attachment> result = attachments(layer, point, clearance, 0, goal);
        std::vector<CachedGoalAttachment> cached(result.size());
        for (std::size_t index = 0; index < result.size(); ++index) {
            cached[index] = {result[index].node, result[index].cost};
        }
        const std::size_t storedBytes = cached.capacity() * sizeof(CachedGoalAttachment);
        if (storedBytes > NavMaxGoalAttachmentCacheBytes) return result;

        while (!goalAttachmentFIFO.empty()
            && (goalAttachmentCache.size() >= NavMaxGoalAttachmentCacheEntries
                || goalAttachmentCacheBytes > NavMaxGoalAttachmentCacheBytes - storedBytes)) {
            const GoalAttachmentKey oldest = goalAttachmentFIFO.front();
            goalAttachmentFIFO.pop_front();
            const auto entry = goalAttachmentCache.find(oldest);
            if (entry == goalAttachmentCache.end()) continue;
            goalAttachmentCacheBytes -= entry->second.capacity() * sizeof(CachedGoalAttachment);
            goalAttachmentCache.erase(entry);
        }

        goalAttachmentCacheBytes += storedBytes;
        goalAttachmentFIFO.push_back(key);
        goalAttachmentCache.emplace(key, std::move(cached));
        return result;
    }

    template <typename Visitor>
    void visitNeighbors(const Layer& layer, int node, Id ignore, Visitor&& visitor) const
    {
        constexpr std::array<int, 8> dx{{1, 1, 0, -1, -1, -1, 0, 1}};
        constexpr std::array<int, 8> dy{{0, 1, 1, 1, 0, -1, -1, -1}};
        if (node < layer.gridCount) {
            const int x = node % layer.width, y = node / layer.width;
            for (int direction = 0; direction < 8; ++direction) {
                const int nx = x + dx[direction], ny = y + dy[direction];
                if (nx < 0 || nx >= layer.width || ny < 0 || ny >= layer.height) continue;
                const int next = ny * layer.width + nx;
                bool connected = layer.edges[node] & (1u << direction);
                if (!connected && ignore && nodeClear(layer, next, ignore)) {
                    connected = segmentClear(layer.gridPoint(node), layer.gridPoint(next), layer.clearance, ignore);
                }
                if (connected) visitor(next, direction & 1 ? NavGridCell * 1.41421356237f : NavGridCell);
            }
            for (int i = layer.gridAdaptiveOffsets[node]; i < layer.gridAdaptiveOffsets[node + 1]; ++i) {
                const int next = layer.gridCount + layer.gridAdaptive[i];
                visitor(next, navDistance(layer.gridPoint(node), layer.point(next)));
            }
        } else {
            const int adaptive = node - layer.gridCount;
            for (int next : layer.adaptiveEdges[adaptive]) {
                visitor(layer.gridCount + next, navDistance(layer.point(node), layer.adaptive[next]));
            }
            if (!ignore) {
                for (int next : layer.adaptiveGrid[adaptive]) {
                    visitor(next, navDistance(layer.point(node), layer.gridPoint(next)));
                }
                return;
            }
            const Vec2 point = layer.point(node);
            const int range = static_cast<int>(std::ceil(NavAdaptiveLink / NavGridCell));
            const int centerX = static_cast<int>(point.x / NavGridCell);
            const int centerY = static_cast<int>(point.y / NavGridCell);
            for (int y = centerY - range; y <= centerY + range; ++y) {
                for (int x = centerX - range; x <= centerX + range; ++x) {
                    if (x < 0 || x >= layer.width || y < 0 || y >= layer.height) continue;
                    const int next = y * layer.width + x;
                    const float cost = navDistance(point, layer.gridPoint(next));
                    if (cost <= NavAdaptiveLink && nodeClear(layer, next, ignore)
                        && segmentClear(point, layer.gridPoint(next), layer.clearance, ignore)) visitor(next, cost);
                }
            }
        }
    }

    void prepareScratch(int count) const
    {
        if (static_cast<int>(costs.size()) < count) {
            costs.resize(count); parents.resize(count); seen.resize(count);
            sourceIndex.resize(count);
            closed.resize(count); goalSuffix.resize(count); goalIndex.resize(count); hasGoal.resize(count);
        }
        ++searchStamp;
        if (searchStamp == 0) {
            std::fill(seen.begin(), seen.end(), 0);
            std::fill(closed.begin(), closed.end(), 0);
            std::fill(hasGoal.begin(), hasGoal.end(), 0);
            searchStamp = 1;
        }
        open.clear();
    }

    NavigationResult completeRoute(const Layer& graph, Vec2 from, const std::vector<Vec2>& goals,
        int bestNode, int bestGoal, int expanded, float clearance, Id ignore) const
    {
        NavigationResult result;
        result.expanded = expanded;
        std::vector<Vec2> raw;
        for (int node = bestNode; node >= 0; node = parents[node]) raw.push_back(graph.point(node));
        std::reverse(raw.begin(), raw.end());
        raw.push_back(goals[bestGoal]);
        Vec2 cursor = from;
        for (std::size_t first = 0; first < raw.size();) {
            std::size_t farthest = first;
            for (std::size_t candidate = first + 1; candidate < raw.size(); ++candidate) {
                if (!segmentClear(cursor, raw[candidate], clearance, ignore)) break;
                farthest = candidate;
            }
            const Vec2 point = raw[farthest];
            if (navDistanceSquared(cursor, point) > 1.0e-6f) result.points.push_back(point);
            cursor = point; first = farthest + 1;
        }
        if (result.points.empty() || navDistanceSquared(result.points.back(), goals[bestGoal]) > 1.0e-6f) {
            return NavigationResult{};
        }
        result.cost = 0;
        cursor = from;
        for (Vec2 point : result.points) { result.cost += navDistance(cursor, point); cursor = point; }
        result.reached = true;
        return result;
    }

    NavigationResult adaptiveRoute(const Layer& graph, Vec2 from, const std::vector<Vec2>& goals,
        const std::vector<Attachment>& allStarts, const std::vector<Attachment>& allEnds,
        float clearance, Id ignore) const
    {
        NavigationResult result;
        std::vector<Attachment> starts, ends;
        for (const Attachment& start : allStarts) if (start.node >= graph.gridCount) starts.push_back(start);
        for (const Attachment& end : allEnds) if (end.node >= graph.gridCount) ends.push_back(end);
        if (starts.empty() || ends.empty()) return result;

        prepareScratch(graph.nodeCount());
        auto heuristic = [&](int node) {
            float best = NavInfinity;
            const Vec2 point = graph.point(node);
            for (Vec2 goal : goals) best = std::min(best, navDistance(point, goal));
            return best;
        };
        auto push = [&](OpenNode node) {
            open.push_back(node);
            std::push_heap(open.begin(), open.end(), OpenGreater{});
        };
        for (const Attachment& end : ends) {
            if (hasGoal[end.node] != searchStamp || end.cost < goalSuffix[end.node]
                || (end.cost == goalSuffix[end.node] && end.goal < goalIndex[end.node])) {
                hasGoal[end.node] = searchStamp; goalSuffix[end.node] = end.cost; goalIndex[end.node] = end.goal;
            }
        }
        for (const Attachment& start : starts) {
            if (seen[start.node] != searchStamp || start.cost < costs[start.node]) {
                seen[start.node] = searchStamp; costs[start.node] = start.cost; parents[start.node] = -1;
                push({start.cost + heuristic(start.node), start.cost, start.node});
            }
        }

        float bestCost = NavInfinity;
        int bestNode = -1, bestGoal = -1;
        while (!open.empty()) {
            std::pop_heap(open.begin(), open.end(), OpenGreater{});
            const OpenNode current = open.back(); open.pop_back();
            if (closed[current.node] == searchStamp || seen[current.node] != searchStamp
                || current.cost != costs[current.node]) continue;
            if (current.score >= bestCost) break;
            closed[current.node] = searchStamp;
            ++result.expanded;
            if (result.expanded >= NavMaxExpandedNodes) { result.exhausted = true; return result; }
            if (hasGoal[current.node] == searchStamp) {
                const float candidate = current.cost + goalSuffix[current.node];
                if (candidate < bestCost || (candidate == bestCost && goalIndex[current.node] < bestGoal)) {
                    bestCost = candidate; bestNode = current.node; bestGoal = goalIndex[current.node];
                }
            }
            const int adaptive = current.node - graph.gridCount;
            for (int nextAdaptive : graph.adaptiveEdges[adaptive]) {
                const int next = graph.gridCount + nextAdaptive;
                if (closed[next] == searchStamp) continue;
                const float candidate = current.cost + navDistance(graph.point(current.node), graph.point(next));
                if (seen[next] != searchStamp || candidate + 1.0e-5f < costs[next]
                    || (std::fabs(candidate - costs[next]) <= 1.0e-5f && current.node < parents[next])) {
                    seen[next] = searchStamp; costs[next] = candidate; parents[next] = current.node;
                    push({candidate + heuristic(next), candidate, next});
                }
            }
        }
        return bestNode < 0 ? result
            : completeRoute(graph, from, goals, bestNode, bestGoal, result.expanded, clearance, ignore);
    }

    NavigationResult route(Vec2 from, const std::vector<Vec2>& suppliedGoals,
        float clearance, Id ignore) const
    {
        NavigationResult result;
        // Moving-unit ids are not part of static navigation geometry. Treating
        // every such id as a real exception disables component checks and the
        // cached edge mask, so normalize absent ids before touching the graph.
        if (ignore) {
            const bool present = std::any_of(state->geometry.circles.begin(), state->geometry.circles.end(),
                [&](const NavCircle& circle) { return circle.id == ignore; });
            if (!present) ignore = 0;
        }
        if (!navFinite(from) || !navFinite(clearance) || clearance < 0
            || !pointClear(from, clearance, ignore) || suppliedGoals.empty()) return result;
        std::vector<Vec2> goals;
        goals.reserve(suppliedGoals.size());
        for (Vec2 goal : suppliedGoals) {
            if (!pointClear(goal, clearance, ignore)) continue;
            bool duplicate = false;
            for (Vec2 existing : goals) if (navDistanceSquared(goal, existing) <= 1.0e-6f) duplicate = true;
            if (!duplicate) goals.push_back(goal);
        }
        if (goals.empty()) return result;

        float directCost = NavInfinity;
        int directGoal = -1;
        for (int goal = 0; goal < static_cast<int>(goals.size()); ++goal) {
            const float candidate = navDistance(from, goals[goal]);
            if (candidate <= 1.0e-3f) return {true, false, {}, 0, 0, from};
            if (candidate < directCost && segmentClear(from, goals[goal], clearance, ignore)) {
                directCost = candidate; directGoal = goal;
            }
        }
        if (directGoal >= 0) return {true, false, {goals[directGoal]}, directCost, 0, from};

        const auto cachedLayer = layer(clearance);
        const Layer& graph = *cachedLayer;
        std::vector<Attachment> starts = attachments(graph, from, clearance, ignore, -1);
        if (starts.empty()) return result;
        std::vector<Attachment> ends;
        for (int goal = 0; goal < static_cast<int>(goals.size()); ++goal) {
            std::vector<Attachment> attached = ignore
                ? attachments(graph, goals[goal], clearance, ignore, goal)
                : cachedGoalAttachments(graph, goals[goal], clearance, goal);
            ends.insert(ends.end(), attached.begin(), attached.end());
        }
        if (ends.empty()) return result;

        NavigationResult adaptive = adaptiveRoute(graph, from, goals, starts, ends, clearance, ignore);
        if (adaptive.reached || adaptive.exhausted) return adaptive;
        result.expanded = adaptive.expanded;

        if (!ignore) {
            std::vector<int> startComponents;
            for (const Attachment& start : starts) startComponents.push_back(graph.component[start.node]);
            std::sort(startComponents.begin(), startComponents.end());
            startComponents.erase(std::unique(startComponents.begin(), startComponents.end()), startComponents.end());
            ends.erase(std::remove_if(ends.begin(), ends.end(), [&](const Attachment& end) {
                return !std::binary_search(startComponents.begin(), startComponents.end(), graph.component[end.node]);
            }), ends.end());
            if (ends.empty()) return result;
        }

        prepareScratch(graph.nodeCount());
        auto heuristic = [&](int node) {
            float best = NavInfinity;
            const Vec2 point = graph.point(node);
            for (Vec2 goal : goals) best = std::min(best, navDistance(point, goal));
            return best;
        };
        auto push = [&](OpenNode node) {
            open.push_back(node);
            std::push_heap(open.begin(), open.end(), OpenGreater{});
        };
        for (const Attachment& end : ends) {
            if (hasGoal[end.node] != searchStamp || end.cost < goalSuffix[end.node]
                || (end.cost == goalSuffix[end.node] && end.goal < goalIndex[end.node])) {
                hasGoal[end.node] = searchStamp;
                goalSuffix[end.node] = end.cost;
                goalIndex[end.node] = end.goal;
            }
        }
        for (const Attachment& start : starts) {
            if (seen[start.node] != searchStamp || start.cost < costs[start.node]) {
                seen[start.node] = searchStamp; costs[start.node] = start.cost; parents[start.node] = -1;
                push({start.cost + heuristic(start.node), start.cost, start.node});
            }
        }

        float bestCost = NavInfinity;
        int bestNode = -1;
        int bestGoal = -1;
        while (!open.empty()) {
            std::pop_heap(open.begin(), open.end(), OpenGreater{});
            const OpenNode current = open.back(); open.pop_back();
            if (closed[current.node] == searchStamp || seen[current.node] != searchStamp
                || current.cost != costs[current.node]) continue;
            if (current.score >= bestCost) break;
            closed[current.node] = searchStamp;
            ++result.expanded;
            if (result.expanded >= NavMaxExpandedNodes) {
                result.exhausted = true;
                result.points.clear(); result.cost = 0;
                return result;
            }
            if (hasGoal[current.node] == searchStamp) {
                const float candidate = current.cost + goalSuffix[current.node];
                if (candidate < bestCost || (candidate == bestCost && goalIndex[current.node] < bestGoal)) {
                    bestCost = candidate; bestNode = current.node; bestGoal = goalIndex[current.node];
                }
            }
            visitNeighbors(graph, current.node, ignore, [&](int next, float edgeCost) {
                if (closed[next] == searchStamp) return;
                const float candidate = current.cost + edgeCost;
                if (seen[next] != searchStamp || candidate + 1.0e-5f < costs[next]
                    || (std::fabs(candidate - costs[next]) <= 1.0e-5f && current.node < parents[next])) {
                    seen[next] = searchStamp; costs[next] = candidate; parents[next] = current.node;
                    push({candidate + heuristic(next), candidate, next});
                }
            });
        }
        if (bestNode < 0) return result;

        return completeRoute(graph, from, goals, bestNode, bestGoal, result.expanded, clearance, ignore);
    }

    NavigationResult multiSourceSearch(const Layer& graph, const std::vector<Vec2>& starts,
        const std::vector<Vec2>& goals, const std::vector<Attachment>& allStarts,
        const std::vector<Attachment>& allEnds, float clearance, Id ignore,
        bool adaptiveOnly, int expanded) const
    {
        NavigationResult result;
        result.expanded = expanded;
        std::vector<Attachment> searchStarts, searchEnds;
        searchStarts.reserve(allStarts.size()); searchEnds.reserve(allEnds.size());
        for (const Attachment& start : allStarts)
            if (!adaptiveOnly || start.node >= graph.gridCount) searchStarts.push_back(start);
        for (const Attachment& end : allEnds)
            if (!adaptiveOnly || end.node >= graph.gridCount) searchEnds.push_back(end);
        if (searchStarts.empty() || searchEnds.empty()) return result;

        prepareScratch(graph.nodeCount());
        auto heuristic = [&](int node) {
            float best = NavInfinity;
            const Vec2 point = graph.point(node);
            for (Vec2 goal : goals) best = std::min(best, navDistance(point, goal));
            return best;
        };
        auto push = [&](OpenNode node) {
            open.push_back(node);
            std::push_heap(open.begin(), open.end(), OpenGreater{});
        };
        for (const Attachment& end : searchEnds) {
            if (hasGoal[end.node] != searchStamp || end.cost < goalSuffix[end.node]
                || (end.cost == goalSuffix[end.node] && end.goal < goalIndex[end.node])) {
                hasGoal[end.node] = searchStamp;
                goalSuffix[end.node] = end.cost;
                goalIndex[end.node] = end.goal;
            }
        }
        for (const Attachment& start : searchStarts) {
            if (seen[start.node] != searchStamp || start.cost < costs[start.node]
                || (start.cost == costs[start.node] && start.goal < sourceIndex[start.node])) {
                seen[start.node] = searchStamp;
                costs[start.node] = start.cost;
                parents[start.node] = -1;
                sourceIndex[start.node] = start.goal;
                push({start.cost + heuristic(start.node), start.cost, start.node});
            }
        }

        float bestCost = NavInfinity;
        int bestNode = -1;
        int bestGoal = -1;
        int bestStart = -1;
        while (!open.empty()) {
            std::pop_heap(open.begin(), open.end(), OpenGreater{});
            const OpenNode current = open.back(); open.pop_back();
            if (closed[current.node] == searchStamp || seen[current.node] != searchStamp
                || current.cost != costs[current.node]) continue;
            if (current.score > bestCost) break;
            closed[current.node] = searchStamp;
            ++result.expanded;
            if (result.expanded >= NavMaxExpandedNodes) {
                result.exhausted = true;
                return result;
            }
            if (hasGoal[current.node] == searchStamp) {
                const float candidate = current.cost + goalSuffix[current.node];
                const int candidateStart = sourceIndex[current.node];
                const int candidateGoal = goalIndex[current.node];
                if (candidate < bestCost || (candidate == bestCost
                    && (bestStart < 0 || candidateStart < bestStart
                        || (candidateStart == bestStart && candidateGoal < bestGoal)))) {
                    bestCost = candidate;
                    bestNode = current.node;
                    bestGoal = candidateGoal;
                    bestStart = candidateStart;
                }
            }
            auto relax = [&](int next, float edgeCost) {
                if (closed[next] == searchStamp) return;
                const float candidate = current.cost + edgeCost;
                const int candidateStart = sourceIndex[current.node];
                if (seen[next] != searchStamp || candidate + 1.0e-5f < costs[next]
                    || (std::fabs(candidate - costs[next]) <= 1.0e-5f
                        && (candidateStart < sourceIndex[next]
                            || (candidateStart == sourceIndex[next] && current.node < parents[next])))) {
                    seen[next] = searchStamp;
                    costs[next] = candidate;
                    parents[next] = current.node;
                    sourceIndex[next] = candidateStart;
                    push({candidate + heuristic(next), candidate, next});
                }
            };
            if (adaptiveOnly) {
                const int adaptive = current.node - graph.gridCount;
                for (int nextAdaptive : graph.adaptiveEdges[adaptive]) {
                    const int next = graph.gridCount + nextAdaptive;
                    relax(next, navDistance(graph.point(current.node), graph.point(next)));
                }
            } else visitNeighbors(graph, current.node, ignore, relax);
        }
        if (bestNode < 0) return result;

        NavigationResult routed = completeRoute(graph, starts[bestStart], goals, bestNode,
            bestGoal, result.expanded, clearance, ignore);
        if (routed.reached) routed.origin = starts[bestStart];
        return routed;
    }

    NavigationResult routeFromAny(const std::vector<Vec2>& suppliedStarts,
        const std::vector<Vec2>& suppliedGoals, float clearance, Id ignore) const
    {
        NavigationResult result;
        if (!navFinite(clearance) || clearance < 0 || suppliedStarts.empty()
            || suppliedGoals.empty()) return result;
        if (ignore) {
            const bool present = std::any_of(state->geometry.circles.begin(), state->geometry.circles.end(),
                [&](const NavCircle& circle) { return circle.id == ignore; });
            if (!present) ignore = 0;
        }

        std::vector<Vec2> starts;
        starts.reserve(suppliedStarts.size());
        for (Vec2 start : suppliedStarts) {
            if (!navFinite(start)) continue;
            start.x = navCanonical(start.x); start.y = navCanonical(start.y);
            if (pointClear(start, clearance, ignore)) starts.push_back(start);
        }
        std::sort(starts.begin(), starts.end(), [](Vec2 a, Vec2 b) {
            if (a.x != b.x) return a.x < b.x;
            return a.y < b.y;
        });
        starts.erase(std::unique(starts.begin(), starts.end(), [](Vec2 a, Vec2 b) {
            return navDistanceSquared(a, b) <= 1.0e-6f;
        }), starts.end());
        if (starts.empty()) return result;

        std::vector<Vec2> goals;
        goals.reserve(suppliedGoals.size());
        for (Vec2 goal : suppliedGoals) {
            if (!pointClear(goal, clearance, ignore)) continue;
            bool duplicate = false;
            for (Vec2 existing : goals) if (navDistanceSquared(goal, existing) <= 1.0e-6f) duplicate = true;
            if (!duplicate) goals.push_back(goal);
        }
        if (goals.empty()) return result;

        // A single valid source uses the established route path verbatim.
        if (starts.size() == 1) {
            result = route(starts.front(), goals, clearance, ignore);
            if (result.reached) result.origin = starts.front();
            return result;
        }

        float directCost = NavInfinity;
        int directStart = -1;
        int directGoal = -1;
        for (int start = 0; start < static_cast<int>(starts.size()); ++start) {
            for (int goal = 0; goal < static_cast<int>(goals.size()); ++goal) {
                const float candidate = navDistance(starts[start], goals[goal]);
                if (candidate <= 1.0e-3f) {
                    result.reached = true;
                    result.origin = starts[start];
                    return result;
                }
                if (candidate < directCost
                    && segmentClear(starts[start], goals[goal], clearance, ignore)) {
                    directCost = candidate; directStart = start; directGoal = goal;
                }
            }
        }
        if (directStart >= 0) {
            result.reached = true;
            result.points.push_back(goals[directGoal]);
            result.cost = directCost;
            result.origin = starts[directStart];
            return result;
        }

        const auto cachedLayer = layer(clearance);
        const Layer& graph = *cachedLayer;
        std::vector<Attachment> allStarts;
        for (int start = 0; start < static_cast<int>(starts.size()); ++start) {
            std::vector<Attachment> attached = attachments(graph, starts[start], clearance, ignore, start);
            allStarts.insert(allStarts.end(), attached.begin(), attached.end());
        }
        if (allStarts.empty()) return result;

        std::vector<Attachment> ends;
        for (int goal = 0; goal < static_cast<int>(goals.size()); ++goal) {
            std::vector<Attachment> attached = ignore
                ? attachments(graph, goals[goal], clearance, ignore, goal)
                : cachedGoalAttachments(graph, goals[goal], clearance, goal);
            ends.insert(ends.end(), attached.begin(), attached.end());
        }
        if (ends.empty()) return result;

        NavigationResult adaptive = multiSourceSearch(graph, starts, goals, allStarts, ends,
            clearance, ignore, true, 0);
        if (adaptive.reached || adaptive.exhausted) return adaptive;

        if (!ignore) {
            std::vector<int> startComponents;
            startComponents.reserve(allStarts.size());
            for (const Attachment& start : allStarts) startComponents.push_back(graph.component[start.node]);
            std::sort(startComponents.begin(), startComponents.end());
            startComponents.erase(std::unique(startComponents.begin(), startComponents.end()), startComponents.end());
            ends.erase(std::remove_if(ends.begin(), ends.end(), [&](const Attachment& end) {
                return !std::binary_search(startComponents.begin(), startComponents.end(), graph.component[end.node]);
            }), ends.end());
            if (ends.empty()) return adaptive;
        }

        return multiSourceSearch(graph, starts, goals, allStarts, ends, clearance, ignore,
            false, adaptive.expanded);
    }
};

Navigation::Navigation() : impl_(std::make_unique<Impl>()) {}
Navigation::Navigation(const Navigation& other) : impl_(std::make_unique<Impl>()) { impl_->state = other.impl_->state; }
Navigation::Navigation(Navigation&& other) noexcept = default;
Navigation& Navigation::operator=(const Navigation& other)
{
    if (this != &other) {
        auto replacement = std::make_unique<Impl>();
        replacement->state = other.impl_->state;
        impl_ = std::move(replacement);
    }
    return *this;
}
Navigation& Navigation::operator=(Navigation&& other) noexcept = default;
Navigation::~Navigation() = default;

void Navigation::sync(float worldSize, const std::vector<NavBox>& boxes,
    const std::vector<NavCircle>& circles)
{
    Impl::Geometry geometry = Impl::normalize(worldSize, boxes, circles);
    if (geometry.fingerprint == impl_->state->geometry.fingerprint
        && Impl::sameGeometry(geometry, impl_->state->geometry)) return;
    impl_->state = std::make_shared<Impl::State>();
    impl_->state->geometry = std::move(geometry);
    impl_->clearGoalAttachmentCache();
}

void Navigation::addCircle(NavCircle circle)
{
    if (!navFinite(circle.center) || !navFinite(circle.radius) || circle.radius < 0) return;
    std::vector<NavCircle> circles = impl_->state->geometry.circles;
    bool replaced = false;
    if (circle.id) {
        for (NavCircle& existing : circles) {
            if (existing.id == circle.id) { existing = circle; replaced = true; break; }
        }
    }
    if (!replaced) circles.push_back(circle);
    sync(impl_->state->geometry.worldSize, impl_->state->geometry.boxes, circles);
}

bool Navigation::pointClear(Vec2 point, float clearance, Id ignore) const
{
    return impl_->pointClear(point, clearance, ignore);
}

bool Navigation::segmentClear(Vec2 from, Vec2 to, float clearance, Id ignore) const
{
    return impl_->segmentClear(from, to, clearance, ignore);
}

NavigationResult Navigation::route(Vec2 from, const std::vector<Vec2>& goals,
    float clearance, Id ignore) const
{
    NavigationResult result = impl_->route(from, goals, clearance, ignore);
    if (result.reached) result.origin = from;
    return result;
}

NavigationResult Navigation::routeFromAny(const std::vector<Vec2>& starts,
    const std::vector<Vec2>& goals, float clearance, Id ignore) const
{
    return impl_->routeFromAny(starts, goals, clearance, ignore);
}

std::uint64_t Navigation::geometryVersion() const { return impl_->state->geometry.fingerprint; }

} // namespace cinder
