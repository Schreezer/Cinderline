#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Presentation/CinderLandscapeTerrain.h"
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
        // 120 cm is well above the 89.7 cm this generator's smallest map used to reach and
        // well below the 175 cm it now reaches there. It is the floor at which a plateau
        // casts a silhouette instead of reading as a painted texture at the RTS camera pitch.
        TestTrue(*FString::Printf(TEXT("%s builds relief tall enough to cast a silhouette"), *Label),
            TallestRelief > 120.0f);
        // THIS IS THE PRIMARY CLIFF-HEIGHT CONTRACT, not a backstop. It used to sit behind
        // the 240-340 cm prop mesas that were the actual cliff body, so 1.5x the walkable
        // ceiling (157.5 cm) was a floor nothing ever approached. With the cliffs sculpted
        // into the heightfield this assertion is the only thing holding their height, and
        // 157.5 cm is 90 cm BELOW what the props used to guarantee - a silent half-height
        // regression that would turn nothing red. Pinned to the generator's own floor
        // instead, with margin for BroadVariation: measured worst across all 18 fixtures is
        // 265.0 cm against this 246.4 cm bar.
        //
        // The number that matters for judging it: the tallest building in the game is the
        // Anchor at 185 cm. A cliff must clearly out-rank the base a player parks beside it.
        for (int32 ObstacleIndex = 0; ObstacleIndex < ObstacleCount; ++ObstacleIndex)
            TestTrue(*FString::Printf(TEXT("%s obstacle %d rises far above any walkable relief (%.0f cm)"),
                *Label, ObstacleIndex, ObstaclePeak[ObstacleIndex]),
                ObstaclePeak[ObstacleIndex] >= CliffPeakFloor * 0.88f);
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
                TestEqual(*FString::Printf(TEXT("Map %d Standard canonical signature is stable"), Map),
                    Signature, CanonicalGeometrySignature(Map));
                TestTrue(TEXT("Only exact Standard geometry selects the authored Landscape"),
                    IsCanonicalGeometry(Simulation));
            }
            else TestFalse(TEXT("Nonstandard dimensions use the generated ground fallback"),
                IsCanonicalGeometry(Simulation));

            ValidateHeightfield(Simulation, FString::Printf(TEXT("Map %d %s two-player"), Map,
                ANSI_TO_TCHAR(cinder::matchLengthName(Length))));
        }

        cinder::Config Config;
        Config.map = Map;
        cinder::Simulation Simulation;
        Simulation.reset(Config);
        cinder::net::Snapshot Changed = cinder::net::snapshotFor(Simulation, 0);
        Changed.obstacles[0].half.x += 1.0f;
        cinder::Simulation Custom;
        std::string ApplyError;
        TestTrue(TEXT("Custom geometry snapshot remains structurally valid"), Custom.applySnapshot(Changed, &ApplyError));
        TestFalse(TEXT("A custom obstacle layout rejects the authored Landscape"), IsCanonicalGeometry(Custom));

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
        }
    }
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
