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

            TArray<uint16> Heights;
            BuildHeightData(Simulation, Heights);
            TestEqual(TEXT("Every size emits a 127 by 127 heightfield"),
                Heights.Num(), SamplesPerAxis * SamplesPerAxis);
            const float Spacing = VertexSpacing(Simulation.worldSize());
            TestTrue(TEXT("Heightfield spacing spans the active simulation world"),
                FMath::IsNearlyEqual(Spacing * QuadsPerAxis, Simulation.worldSize(), 0.01f));
            TArray<bool> RaisedObstacle;
            RaisedObstacle.Init(false, static_cast<int32>(Simulation.obstacles().size()));
            for (int32 Y = 0; Y < SamplesPerAxis; ++Y) for (int32 X = 0; X < SamplesPerAxis; ++X)
            {
                const uint16 Height = Heights[Y * SamplesPerAxis + X];
                if (X == 0 || Y == 0 || X == SamplesPerAxis - 1 || Y == SamplesPerAxis - 1)
                    TestEqual(TEXT("Active world boundary remains flat"), Height, FlatHeight);
                if (Height == FlatHeight) continue;
                bool bInsideGuard = false;
                const float WorldX = X * Spacing, WorldY = Y * Spacing;
                for (int32 ObstacleIndex = 0; ObstacleIndex < static_cast<int32>(Simulation.obstacles().size()); ++ObstacleIndex)
                {
                    const cinder::Obstacle& Obstacle = Simulation.obstacles()[ObstacleIndex];
                    if (FMath::Abs(WorldX - Obstacle.center.x) <= Obstacle.half.x - Spacing
                        && FMath::Abs(WorldY - Obstacle.center.y) <= Obstacle.half.y - Spacing)
                    {
                        bInsideGuard = true;
                        RaisedObstacle[ObstacleIndex] = true;
                    }
                }
                TestTrue(TEXT("Every raised vertex keeps one full vertex of flat playable guard"), bInsideGuard);
            }
            for (int32 ObstacleIndex = 0; ObstacleIndex < RaisedObstacle.Num(); ++ObstacleIndex)
                TestTrue(*FString::Printf(TEXT("Map %d %s obstacle %d receives generated height"), Map,
                    ANSI_TO_TCHAR(cinder::matchLengthName(Length)), ObstacleIndex), RaisedObstacle[ObstacleIndex]);
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

        cinder::Config FourPlayerConfig;
        FourPlayerConfig.map = Map;
        FourPlayerConfig.playerCount = cinder::Simulation::MaxPlayers;
        cinder::Simulation FourPlayerSimulation;
        FourPlayerSimulation.reset(FourPlayerConfig);
        TestNotEqual(TEXT("Four-player obstacle geometry differs from the authored two-player Landscape"),
            GeometrySignature(FourPlayerSimulation), CanonicalGeometrySignature(Map));
        TestFalse(TEXT("Four-player matches select the generated-ground fallback"),
            IsCanonicalGeometry(FourPlayerSimulation));
    }
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
