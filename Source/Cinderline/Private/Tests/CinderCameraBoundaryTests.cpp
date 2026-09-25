#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Camera/CameraComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/SpringArmComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Presentation/CinderBattlefield.h"
#include "Presentation/CinderCamera.h"
#include "Sim/Simulation.h"
#include "Tests/AutomationCommon.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderCameraBoundaryTest,
    "Cinderline.Presentation.CameraBoundary",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderCameraBoundaryTest::RunTest(const FString& Parameters)
{
    FTestWorldWrapper WorldOwner;
    if (!WorldOwner.CreateTestWorld(EWorldType::Game))
    {
        WorldOwner.ForwardErrorMessages(this);
        return false;
    }
    ACinderCamera* Rig = WorldOwner.GetTestWorld()->SpawnActor<ACinderCamera>();
    if (!TestNotNull(TEXT("Real camera rig spawned"), Rig)) return false;
    struct FWorldCase
    {
        cinder::MatchLength Length;
        double WorldSize;
        const TCHAR* Name;
    };
    const FWorldCase WorldCases[] = {
        {cinder::MatchLength::Short, 3600.0, TEXT("Short")},
        {cinder::MatchLength::Standard, 4800.0, TEXT("Standard")},
        {cinder::MatchLength::Long, 6000.0, TEXT("Long")}
    };
    const FVector Directions[] = {
        {-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0},
        {-1, -1, 0}, {-1, 1, 0}, {1, -1, 0}, {1, 1, 0}
    };
    int32 Cases = 0;
    for (const FWorldCase& WorldCase : WorldCases)
    {
        const double WorldSize = WorldCase.WorldSize;
        const FVector Center(WorldSize * 0.5, WorldSize * 0.5, 0);
        Rig->SetWorldSize(WorldSize);
        TestEqual(*FString::Printf(TEXT("%s camera accepts the simulation world size"), WorldCase.Name),
            Rig->ActiveWorldSize, static_cast<float>(WorldSize));
        for (float Aspect : {4.0f / 3.0f, 16.0f / 9.0f, 2868.0f / 1320.0f, 9.0f / 16.0f})
        {
            Rig->Camera->AspectRatio = Aspect; // No viewport: the rig uses its configured aspect.
            const auto Footprint = Rig->GroundFootprint();
            if (!TestTrue(TEXT("Camera rays intersect the ground at every supported aspect"), Footprint.bValid)) return false;
            for (float Zoom : {ACinderCamera::ClosestDistance, ACinderCamera::DefaultDistance,
                               ACinderCamera::FarthestDistance})
            for (const FVector& Direction : Directions)
            {
                Rig->Focus(Center, true);
                Rig->Zoom(Zoom - Rig->Distance());
                Rig->Focus(Center, true);
                Rig->Pan(Direction * 1000000);
                const FVector AtLimit = Rig->TargetPosition;
                const FVector2D Low = FVector2D(AtLimit) + Footprint.Min * Rig->Distance();
                const FVector2D High = FVector2D(AtLimit) + Footprint.Max * Rig->Distance();
                TestTrue(TEXT("Camera exposes at most 360 world units outside each active map edge"),
                    Low.X >= -360.01 && Low.Y >= -360.01
                    && High.X <= WorldSize + 360.01 && High.Y <= WorldSize + 360.01);
                if (Direction.X < 0) TestTrue(TEXT("West border becomes visible"), Low.X < -1);
                if (Direction.X > 0) TestTrue(TEXT("East border becomes visible"), High.X > WorldSize + 1);
                if (Direction.Y < 0) TestTrue(TEXT("North border becomes visible"), Low.Y < -1);
                if (Direction.Y > 0) TestTrue(TEXT("South border becomes visible"), High.Y > WorldSize + 1);

                for (int32 Repeat = 0; Repeat < 100; ++Repeat) Rig->Pan(Direction * 1000000);
                TestTrue(TEXT("Continued outward dragging stops at the same finite limit"),
                    Rig->TargetPosition.Equals(AtLimit, 0.001));
                Rig->Pan(-Direction * 40);
                TestTrue(TEXT("Reversing the drag moves back immediately"),
                    FVector::DotProduct(Rig->TargetPosition - AtLimit, Direction) < -1);

                Rig->SetActorLocation(AtLimit);
                Rig->Boom->TargetArmLength = Rig->Distance();
                Rig->Zoom(3200);
                for (int32 Frame = 0; Frame < 60; ++Frame)
                {
                    Rig->Tick(1.0f / 30);
                    const auto CurrentFootprint = Rig->GroundFootprint();
                    const FVector2D CurrentLow = FVector2D(Rig->GetActorLocation()) + CurrentFootprint.Min * Rig->Boom->TargetArmLength;
                    const FVector2D CurrentHigh = FVector2D(Rig->GetActorLocation()) + CurrentFootprint.Max * Rig->Boom->TargetArmLength;
                    if (!TestTrue(TEXT("Interpolated zoom and pan stay inside the active border allowance"),
                        CurrentLow.X >= -360.1 && CurrentLow.Y >= -360.1
                        && CurrentHigh.X <= WorldSize + 360.1 && CurrentHigh.Y <= WorldSize + 360.1)) return false;
                }
                ++Cases;
            }
        }
        Rig->Camera->AspectRatio = 2868.0f / 1320.0f;
        Rig->Focus(Center, true);
        Rig->Zoom(ACinderCamera::ClosestDistance - Rig->Distance());
        Rig->Focus(Center, true);
        Rig->Pan(FVector(-1000000, 0, 0));
        const auto CloseFootprint = Rig->GroundFootprint();
        const double CloseOverscroll = -(Rig->TargetPosition.X + CloseFootprint.Min.X * Rig->Distance());
        TestTrue(TEXT("Close zoom allows less empty space than the absolute border cap"),
            CloseOverscroll > 0 && CloseOverscroll < 200);
    }

    ACinderBattlefield* Battle = WorldOwner.GetTestWorld()->SpawnActor<ACinderBattlefield>();
    if (!TestNotNull(TEXT("Boundary fixture battlefield spawned"), Battle)) return false;
    if (!WorldOwner.BeginPlayInTestWorld())
    {
        WorldOwner.ForwardErrorMessages(this);
        return false;
    }
    TArray<UInstancedStaticMeshComponent*> Components;
    Battle->GetComponents(Components);
    UInstancedStaticMeshComponent* Border = nullptr;
    for (auto* Component : Components)
        if (Component->ComponentHasTag(TEXT("CinderMapBorder"))) Border = Component;
    if (!TestNotNull(TEXT("Static map border batch exists"), Border)) return false;
    TestTrue(TEXT("Black border is unlit and opaque"), Border->GetMaterial(0)
        && Border->GetMaterial(0)->GetShadingModels().HasShadingModel(MSM_Unlit)
        && Border->GetMaterial(0)->GetBlendMode() == BLEND_Opaque);
    TestTrue(TEXT("Border is noncolliding and casts no shadows"),
        Border->GetCollisionEnabled() == ECollisionEnabled::NoCollision && !Border->CastShadow);
    TArray<FTransform> StandardTransforms;
    for (const FWorldCase& WorldCase : WorldCases)
    {
        Battle->StartMatch(0, cinder::AIDifficulty::Normal, WorldCase.Length);
        Battle->Tick(0.033f);
        TestEqual(*FString::Printf(TEXT("%s match resynchronizes the live camera bounds"), WorldCase.Name),
            Rig->ActiveWorldSize, static_cast<float>(WorldCase.WorldSize));
        const float ExpectedInverse = 1.0f / static_cast<float>(WorldCase.WorldSize);
        float FogInverse = 0.0f, GroundInverse = 0.0f;
        if (FApp::CanEverRender())
        {
            TestTrue(*FString::Printf(TEXT("%s fog material exposes active world-size UV parameter"), WorldCase.Name),
                Battle->FogMaterial && Battle->FogMaterial->GetScalarParameterValue(
                    FMaterialParameterInfo(TEXT("CinderWorldSizeInverse")), FogInverse));
            TestTrue(*FString::Printf(TEXT("%s fog mask spans the active world"), WorldCase.Name),
                FMath::IsNearlyEqual(FogInverse, ExpectedInverse, 1.e-8f));
        }
        else
        {
            TestNull(TEXT("NullRHI intentionally retains the grid fog fallback"), Battle->FogMaterial.Get());
            TestEqual(TEXT("NullRHI has no fog material plane"), Battle->FogPlaneBatch, INDEX_NONE);
        }
        TestTrue(*FString::Printf(TEXT("%s terrain material exposes active world-size UV parameter"), WorldCase.Name),
            Battle->GroundSurfaceMaterial && Battle->GroundSurfaceMaterial->GetScalarParameterValue(
                FMaterialParameterInfo(TEXT("CinderWorldSizeInverse")), GroundInverse));
        TestTrue(*FString::Printf(TEXT("%s terrain layers span the active world"), WorldCase.Name),
            FMath::IsNearlyEqual(GroundInverse, ExpectedInverse, 1.e-8f));
        TestEqual(*FString::Printf(TEXT("%s match retains exactly four border instances"), WorldCase.Name),
            Border->GetInstanceCount(), 4);
        for (int32 Index = 0; Index < Border->GetInstanceCount(); ++Index)
        {
            FTransform Transform;
            Border->GetInstanceTransform(Index, Transform, true);
            const FVector Half = Transform.GetScale3D() * 50;
            const FVector Low = Transform.GetLocation() - Half;
            const FVector High = Transform.GetLocation() + Half;
            TestTrue(*FString::Printf(TEXT("%s border plane %d stays outside playable ground"),
                WorldCase.Name, Index), High.X <= 0 || Low.X >= WorldCase.WorldSize
                    || High.Y <= 0 || Low.Y >= WorldCase.WorldSize);
            if (WorldCase.Length == cinder::MatchLength::Standard) StandardTransforms.Add(Transform);
        }
    }
    Battle->StartMatch(2, cinder::AIDifficulty::Normal, cinder::MatchLength::Standard);
    Battle->Tick(0.033f);
    for (int32 Index = 0; Index < StandardTransforms.Num(); ++Index)
    {
        FTransform Transform;
        Border->GetInstanceTransform(Index, Transform, true);
        TestTrue(TEXT("Returning to Standard restores its exact border transforms"),
            Transform.Equals(StandardTransforms[Index], 0));
    }
    // The authored duel raises the home to 180 cm. Exercise the actual camera's
    // focus, zoom clamp and projection with the battlefield's presented-height
    // sampler; a flat XY-only focus lets the 185 cm Anchor roof leave a phone view.
    Battle->StartMatch(0, cinder::AIDifficulty::Normal, cinder::MatchLength::Standard);
    Battle->bTerrainRelief = true;
    Battle->LastFogCells.Init(2, cinder::Simulation::FogSize * cinder::Simulation::FogSize);
    Rig->SetTerrainSource(Battle);
    const cinder::Vec2 Home = Battle->Sim().entities().front().pos;
    const FVector AnchorExtent(125.0f, 125.0f, 92.5f);
    int32 ElevatedCases = 0;
    for (float Aspect : {1440.0f / 900.0f, 1170.0f / 540.0f})
    for (float RequestedZoom : {ACinderCamera::ClosestDistance, ACinderCamera::DefaultDistance,
        ACinderCamera::FarthestDistance})
    {
        Rig->Camera->AspectRatio = Aspect;
        const float GroundZ = Battle->PickingGroundHeight(Home);
        TestTrue(TEXT("The new main has presented elevated ground"), GroundZ > 170.0f);
        Rig->Focus(FVector(Home.x, Home.y, GroundZ), true, AnchorExtent);
        Rig->Zoom(RequestedZoom - Rig->Distance());
        Rig->Focus(FVector(Home.x, Home.y, GroundZ), true, AnchorExtent);
        const auto Footprint = Rig->GroundFootprint();
        TestTrue(TEXT("Home focus frames the complete elevated Anchor at desktop and phone zooms"),
            Rig->ContainsFocus(Rig->GetActorLocation(), Rig->Distance(), Footprint));
        // The ordinary interpolation clamp must retain the same permitted
        // focus region instead of undoing a correct instantaneous Home view.
        for (int32 Frame = 0; Frame < 30; ++Frame) Rig->Tick(1.0f / 30);
        TestTrue(TEXT("Normal camera ticks retain the complete elevated Home framing"),
            Rig->ContainsFocus(Rig->GetActorLocation(), Rig->Boom->TargetArmLength, Footprint));
        const FVector Eye = Rig->GetActorLocation() - Footprint.Forward * Rig->Distance();
        for (int32 X : {-1, 1}) for (int32 Y : {-1, 1}) for (int32 Z : {-1, 1})
        {
            const FVector Point(Home.x + X * AnchorExtent.X, Home.y + Y * AnchorExtent.Y,
                GroundZ + AnchorExtent.Z + Z * AnchorExtent.Z);
            const FVector Delta = Point - Eye;
            const double Depth = FVector::DotProduct(Delta, Footprint.Forward);
            TestTrue(TEXT("Every real Anchor bounding-box corner projects inside the safe viewport"),
                Depth > 0 && FMath::Abs(FVector::DotProduct(Delta, Footprint.Right)) <= Depth * Footprint.TanHalfX * 0.801
                && FMath::Abs(FVector::DotProduct(Delta, Footprint.Up)) <= Depth * Footprint.TanHalfY * 0.801);
        }
        const FVector Pivot = Rig->GetActorLocation();
        const double HeightDistance = Pivot.Z / -Footprint.Forward.Z;
        const FVector2D Shift(Footprint.Forward.X * HeightDistance, Footprint.Forward.Y * HeightDistance);
        const FVector2D Low = FVector2D(Pivot) + Shift + Footprint.Min * (Rig->Distance() + HeightDistance);
        const FVector2D High = FVector2D(Pivot) + Shift + Footprint.Max * (Rig->Distance() + HeightDistance);
        TestTrue(TEXT("The raised camera keeps its complete world-plane footprint within the existing border allowance"),
            Low.X >= -360.1 && Low.Y >= -360.1 && High.X <= 5160.1 && High.Y <= 5160.1);
        ++ElevatedCases;
    }
    Rig->Camera->AspectRatio = 1170.0f / 540.0f;
    Rig->Focus(FVector(1490, 1230, 0), true);
    Rig->Zoom(ACinderCamera::DefaultDistance - Rig->Distance());
    Rig->Focus(FVector(1490, 1230, 0), true);
    for (int32 Step = 0; Step < 4; ++Step)
    {
        Rig->Pan(FVector(140, 0, 0));
        for (int32 Frame = 0; Frame < 60; ++Frame) Rig->Tick(1.0f / 30);
        const FVector Pivot = Rig->GetActorLocation();
        TestTrue(TEXT("Panning down the ramp follows the currently presented incline"),
            FMath::IsNearlyEqual(Pivot.Z, static_cast<double>(Battle->PickingGroundHeight(
                {static_cast<float>(Pivot.X), static_cast<float>(Pivot.Y)})), 0.1));
    }
    Battle->LastFogCells.Init(0, cinder::Simulation::FogSize * cinder::Simulation::FogSize);
    Rig->Focus(FVector(Home.x, Home.y, 0), true);
    TestTrue(TEXT("Unknown plateau focus follows fog-flattened ground without exposing its elevation"),
        FMath::IsNearlyEqual(Rig->GetActorLocation().Z,
            static_cast<double>(Battle->PickingGroundHeight(Home)), 0.1) && Rig->GetActorLocation().Z <= 0.0);
    Battle->bTerrainRelief = false;
    Rig->Focus(FVector(Home.x, Home.y, 0), true);
    TestTrue(TEXT("A flat fallback never retains the previous elevated camera pivot"),
        FMath::IsNearlyZero(Rig->GetActorLocation().Z, 0.1));
    AddInfo(FString::Printf(TEXT("CINDERLINE_CAMERA_ELEVATED_HOME_PASS cases=%d; actual 250x250x185 cm Anchor bounds, desktop/phone, closest/default/farthest requests, elevated border footprint, ramp pan, fog flattening and flat fallback."), ElevatedCases));
    AddInfo(FString::Printf(TEXT("CINDERLINE_CAMERA_BOUNDARY_PASS cases=%d; three world sizes, four aspects, three zooms, eight edge/corner directions, repeated drag, reversal, interpolated zoom, and resized black borders. Physical input and rendered black pixels require viewport verification."), Cases));
    return true;
}

#endif
