#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "Presentation/CinderBattlefield.h"
#include "Presentation/CinderFogMask.h"
#include "Presentation/CinderLandscapeTerrain.h"
#include "Presentation/CinderTerrainPicking.h"
#include "Sim/MapDefinition.h"
#include "Tests/AutomationCommon.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderTerrainPickingTest,
    "Cinderline.Presentation.TerrainPicking",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderTerrainPickingTest::RunTest(const FString& Parameters)
{
    using CinderTerrainPicking::Point3;
    const double Minimum = CinderLandscapeTerrain::BaselineZ - CinderLandscapeTerrain::WalkableRelief;
    const double Maximum = CinderLandscapeTerrain::BaselineZ + CinderLandscapeTerrain::MaxRelief
        + CinderLandscapeTerrain::WalkableRelief;
    const FVector CameraForward = FRotator(-54, -45, 0).Vector();
    const Point3 Direction{CameraForward.X, CameraForward.Y, CameraForward.Z};
    const auto RoundTrip = [&](cinder::Vec2 Target, const TCHAR* Label, auto&& HeightAt)
    {
        const double ExpectedZ = HeightAt(Target.x, Target.y);
        for (double CameraDistance : {1600.0, 6000.0})
        {
            const Point3 Origin{Target.x - Direction.X * CameraDistance,
                Target.y - Direction.Y * CameraDistance, ExpectedZ - Direction.Z * CameraDistance};
            Point3 Hit;
            int Samples = 0;
            const bool bHit = CinderTerrainPicking::Intersect(Origin, Direction, Minimum, Maximum,
                [&](double X, double Y) { ++Samples; return HeightAt(X, Y); }, Hit);
            TestTrue(*FString::Printf(TEXT("%s camera ray hits presented terrain"), Label), bHit);
            if (bHit)
            {
                TestTrue(*FString::Printf(TEXT("%s projection preserves world XY"), Label),
                    FMath::Abs(Hit.X - Target.x) < 0.1 && FMath::Abs(Hit.Y - Target.y) < 0.1);
                TestTrue(*FString::Printf(TEXT("%s projection preserves presented Z"), Label),
                    FMath::Abs(Hit.Z - ExpectedZ) < 0.1);
            }
            TestTrue(TEXT("Ground picking bounds height queries independently of camera altitude"),
                Samples <= 1 + CinderTerrainPicking::MarchSteps + CinderTerrainPicking::RefinementSteps);
        }
    };

    cinder::Simulation Authored;
    const auto& Map = cinder::mapDefinition(0, 2, cinder::MatchLength::Standard);
    TestTrue(TEXT("Picking fixture exercises authored high ground"), Authored.usesAuthoredTerrain());
    const auto AuthoredHeight = [&](double X, double Y)
    {
        return CinderLandscapeTerrain::BaselineZ + CinderLandscapeTerrain::HeightAt(Authored,
            static_cast<float>(X), static_cast<float>(Y));
    };
    for (const auto& Start : Map.starts) RoundTrip(Start, TEXT("Main plateau"), AuthoredHeight);
    for (const auto& Ramp : Map.ramps) for (float Along : {0.15f, 0.45f, 0.75f})
        RoundTrip({FMath::Lerp(Ramp.high.x, Ramp.low.x, Along),
            FMath::Lerp(Ramp.high.y, Ramp.low.y, Along)}, TEXT("Traversable ramp"), AuthoredHeight);
    RoundTrip({2400, 2400}, TEXT("Central low ground"), AuthoredHeight);

    const cinder::Vec2 Main = Map.starts.front();
    const double MainZ = AuthoredHeight(Main.x, Main.y);
    TestTrue(TEXT("The old zero-plane pick would visibly displace a plateau destination"),
        std::hypot(Direction.X, Direction.Y) * MainZ / -Direction.Z > 100);

    // GroundHeight returns exactly zero when a matching Landscape is unavailable,
    // even if the simulation's map definition contains an elevated main.
    RoundTrip(Main, TEXT("Presented flat fallback"), [](double, double) { return 0.0; });
    cinder::Config LegacyConfig; LegacyConfig.mapRevision = 0; LegacyConfig.ai = false;
    cinder::Simulation Legacy; Legacy.reset(LegacyConfig);
    RoundTrip({1100, 900}, TEXT("Legacy decorative terrain"), [&](double X, double Y)
    {
        return CinderLandscapeTerrain::BaselineZ + CinderLandscapeTerrain::HeightAt(Legacy,
            static_cast<float>(X), static_cast<float>(Y));
    });
    RoundTrip({1100, 900}, TEXT("Ground below the zero plane"), [](double, double) { return -45.0; });

    FTestWorldWrapper WorldOwner;
    if (!WorldOwner.CreateTestWorld(EWorldType::Game))
    {
        WorldOwner.ForwardErrorMessages(this);
        return false;
    }
    ACinderBattlefield* Battle = WorldOwner.GetTestWorld()->SpawnActor<ACinderBattlefield>();
    if (!TestNotNull(TEXT("Fog-aware picking fixture spawns the real battlefield"), Battle)) return false;
    // Exercise the material's relief/fog contract without relying on a GPU or
    // an installed Landscape asset in this transient automation world.
    Battle->bTerrainRelief = true;
    Battle->LastFogCells.SetNumZeroed(CinderFogMask::Cells * CinderFogMask::Cells);
    const float CellSize = Battle->Sim().worldSize() / CinderFogMask::Cells;
    for (int32 Y = 0; Y < CinderFogMask::Cells; ++Y) for (int32 X = 0; X < CinderFogMask::Cells; ++X)
    {
        const cinder::Vec2 Center{(X + 0.5f) * CellSize, (Y + 0.5f) * CellSize};
        Battle->LastFogCells[Y * CinderFogMask::Cells + X] = Battle->Sim().visible(0, Center)
            ? 2 : Battle->Sim().explored(0, Center) ? 1 : 0;
    }
    const cinder::Vec2 HiddenMain = Map.starts.back();
    const auto PresentedHeight = [Battle](double X, double Y)
    {
        return Battle->PickingGroundHeight({static_cast<float>(X), static_cast<float>(Y)});
    };
    TestFalse(TEXT("The opponent main begins unexplored"), Battle->Sim().explored(0, HiddenMain));
    TestEqual(TEXT("Unknown high ground picks the shader's flattened baseline"),
        Battle->PickingGroundHeight(HiddenMain), CinderLandscapeTerrain::BaselineZ);
    TestTrue(TEXT("Hidden plateau geometry remains available for entity seating"),
        Battle->GroundHeight(HiddenMain) > 170);
    RoundTrip(HiddenMain, TEXT("Unexplored opponent main"), PresentedHeight);
    TestEqual(TEXT("Fully explored home ground retains its real picking height"),
        Battle->PickingGroundHeight(Main), Battle->GroundHeight(Main));
    for (uint8 State : {uint8(1), uint8(2)})
    {
        Battle->LastFogCells.Init(State, CinderFogMask::Cells * CinderFogMask::Cells);
        TestEqual(TEXT("Explored and visible terrain retain the full plateau height"),
            Battle->PickingGroundHeight(HiddenMain), Battle->GroundHeight(HiddenMain));
    }

    // A partial reveal must reproduce the same quantized bilinear G channel as
    // the shader, rather than switching the entire cell to its full height.
    Battle->LastFogCells.Init(0, CinderFogMask::Cells * CinderFogMask::Cells);
    for (int32 Y = 0; Y < CinderFogMask::Cells; ++Y) for (int32 X = 50; X < CinderFogMask::Cells; ++X)
        Battle->LastFogCells[Y * CinderFogMask::Cells + X] = 1;
    const float TexelSize = Battle->Sim().worldSize() / CinderFogMask::TextureSize;
    const cinder::Vec2 FeatherPoint{201.75f * TexelSize, 220.75f * TexelSize};
    const auto UploadedGreen = [Battle](int32 X, int32 Y)
    {
        return FMath::RoundToInt(CinderFogMask::SampleTexel(Battle->FogCells(), X, Y).Explored * 255.0f) / 255.0f;
    };
    const float Green = UploadedGreen(201, 220) * 0.5625f + UploadedGreen(202, 220) * 0.1875f
        + UploadedGreen(201, 221) * 0.1875f + UploadedGreen(202, 221) * 0.0625f;
    const float ExpectedFeather = CinderLandscapeTerrain::BaselineZ
        + (Battle->GroundHeight(FeatherPoint) - CinderLandscapeTerrain::BaselineZ) * Green;
    TestTrue(TEXT("The fog boundary fixture has genuinely partial explored height"), Green > 0 && Green < 1);
    TestTrue(TEXT("Picking matches uploaded fog quantization and bilinear filtering"),
        FMath::IsNearlyEqual(Battle->PickingGroundHeight(FeatherPoint), ExpectedFeather, 0.001f));
    Battle->bTerrainRelief = false;
    TestEqual(TEXT("The flat presentation fallback stays flat regardless of map and fog"),
        Battle->PickingGroundHeight(HiddenMain), 0.0f);

    const Point3 Untouched{123, 456, 789};
    Point3 Hit = Untouched;
    const auto Flat = [](double, double) { return 0.0; };
    TestFalse(TEXT("Upward rays do not select ground behind the camera"),
        CinderTerrainPicking::Intersect({0, 0, 1000}, {0, 0, 1}, Minimum, Maximum, Flat, Hit));
    TestFalse(TEXT("Parallel rays have no ground hit"),
        CinderTerrainPicking::Intersect({0, 0, 1000}, {1, 0, 0}, Minimum, Maximum, Flat, Hit));
    TestFalse(TEXT("A downward ray already below ground has no front surface hit"),
        CinderTerrainPicking::Intersect({0, 0, -10}, {0, 0, -1}, Minimum, Maximum, Flat, Hit));
    TestFalse(TEXT("Invalid height samples do not create destinations"),
        CinderTerrainPicking::Intersect({0, 0, 1000}, {0, 0, -1}, Minimum, Maximum,
            [](double, double) { return std::numeric_limits<double>::quiet_NaN(); }, Hit));
    TestTrue(TEXT("Every rejected pick leaves its destination untouched"),
        Hit.X == Untouched.X && Hit.Y == Untouched.Y && Hit.Z == Untouched.Z);
    return true;
}

#endif
