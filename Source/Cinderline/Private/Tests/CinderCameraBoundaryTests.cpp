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
            for (float Zoom : {650.0f, 1650.0f, 3200.0f})
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
        Rig->Zoom(650 - Rig->Distance());
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
    AddInfo(FString::Printf(TEXT("CINDERLINE_CAMERA_BOUNDARY_PASS cases=%d; three world sizes, four aspects, three zooms, eight edge/corner directions, repeated drag, reversal, interpolated zoom, and resized black borders. Physical input and rendered black pixels require viewport verification."), Cases));
    return true;
}

#endif
