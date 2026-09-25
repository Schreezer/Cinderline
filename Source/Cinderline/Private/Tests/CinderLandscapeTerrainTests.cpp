#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Presentation/CinderLandscapeTerrain.h"
#include "Sim/MapDefinition.h"
#include "Sim/Network.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderLandscapeHeightGeometryTest,
    "Cinderline.Presentation.LandscapeHeightGeometry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderLandscapeHeightGeometryTest::RunTest(const FString& Parameters)
{
    using namespace CinderLandscapeTerrain;

    // One heightfield, every geometric invariant it has to satisfy.
    //
    // THE CONTRACT THIS REPLACES: until the rolling relief landed, this test asserted that
    // every non-flat vertex lay inside an obstacle - "every raised vertex keeps one full
    // vertex of flat playable guard". That was trivially satisfiable by a plane with four
    // mesas on it, and it is exactly the assertion the terrain work removes. It is replaced
    // below by a set that is strictly stronger, because each one now has to hold over the
    // whole map instead of over the 5% of it that used to be allowed to have shape at all.
    auto ValidateHeightfield = [this](const cinder::Simulation& Simulation, const FString& Label)
    {
        TArray<uint16> Heights;
        BuildHeightData(Simulation, Heights);
        TestEqual(*FString::Printf(TEXT("%s emits a 127 by 127 heightfield"), *Label),
            Heights.Num(), SamplesPerAxis * SamplesPerAxis);
        const float Spacing = VertexSpacing(Simulation.worldSize());
        TestTrue(*FString::Printf(TEXT("%s heightfield spacing spans the active simulation world"), *Label),
            FMath::IsNearlyEqual(Spacing * QuadsPerAxis, Simulation.worldSize(), 0.01f));

        TArray<TPair<cinder::Vec2, cinder::Vec2>> PaintedRoutes;
        GatherPathways(Simulation, PaintedRoutes);
        TestEqual(*FString::Printf(TEXT("%s paint uses every authored route segment"), *Label),
            PaintedRoutes.Num() * 3, PathwaySampleCount(Simulation));
        for (int32 RouteIndex = 0; RouteIndex < PaintedRoutes.Num(); ++RouteIndex)
        {
            float MidX = 0, MidY = 0;
            PathwaySample(Simulation, RouteIndex * 3 + 1, MidX, MidY);
            const auto& Segment = PaintedRoutes[RouteIndex];
            TestTrue(*FString::Printf(TEXT("%s route paint follows flattened terrain"), *Label),
                FMath::IsNearlyEqual(MidX, (Segment.Key.x + Segment.Value.x) * 0.5f, 0.01f)
                && FMath::IsNearlyEqual(MidY, (Segment.Key.y + Segment.Value.y) * 0.5f, 0.01f));
        }

        const int32 ObstacleCount = static_cast<int32>(Simulation.obstacles().size());
        int32 AuthoredBodies = 0;
        for (int32 ObstacleIndex = 0; ObstacleIndex < ObstacleCount; ++ObstacleIndex)
            if (UsesAuthoredCliffBody(Simulation, ObstacleIndex)) ++AuthoredBodies;
        TestEqual(*FString::Printf(TEXT("%s supplies authored bodies for every canonical obstacle"), *Label),
            AuthoredBodies, ObstacleCount);
        TestFalse(TEXT("A negative obstacle index cannot opt into authored geometry"),
            UsesAuthoredCliffBody(Simulation, -1));
        TestFalse(TEXT("An absent obstacle index cannot opt into authored geometry"),
            UsesAuthoredCliffBody(Simulation, ObstacleCount));
        // An "interior" sample is one the cliff generator is allowed to raise: strictly inside
        // an obstacle rectangle by one full vertex. This is the same predicate HeightAt uses
        // to gate its terracing, and it is the line that separates impassable relief from
        // relief a unit walks straight over, so every assertion below is stated relative to it.
        TArray<bool> Interior;
        Interior.Init(false, SamplesPerAxis * SamplesPerAxis);
        TArray<float> Relief;
        Relief.Init(0.0f, SamplesPerAxis * SamplesPerAxis);
        TArray<float> ObstaclePeak;
        ObstaclePeak.Init(0.0f, ObstacleCount);

        bool bEncodable = true, bGeneratorAgrees = true;
        float TallestRelief = 0.0f, TallestWalkable = 0.0f;
        double WalkableSum = 0.0, WalkableSquares = 0.0;
        int32 WalkableCount = 0, UndulatingCount = 0;
        for (int32 Y = 0; Y < SamplesPerAxis; ++Y) for (int32 X = 0; X < SamplesPerAxis; ++X)
        {
            const int32 Sample = Y * SamplesPerAxis + X;
            const uint16 Height = Heights[Sample];
            // Heights ride in a uint16 as FlatHeight + Relief * HeightEncodeScale. A sample
            // that touches either end of the range means the encoding saturated or wrapped,
            // and nothing downstream reports that: the landscape simply renders its plateau
            // tops as inverted pits. This is the one assertion that catches a relief ceiling
            // raised without the matching encode scale, which is exactly how the 480 cm peaks
            // would have shipped as garbage under the old scale of 128 (255.9 cm ceiling).
            // It matters more now that relief is signed and the low ground encodes BELOW
            // FlatHeight: the bottom of the range is live geometry rather than dead headroom.
            bEncodable &= Height > 0 && Height < 65535;
            const float Value = (static_cast<float>(Height) - static_cast<float>(FlatHeight))
                / HeightEncodeScale;
            Relief[Sample] = Value;
            TallestRelief = FMath::Max(TallestRelief, Value);
            // HeightAt is what other presentation systems seat props against, so it must be
            // the same generator the landscape renders, not an approximation of it.
            bGeneratorAgrees &= Height == static_cast<uint16>(FlatHeight
                + FMath::RoundToInt(HeightAt(Simulation, X * Spacing, Y * Spacing) * HeightEncodeScale));
            if (X == 0 || Y == 0 || X == SamplesPerAxis - 1 || Y == SamplesPerAxis - 1)
                TestEqual(*FString::Printf(TEXT("%s active world boundary remains flat"), *Label),
                    Height, FlatHeight);
            if (Height != FlatHeight) ++UndulatingCount;

            const float WorldX = X * Spacing, WorldY = Y * Spacing;
            for (int32 ObstacleIndex = 0; ObstacleIndex < ObstacleCount; ++ObstacleIndex)
            {
                const cinder::Obstacle& Obstacle = Simulation.obstacles()[ObstacleIndex];
                // WRITTEN IN THE GENERATOR'S EXACT EXPRESSION FORM, DELIBERATELY. HeightAt
                // computes `half - |d|` and compares that to Spacing; stating the same test
                // as `|d| <= half - Spacing` is algebraically identical but rounds
                // differently, and map 2's obstacle edges land exactly on vertices at every
                // match length (2p map 2 Long: edge y = 4000, vertex 84 * 6000/126 = 4000.0
                // exactly). A disagreement at that tie puts a vertex the generator treats as
                // guard band into the test's walkable set, where a 120 cm toe reads as a 1.84
                // gradient against a 0.27 ceiling. Keep these two expressions character for
                // character the same shape.
                const float InsetX = Obstacle.half.x - FMath::Abs(WorldX - Obstacle.center.x);
                const float InsetY = Obstacle.half.y - FMath::Abs(WorldY - Obstacle.center.y);
                if (FMath::Min(InsetX, InsetY) >= Spacing)
                {
                    Interior[Sample] = true;
                    ObstaclePeak[ObstacleIndex] = FMath::Max(ObstaclePeak[ObstacleIndex], Value);
                }
            }
            if (Interior[Sample]) continue;
            TallestWalkable = FMath::Max(TallestWalkable, FMath::Abs(Value));
            WalkableSum += Value;
            WalkableSquares += static_cast<double>(Value) * Value;
            ++WalkableCount;
        }

        // NEW - the assertion the old "every raised vertex is inside an obstacle" becomes.
        // Outside the obstacle interiors there is nothing in the simulation that stops a unit:
        // cinder::Vec2 is {x, y} with no elevation and no slope, so this relief is walked
        // straight over. Capping it is therefore a correctness requirement, not a stylistic
        // one - terrain that looks like it blocks but does not is worse than flat ground.
        // Catches: anyone raising the hill amplitudes or a trough depth until the open field
        // grows something a player would read as a barrier and path around for no reason.
        TestTrue(*FString::Printf(TEXT("%s keeps every walkable sample under the walkable relief ceiling (%.1f of %.1f cm)"),
            *Label, TallestWalkable, WalkableRelief), TallestWalkable <= WalkableRelief);
        // ...and comfortably under it, not pressed against it. HeightAt clamps to
        // WalkableRelief as a last-resort guard for off-grid prop seating; if the clamp were
        // doing real work the assertion above would pass while the map quietly grew flat-topped
        // mesas wherever the noise saturated. Catches a generator tuned past its own ceiling.
        TestTrue(*FString::Printf(TEXT("%s reaches its walkable relief naturally rather than by clamping"), *Label),
            TallestWalkable < WalkableRelief - 8.0f);

        // NEW - the real guarantee. Relief that is shallow on average can still contain a
        // single step that reads as a wall, and a step is what a player actually reacts to.
        // Measured in both axes, over every adjacent pair where BOTH samples are walkable;
        // pairs straddling an obstacle edge are excluded because that step IS a cliff and is
        // supposed to be abrupt. Catches: a trough or worn route whose shoulder was narrowed
        // without reducing its depth, which is the single easiest way to break this terrain.
        float SteepestGradient = 0.0f;
        for (int32 Y = 0; Y < SamplesPerAxis; ++Y) for (int32 X = 0; X + 1 < SamplesPerAxis; ++X)
        {
            const int32 Sample = Y * SamplesPerAxis + X;
            if (Interior[Sample] || Interior[Sample + 1]) continue;
            SteepestGradient = FMath::Max(SteepestGradient,
                FMath::Abs(Relief[Sample + 1] - Relief[Sample]) / Spacing);
        }
        for (int32 Y = 0; Y + 1 < SamplesPerAxis; ++Y) for (int32 X = 0; X < SamplesPerAxis; ++X)
        {
            const int32 Sample = Y * SamplesPerAxis + X;
            if (Interior[Sample] || Interior[Sample + SamplesPerAxis]) continue;
            SteepestGradient = FMath::Max(SteepestGradient,
                FMath::Abs(Relief[Sample + SamplesPerAxis] - Relief[Sample]) / Spacing);
        }
        TestTrue(*FString::Printf(TEXT("%s keeps every walkable step under the walkable gradient (%.3f of %.3f)"),
            *Label, SteepestGradient, WalkableGradient), SteepestGradient < WalkableGradient);

        // NEW - the map is actually undulating rather than a plane. Without this the whole
        // feature could silently regress to flat ground - a stray early-out, a feather radius
        // grown until it covers the map, an amplitude typo'd to zero - and every other
        // assertion here would still pass, because a plane satisfies every ceiling trivially.
        // 60% is far below the 93% the generator delivers and far above the 5% the old
        // obstacle-only heightfield reached, so it cannot be met by mesas alone.
        const float UndulatingFraction = static_cast<float>(UndulatingCount)
            / static_cast<float>(SamplesPerAxis * SamplesPerAxis);
        TestTrue(*FString::Printf(TEXT("%s undulates across the map rather than only at obstacles (%.0f%%)"),
            *Label, UndulatingFraction * 100.0f), UndulatingFraction > 0.60f);
        // The fraction alone would accept a map shaped entirely by rounding noise, so pair it
        // with the spread: relief has to have real magnitude, not just be non-zero. 2.5 cm
        // against the 3.6 cm the flattest map and match length produces.
        const double WalkableMean = WalkableCount > 0 ? WalkableSum / WalkableCount : 0.0;
        const double WalkableVariance = WalkableCount > 0
            ? WalkableSquares / WalkableCount - WalkableMean * WalkableMean : 0.0;
        const float WalkableDeviation = FMath::Sqrt(static_cast<float>(FMath::Max(0.0, WalkableVariance)));
        TestTrue(*FString::Printf(TEXT("%s walkable relief carries a non-trivial spread (%.2f cm)"),
            *Label, WalkableDeviation), WalkableDeviation > 2.5f);

        // NEW - worn routes are the flattest ground on the map. This is the gameplay
        // readability guarantee: players march and build along these lines, and a flat
        // building footprint must never end up sitting on a visible slope. Sampled on the
        // real authored polylines rather than on guessed coordinates, so a route re-authored
        // into the side of a hill fails here. Catches: pathway damping weakened, a shoulder
        // narrowed, or a route moved somewhere the field is steep.
        const int32 PathwaySamples = PathwaySampleCount(Simulation);
        TestTrue(*FString::Printf(TEXT("%s authors worn routes at all"), *Label), PathwaySamples > 0);
        float SteepestPathway = 0.0f;
        for (int32 Index = 0; Index < PathwaySamples; ++Index)
        {
            float RouteX = 0.0f, RouteY = 0.0f;
            PathwaySample(Simulation, Index, RouteX, RouteY);
            const float GradientX = FMath::Abs(HeightAt(Simulation, RouteX + Spacing, RouteY)
                - HeightAt(Simulation, RouteX - Spacing, RouteY)) / (2.0f * Spacing);
            const float GradientY = FMath::Abs(HeightAt(Simulation, RouteX, RouteY + Spacing)
                - HeightAt(Simulation, RouteX, RouteY - Spacing)) / (2.0f * Spacing);
            SteepestPathway = FMath::Max(SteepestPathway, FMath::Max(GradientX, GradientY));
        }
        TestTrue(*FString::Printf(TEXT("%s keeps worn routes flatter than the open field (%.3f of %.3f)"),
            *Label, SteepestPathway, PathwayGradient), SteepestPathway < PathwayGradient);

        TestTrue(*FString::Printf(TEXT("%s encodes every sample strictly inside the uint16 range"), *Label),
            bEncodable);
        TestTrue(*FString::Printf(TEXT("%s heightfield matches the HeightAt generator exactly"), *Label),
            bGeneratorAgrees);
        // MaxRelief itself is pinned below MaxEncodableHeight by a static_assert in
        // BuildHeightData; this only checks the generator honours its own ceiling.
        TestTrue(*FString::Printf(TEXT("%s relief stays under the encodable ceiling"), *Label),
            TallestRelief <= MaxRelief);
        // Meshes now supply every canonical cliff silhouette. A remaining tall mesa
        // would stack underneath them; SceneryObstacleBounds checks the actual bodies.
        TestTrue(*FString::Printf(TEXT("%s contains only walkable relief and low rock support"), *Label),
            TallestRelief <= WalkableRelief);
        for (int32 ObstacleIndex = 0; ObstacleIndex < ObstacleCount; ++ObstacleIndex)
        {
            const cinder::Obstacle& Obstacle = Simulation.obstacles()[ObstacleIndex];
            TestEqual(*FString::Printf(TEXT("%s obstacle %d reaches its low core without retaining a tall mesa"),
                *Label, ObstacleIndex), ObstaclePeak[ObstacleIndex], AuthoredCliffFoundationHeight);
            TestTrue(TEXT("Authored rock support remains below ordinary walkable relief"),
                AuthoredCliffFoundationHeight < WalkableRelief);
            for (int32 Axis = 0; Axis < 2; ++Axis)
            for (float Sign : {-1.0f, 1.0f})
            {
                float PreviousSupport = 0.0f;
                for (int32 Step = 0; Step <= 24; ++Step)
                {
                    const float Inset = Spacing * 2.5f * Step / 24.0f;
                    const float X = Obstacle.center.x + (Axis == 0 ? Sign * (Obstacle.half.x - Inset) : 0.0f);
                    const float Y = Obstacle.center.y + (Axis == 1 ? Sign * (Obstacle.half.y - Inset) : 0.0f);
                    const float Support = HeightAt(Simulation, X, Y);
                    TestTrue(TEXT("Authored support rises continuously inward without an exposed tall step"),
                        Support >= PreviousSupport - 0.001f
                        && Support - PreviousSupport <= AuthoredCliffFoundationHeight * 0.11f
                        && Support >= 0.0f && Support <= AuthoredCliffFoundationHeight);
                    if (Inset < Spacing)
                        TestEqual(TEXT("Every authored support edge retains one full flat vertex guard"), Support, 0.0f);
                    if (Step == 24)
                        TestTrue(TEXT("Authored support reaches its full core within two and a half quads"),
                            FMath::IsNearlyEqual(Support, AuthoredCliffFoundationHeight, 0.001f));
                    PreviousSupport = Support;
                }
            }

            for (bool bRenderedRelief : {false, true})
            {
                constexpr float HullRadius = 52.0f;
                TestTrue(TEXT("Aircraft clear mesh bodies in either rendered terrain mode"),
                    VisualObstacleTopAt(Simulation, Obstacle.center.x, Obstacle.center.y,
                        bRenderedRelief, HullRadius) >= AuthoredCliffBodyMaxHeight);
                TestTrue(TEXT("Aircraft already clear the cliff before their hull crosses its boundary"),
                    VisualObstacleTopAt(Simulation, Obstacle.center.x - Obstacle.half.x - HullRadius,
                        Obstacle.center.y, bRenderedRelief, HullRadius) >= AuthoredCliffBodyMaxHeight - 0.001f);
            }
        }

        // Neighboring obstacle shoulders may overlap. Their maximum must remain
        // bounded, and canonical rock bodies must not change height with terrain mode.
        bool bAirEnvelopeBounded = true, bAirModesAgree = true;
        for (int32 Y = 0; Y <= 32; ++Y)
        for (int32 X = 0; X <= 32; ++X)
        {
            const float WorldX = Simulation.worldSize() * X / 32.0f;
            const float WorldY = Simulation.worldSize() * Y / 32.0f;
            const float FlatTop = VisualObstacleTopAt(Simulation, WorldX, WorldY, false, 52.0f);
            const float ReliefTop = VisualObstacleTopAt(Simulation, WorldX, WorldY, true, 52.0f);
            bAirEnvelopeBounded &= FMath::IsFinite(ReliefTop) && ReliefTop >= 0.0f
                && ReliefTop <= AuthoredCliffBodyMaxHeight;
            bAirModesAgree &= FMath::IsNearlyEqual(FlatTop, ReliefTop, 0.001f);
        }
        TestTrue(TEXT("Overlapping aircraft envelopes never add together or exceed the rock ceiling"), bAirEnvelopeBounded);
        TestTrue(TEXT("Canonical aircraft clearance is identical over flat ground and low landscape support"), bAirModesAgree);

        // Isolate a canonical rectangle without changing its index. Testing a descent
        // against the complete map incorrectly fails when another cliff takes over.
        auto IsolatedSnapshot = cinder::net::snapshotFor(Simulation, 0);
        IsolatedSnapshot.obstacles.resize(1);
        cinder::Simulation Isolated;
        if (TestTrue(TEXT("An isolated canonical rectangle remains a valid snapshot"),
            Isolated.applySnapshot(IsolatedSnapshot)))
        {
            TestTrue(TEXT("Removing later obstacles does not change the first rectangle's body policy"),
                UsesAuthoredCliffBody(Isolated, 0));
            const cinder::Obstacle& Obstacle = Isolated.obstacles().front();
            const float Shoulder = 520.0f * Simulation.worldSize() / cinder::Simulation::WorldSize;
            constexpr float HullRadius = 52.0f;
            for (bool bRenderedRelief : {false, true})
            for (int32 Axis = 0; Axis < 2; ++Axis)
            for (float Sign : {-1.0f, 1.0f})
            {
                float PreviousTop = AuthoredCliffBodyMaxHeight;
                for (int32 Step = 0; Step <= 32; ++Step)
                {
                    const float Outside = HullRadius + Shoulder * Step / 32.0f;
                    const float X = Obstacle.center.x + (Axis == 0 ? Sign * (Obstacle.half.x + Outside) : 0.0f);
                    const float Y = Obstacle.center.y + (Axis == 1 ? Sign * (Obstacle.half.y + Outside) : 0.0f);
                    const float Top = VisualObstacleTopAt(Isolated, X, Y, bRenderedRelief, HullRadius);
                    TestTrue(TEXT("An isolated aircraft shoulder descends continuously instead of popping"),
                        Top >= 0.0f && Top <= PreviousTop + 0.001f
                        && PreviousTop - Top <= AuthoredCliffBodyMaxHeight * 0.055f);
                    PreviousTop = Top;
                }
                TestTrue(TEXT("An isolated aircraft envelope ends beyond its broad approach shoulder"),
                    PreviousTop < 0.001f);
            }
        }
        TestEqual(TEXT("Aircraft obstacle clearance does not raise distant open ground"),
            VisualObstacleTopAt(Simulation, 0.0f, 0.0f, true, 52.0f), 0.0f);
    };

    auto ValidateCustomCliffPolicy = [this](const cinder::Simulation& Simulation)
    {
        const auto Canonical = cinder::net::snapshotFor(Simulation, 0);
        const int32 Count = static_cast<int32>(Canonical.obstacles.size());
        for (int32 Index = 0; Index < Count; ++Index)
        for (int32 Coordinate = 0; Coordinate < 4; ++Coordinate)
        {
            auto Changed = Canonical;
            cinder::Obstacle& ChangedObstacle = Changed.obstacles[Index];
            float* Coordinates[] = {&ChangedObstacle.center.x, &ChangedObstacle.center.y,
                &ChangedObstacle.half.x, &ChangedObstacle.half.y};
            *Coordinates[Coordinate] += 1.0f;
            cinder::Simulation Custom;
            if (!TestTrue(TEXT("A changed canonical rectangle remains a valid custom snapshot"),
                Custom.applySnapshot(Changed))) continue;
            TestFalse(TEXT("Custom dimensions reject a stale authored Landscape"), IsCanonicalGeometry(Custom));
            for (int32 Peer = 0; Peer < Count; ++Peer)
                TestEqual(TEXT("Only the changed indexed rectangle loses authored body support"),
                    UsesAuthoredCliffBody(Custom, Peer), Peer != Index);
            const cinder::Obstacle& Obstacle = Changed.obstacles[Index];
            TestTrue(TEXT("A custom mismatch retains a tall legacy support instead of a hidden low core"),
                HeightAt(Custom, Obstacle.center.x, Obstacle.center.y) >= CliffPeakFloor * 0.88f);
            const float LegacyTop = FMath::Clamp(FMath::Min(Obstacle.half.x, Obstacle.half.y) * 2.0f,
                CliffPeakFloor, MaxRelief) + 120.0f;
            TestTrue(TEXT("Custom aircraft clearance still covers legacy landscape cap dressing"),
                VisualObstacleTopAt(Custom, Obstacle.center.x, Obstacle.center.y, true) >= LegacyTop);
            TestEqual(TEXT("Custom flat fallback uses the mesh ceiling instead of nonexistent relief"),
                VisualObstacleTopAt(Custom, Obstacle.center.x, Obstacle.center.y, false), AuthoredCliffBodyMaxHeight);
        }

        auto Reordered = Canonical;
        const cinder::Obstacle First = Reordered.obstacles[0];
        Reordered.obstacles[0] = Reordered.obstacles[1];
        Reordered.obstacles[1] = First;
        cinder::Simulation Custom;
        if (TestTrue(TEXT("Reordered obstacle geometry remains a valid snapshot"), Custom.applySnapshot(Reordered)))
        {
            TestFalse(TEXT("A canonical rectangle in a different slot cannot opt in"), UsesAuthoredCliffBody(Custom, 0));
            TestFalse(TEXT("Both reordered slots fall back safely"), UsesAuthoredCliffBody(Custom, 1));
        }
        auto Extended = Canonical;
        Extended.obstacles.push_back(First);
        if (TestTrue(TEXT("An additional obstacle remains a valid custom snapshot"), Custom.applySnapshot(Extended)))
            TestFalse(TEXT("An extra rectangle cannot read beyond the authored policy table"),
                UsesAuthoredCliffBody(Custom, Count));
    };

    TSet<uint64> Signatures;
    for (int32 Map = 0; Map < MapCount; ++Map)
    {
        TSet<uint64> SizeSignatures;
        for (cinder::MatchLength Length : {cinder::MatchLength::Short,
            cinder::MatchLength::Standard, cinder::MatchLength::Long})
        {
            cinder::Config Config;
            Config.map = Map;
            Config.matchLength = Length;
            Config.mapRevision = 0; // Exercise the original decorative-relief contract.
            cinder::Simulation Simulation;
            Simulation.reset(Config);
            const uint64 Signature = GeometrySignature(Simulation);
            const bool bStandard = Length == cinder::MatchLength::Standard;
            TestFalse(*FString::Printf(TEXT("Map %d size %s has a distinct scene signature"), Map,
                ANSI_TO_TCHAR(cinder::matchLengthName(Length))), Signatures.Contains(Signature));
            TestFalse(TEXT("Each size on one map has a distinct scene signature"), SizeSignatures.Contains(Signature));
            Signatures.Add(Signature);
            SizeSignatures.Add(Signature);
            if (bStandard)
            {
                TestNotEqual(*FString::Printf(TEXT("Map %d legacy revision cannot reuse a current Landscape bake"), Map),
                    Signature, CanonicalGeometrySignature(Map));
                TestFalse(TEXT("Legacy Standard saves select their generated terrain fallback"),
                    IsCanonicalGeometry(Simulation));
            }
            else TestFalse(TEXT("Nonstandard dimensions use the generated ground fallback"),
                IsCanonicalGeometry(Simulation));

            ValidateHeightfield(Simulation, FString::Printf(TEXT("Map %d %s two-player"), Map,
                ANSI_TO_TCHAR(cinder::matchLengthName(Length))));
            ValidateCustomCliffPolicy(Simulation);
        }

        // Four-player matches author a completely different obstacle layout, so they select a
        // different worn-route set. That route data would otherwise never be exercised: an
        // unchecked four-player route could run straight through a mesa and only show up as a
        // road climbing a cliff face in a screenshot. Every geometric invariant is re-run.
        for (cinder::MatchLength Length : {cinder::MatchLength::Short,
            cinder::MatchLength::Standard, cinder::MatchLength::Long})
        {
            cinder::Config FourPlayerConfig;
            FourPlayerConfig.map = Map;
            FourPlayerConfig.matchLength = Length;
            FourPlayerConfig.playerCount = cinder::Simulation::MaxPlayers;
            FourPlayerConfig.mapRevision = 0;
            cinder::Simulation FourPlayerSimulation;
            FourPlayerSimulation.reset(FourPlayerConfig);
            if (Length == cinder::MatchLength::Standard)
            {
                TestNotEqual(TEXT("Four-player obstacle geometry differs from the authored two-player Landscape"),
                    GeometrySignature(FourPlayerSimulation), CanonicalGeometrySignature(Map));
                TestFalse(TEXT("Four-player matches select the generated-ground fallback"),
                    IsCanonicalGeometry(FourPlayerSimulation));
            }
            ValidateHeightfield(FourPlayerSimulation, FString::Printf(TEXT("Map %d %s four-player"), Map,
                ANSI_TO_TCHAR(cinder::matchLengthName(Length))));
            ValidateCustomCliffPolicy(FourPlayerSimulation);
        }

        cinder::Config CurrentConfig;
        CurrentConfig.map = Map;
        CurrentConfig.mapRevision = cinder::CurrentMapRevision;
        cinder::Simulation CurrentSimulation;
        CurrentSimulation.reset(CurrentConfig);
        TestEqual(*FString::Printf(TEXT("Map %d current Standard canonical signature is stable"), Map),
            GeometrySignature(CurrentSimulation), CanonicalGeometrySignature(Map));
        TestTrue(TEXT("Current Standard geometry selects the current authored Landscape bake"),
            IsCanonicalGeometry(CurrentSimulation));
        for (int32 ObstacleIndex = 0; ObstacleIndex < static_cast<int32>(CurrentSimulation.obstacles().size()); ++ObstacleIndex)
            TestTrue(TEXT("Current canonical obstacle indices share the authoritative body policy"),
                UsesAuthoredCliffBody(CurrentSimulation, ObstacleIndex));
    }

    // The new map's raised ground is playable, authoritative terrain. It must not
    // inherit the <105 cm decorative-relief assertions above, or acquire tall
    // rock support below retaining walls just because those walls block movement.
    cinder::Simulation Authored;
    cinder::Config AuthoredConfig;
    AuthoredConfig.ai = false;
    AuthoredConfig.mapRevision = cinder::CurrentMapRevision;
    Authored.reset(AuthoredConfig);
    const auto& Map = cinder::mapDefinition(0, 2, cinder::MatchLength::Standard,
        cinder::CurrentMapRevision);
    TestTrue(TEXT("Authored terrain fixture uses the new shared plateau/ramp geometry"), Authored.usesAuthoredTerrain());
    TArray<uint16> AuthoredHeights;
    BuildHeightData(Authored, AuthoredHeights);
    const float Spacing = VertexSpacing(Authored.worldSize());
    auto DecodedHeight = [&](int32 X, int32 Y)
    {
        return (static_cast<float>(AuthoredHeights[Y * SamplesPerAxis + X])
            - static_cast<float>(FlatHeight)) / HeightEncodeScale;
    };
    auto BilinearHeight = [&](float X, float Y)
    {
        const float GridX = FMath::Clamp(X / Spacing, 0.0f, static_cast<float>(QuadsPerAxis));
        const float GridY = FMath::Clamp(Y / Spacing, 0.0f, static_cast<float>(QuadsPerAxis));
        const int32 X0 = FMath::Min(FMath::FloorToInt(GridX), QuadsPerAxis - 1);
        const int32 Y0 = FMath::Min(FMath::FloorToInt(GridY), QuadsPerAxis - 1);
        return FMath::Lerp(FMath::Lerp(DecodedHeight(X0, Y0), DecodedHeight(X0 + 1, Y0), GridX - X0),
            FMath::Lerp(DecodedHeight(X0, Y0 + 1), DecodedHeight(X0 + 1, Y0 + 1), GridX - X0), GridY - Y0);
    };
    bool bAuthoredSamplesAgree = true, bAuthoredRangeSafe = true;
    for (int32 Y = 0; Y < SamplesPerAxis; ++Y) for (int32 X = 0; X < SamplesPerAxis; ++X)
    {
        const float Generated = HeightAt(Authored, X * Spacing, Y * Spacing);
        bAuthoredSamplesAgree &= FMath::IsNearlyEqual(DecodedHeight(X, Y), Generated, 1.0f / HeightEncodeScale);
        bAuthoredRangeSafe &= Generated >= 0.0f && Generated <= 180.001f
            && AuthoredHeights[Y * SamplesPerAxis + X] > 0 && AuthoredHeights[Y * SamplesPerAxis + X] < 65535;
    }
    TestTrue(TEXT("Authored heightfield rasterizes its exact generator without clipping"), bAuthoredSamplesAgree && bAuthoredRangeSafe);
    for (const auto& Site : Map.sites)
    {
        const float Expected = Site.role == cinder::MapSiteRole::Home ? 180.0f : 0.0f;
        TestEqual(TEXT("Economy pads use shared playable terrain height"), HeightAt(Authored, Site.center.x, Site.center.y), Expected);
        TestTrue(TEXT("Rasterized economy pads agree with gameplay height"),
            FMath::IsNearlyEqual(BilinearHeight(Site.center.x, Site.center.y), Expected, 0.02f));
    }
    for (const auto& Ramp : Map.ramps)
    {
        const float Length = FMath::Sqrt(FMath::Square(Ramp.low.x - Ramp.high.x) + FMath::Square(Ramp.low.y - Ramp.high.y));
        const float ExpectedGradient = (Ramp.highHeight - Ramp.lowHeight) / Length;
        for (int32 Sample = 0; Sample <= 20; ++Sample)
        {
            const float Alpha = static_cast<float>(Sample) / 20.0f;
            const float X = FMath::Lerp(Ramp.high.x, Ramp.low.x, Alpha);
            const float Y = FMath::Lerp(Ramp.high.y, Ramp.low.y, Alpha);
            const float Expected = FMath::Lerp(Ramp.highHeight, Ramp.lowHeight, Alpha);
            TestTrue(TEXT("Presentation ramp height agrees with the authoritative continuous slope"),
                FMath::IsNearlyEqual(HeightAt(Authored, X, Y), Expected, 0.001f));
            // Away from endpoint quads a bilinear heightfield reproduces a linear
            // ramp exactly, including lanes beside its centerline.
            if (Sample >= 2 && Sample <= 18)
            {
                for (float Across : {-120.0f, 0.0f, 120.0f})
                    TestTrue(TEXT("Rasterized ramp lanes remain continuous and agree with unit seating"),
                        FMath::IsNearlyEqual(BilinearHeight(X, Y + Across), Expected, 0.04f));
                const float Before = BilinearHeight(X - 1.0f, Y);
                const float After = BilinearHeight(X + 1.0f, Y);
                TestTrue(TEXT("Rasterized ramp gradient preserves the authored rise/run"),
                    FMath::IsNearlyEqual(FMath::Abs(After - Before) * 0.5f, ExpectedGradient, 0.002f));
            }
        }
    }
    for (size_t Index = 0; Index < Map.obstacles.size(); ++Index)
    {
        const auto& Obstacle = Map.obstacles[Index];
        const float Generated = HeightAt(Authored, Obstacle.center.x, Obstacle.center.y);
        if (Map.obstacleKinds[Index] == cinder::MapObstacleKind::RockMass)
        {
            TestTrue(TEXT("Authored rock masses keep only their low buried support"),
                Generated > 0.0f && Generated <= AuthoredCliffFoundationHeight);
            for (float Sign : {-1.0f, 1.0f})
                TestTrue(TEXT("Rock support cannot leak through its bilinear boundary guard"),
                    FMath::IsNearlyZero(BilinearHeight(Obstacle.center.x,
                        Obstacle.center.y + Sign * (Obstacle.half.y + 1.0f)), 0.02f));
        }
        else
            TestEqual(TEXT("Retaining walls and perimeter never acquire a false rock mesa"),
                Generated, Map.terrainHeight(Obstacle.center));
    }
    cinder::Navigation Walkable;
    Walkable.sync(Map.worldSize, Map.obstacles, {});
    bool bWalkableFlatQuadsAgree = true;
    for (float Y = 211.0f; Y < 4600.0f; Y += 37.0f)
    for (float X = 213.0f; X < 4600.0f; X += 41.0f)
    {
        const cinder::Vec2 Point{X, Y};
        if (!Walkable.pointClear(Point, 16.0f) || Map.onRamp(Point, Spacing * 2.0f)) continue;
        bWalkableFlatQuadsAgree &= FMath::IsNearlyEqual(BilinearHeight(X, Y), Map.terrainHeight(Point), 0.04f);
    }
    TestTrue(TEXT("Cliff transitions stay inside wall footprints after bilinear filtering; walkable flat ground stays exact"),
        bWalkableFlatQuadsAgree);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
