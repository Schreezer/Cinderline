#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Presentation/CinderBattlefield.h"
#include "Presentation/CinderGameMode.h"
#include "Presentation/CinderPlayerController.h"
#include "Sim/AIDifficulty.h"
#include "Sim/MatchLength.h"
#include "Tests/AutomationCommon.h"

namespace
{
struct FDifficultyFixture
{
    FTestWorldWrapper WorldOwner;
    ACinderBattlefield* Battle = nullptr;
    ACinderPlayerController* Controller = nullptr;

    bool Initialize(FAutomationTestBase& Test)
    {
        if (!WorldOwner.CreateTestWorld(EWorldType::Game))
        {
            WorldOwner.ForwardErrorMessages(&Test);
            return false;
        }
        UWorld* World = WorldOwner.GetTestWorld();
        if (!Test.TestNotNull(TEXT("Difficulty test world exists"), World)) return false;
        World->GetWorldSettings()->DefaultGameMode = ACinderGameMode::StaticClass();
        Battle = World->SpawnActor<ACinderBattlefield>();
        if (!Test.TestNotNull(TEXT("Difficulty battlefield spawned"), Battle)) return false;
        if (!WorldOwner.BeginPlayInTestWorld())
        {
            WorldOwner.ForwardErrorMessages(&Test);
            return false;
        }
        Controller = World->SpawnActor<ACinderPlayerController>();
        if (!Test.TestNotNull(TEXT("Difficulty controller spawned"), Controller)) return false;
        return Test.TestTrue(TEXT("Difficulty controller resolves the battlefield"),
            Controller->Battlefield() == Battle);
    }
};

struct FTemporaryDifficultyFiles
{
    FString Directory = FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("Automation"),
        TEXT("CinderlineDifficulty"), FGuid::NewGuid().ToString(EGuidFormats::Digits));
    FString Settings = FPaths::ConvertRelativePathToFull(FPaths::Combine(Directory, TEXT("Skirmish.ini")));
    FString Match = FPaths::ConvertRelativePathToFull(FPaths::Combine(Directory, TEXT("match.cinder")));

    FTemporaryDifficultyFiles()
    {
        IFileManager::Get().MakeDirectory(*Directory, true);
    }

    ~FTemporaryDifficultyFiles()
    {
        IFileManager::Get().Delete(*Settings, false, true);
        IFileManager::Get().Delete(*Match, false, true);
        IFileManager::Get().DeleteDirectory(*Directory, false, false);
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCinderDifficultyIntegration,
    "Cinderline.Integration.SoloAIDifficulty",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCinderDifficultyIntegration::RunTest(const FString& Parameters)
{
    FDifficultyFixture Fixture;
    if (!Fixture.Initialize(*this)) return false;
    ACinderBattlefield& Battle = *Fixture.Battle;
    ACinderPlayerController& Controller = *Fixture.Controller;

    TestTrue(TEXT("A fresh menu defaults to Normal difficulty"),
        Controller.SelectedAIDifficulty() == cinder::AIDifficulty::Normal);
    TestTrue(TEXT("A fresh solo menu defaults to Standard match length"),
        Controller.SelectedMatchLength() == cinder::MatchLength::Standard);
    for (std::size_t Index = 0; Index < cinder::kAIDifficultyCount; ++Index)
    {
        Battle.ReturnToMenu();
        Controller.ExecuteAction(TEXT("difficulty"), static_cast<int32>(Index));
        const cinder::AIDifficulty Expected = cinder::aiDifficultyAt(Index);
        TestTrue(FString::Printf(TEXT("Menu selects %s"), UTF8_TO_TCHAR(cinder::aiDifficultyName(Expected))),
            Controller.SelectedAIDifficulty() == Expected);
        Controller.ExecuteAction(TEXT("start"), static_cast<int32>(Index % 3));
        TestTrue(TEXT("Starting from the menu enables the solo AI"), Battle.Sim().config().ai);
        TestEqual(TEXT("Starting from the menu applies the selected aggression"),
            Battle.Sim().config().aiAggression, cinder::aiDifficultyAggression(Expected));
        TestTrue(TEXT("Battlefield reports the active match difficulty"), Battle.MatchDifficulty() == Expected);
    }

    struct FLengthCase { cinder::MatchLength Length; float WorldSize; };
    const FLengthCase Lengths[] = {
        {cinder::MatchLength::Short, 3600.0f},
        {cinder::MatchLength::Standard, 4800.0f},
        {cinder::MatchLength::Long, 6000.0f},
    };
    for (const FLengthCase& Length : Lengths)
    {
        Battle.ReturnToMenu();
        Controller.ExecuteAction(TEXT("matchlength"), static_cast<int32>(Length.Length));
        TestTrue(FString::Printf(TEXT("Menu selects match length %d"), static_cast<int32>(Length.Length)),
            Controller.SelectedMatchLength() == Length.Length);
        Controller.ExecuteAction(TEXT("start"), static_cast<int32>(Length.Length));
        TestTrue(TEXT("Starting a solo match applies the selected match length"),
            Battle.Sim().config().matchLength == Length.Length);
        TestEqual(TEXT("Active world dimensions follow the selected match length"),
            Battle.Sim().worldSize(), Length.WorldSize);

        if (Length.Length == cinder::MatchLength::Standard) continue;
        const cinder::Id Soldier = Battle.Sim().debugSpawn(cinder::Kind::Striker, 0, {700, 1000});
        if (!TestTrue(TEXT("Dynamic bounds fixture selects a live owned unit"),
            Soldier && Controller.SelectOwnedEntity(Soldier))) return false;
        Controller.ExecuteAction(TEXT("move"));
        Controller.FeedbackText.Empty();
        const cinder::Vec2 Point{Length.Length == cinder::MatchLength::Short ? 3700.0f : 5500.0f, 1000.0f};
        const auto BeforePoint = Battle.Sim().stateHash();
        const auto BeforePointCount = Battle.Sim().recording().size();
        Controller.HandleWorldTap(0, &Point);
        if (Length.Length == cinder::MatchLength::Short)
        {
            TestTrue(TEXT("Short-world tap rejects x=3700 and retains the pending Move with feedback"),
                Controller.IsMoveCommandMode() && !Controller.Feedback().IsEmpty()
                && Battle.Sim().stateHash() == BeforePoint && Battle.Sim().recording().size() == BeforePointCount);
        }
        else
        {
            if (!TestTrue(TEXT("Long-world tap accepts x=5500 and records one Move"),
                Battle.Sim().recording().size() == BeforePointCount + 1)) return false;
            const auto& Move = Battle.Sim().recording().back().command;
            TestTrue(TEXT("Long-world destination keeps the chosen unit and clears the armed mode"),
                Move.type == cinder::CommandType::Move && Move.units == std::vector<cinder::Id>{Soldier}
                && Move.point.x == Point.x && Move.point.y == Point.y
                && Battle.Sim().find(Soldier)->order == cinder::Order::Move
                && !Controller.IsMoveCommandMode() && !Controller.IsAttackMoveMode()
                && !Controller.IsDefendCommandMode());
        }
    }

    Battle.ReturnToMenu();
    Controller.ExecuteAction(TEXT("difficulty"), static_cast<int32>(cinder::AIDifficulty::Hard));
    Controller.ExecuteAction(TEXT("matchlength"), static_cast<int32>(cinder::MatchLength::Short));
    Controller.ExecuteAction(TEXT("start"), 2);
    Controller.MenuDifficulty = cinder::AIDifficulty::VeryEasy;
    Controller.MenuMatchLength = cinder::MatchLength::Long;
    Controller.ExecuteAction(TEXT("start"), 2);
    TestTrue(TEXT("A rematch retains the completed match difficulty"),
        Battle.MatchDifficulty() == cinder::AIDifficulty::Hard);
    TestTrue(TEXT("A rematch retains its active Short length despite a different menu preference"),
        Battle.Sim().config().matchLength == cinder::MatchLength::Short && Battle.Sim().worldSize() == 3600.0f
        && Controller.SelectedMatchLength() == cinder::MatchLength::Long);

    FTemporaryDifficultyFiles Files;
    Controller.MenuMatchLength = cinder::MatchLength::Long;
    Controller.LoadMatchLengthPreference(Files.Settings);
    TestTrue(TEXT("Missing match-length preferences default to Standard"),
        Controller.SelectedMatchLength() == cinder::MatchLength::Standard);
    Controller.MenuDifficulty = cinder::AIDifficulty::Expert;
    Controller.SaveDifficultyPreference(Files.Settings);
    Controller.MenuDifficulty = cinder::AIDifficulty::VeryEasy;
    Controller.LoadDifficultyPreference(Files.Settings);
    TestTrue(TEXT("The menu difficulty round-trips through local settings"),
        Controller.SelectedAIDifficulty() == cinder::AIDifficulty::Expert);

    Controller.MenuMatchLength = cinder::MatchLength::Short;
    Controller.LoadMatchLengthPreference(Files.Settings);
    TestTrue(TEXT("Legacy difficulty-only preferences default match length to Standard"),
        Controller.SelectedMatchLength() == cinder::MatchLength::Standard);
    const FString LegacySettings = FString::Printf(TEXT("[Skirmish]\nAIDifficulty=%d\n\n[Unrelated]\nKeepMe=preserved\n"),
        static_cast<int32>(cinder::AIDifficulty::Expert));
    if (!TestTrue(TEXT("Preference compatibility fixture writes only a temporary INI"),
        FFileHelper::SaveStringToFile(LegacySettings, *Files.Settings))) return false;
    Controller.MenuMatchLength = cinder::MatchLength::Long;
    Controller.SaveMatchLengthPreference(Files.Settings);
    Controller.MenuMatchLength = cinder::MatchLength::Short;
    Controller.LoadMatchLengthPreference(Files.Settings);
    TestTrue(TEXT("The match-length preference round-trips independently"),
        Controller.SelectedMatchLength() == cinder::MatchLength::Long);
    FConfigFile LengthSettings; LengthSettings.Read(Files.Settings);
    int32 StoredDifficulty = -1;
    FString Unrelated;
    TestTrue(TEXT("Saving match length preserves the existing AI difficulty and unrelated settings"),
        LengthSettings.GetInt(TEXT("Skirmish"), TEXT("AIDifficulty"), StoredDifficulty)
        && StoredDifficulty == static_cast<int32>(cinder::AIDifficulty::Expert)
        && LengthSettings.GetString(TEXT("Unrelated"), TEXT("KeepMe"), Unrelated) && Unrelated == TEXT("preserved"));
    Controller.MenuDifficulty = cinder::AIDifficulty::Easy;
    Controller.SaveDifficultyPreference(Files.Settings);
    FConfigFile DifficultySettings; DifficultySettings.Read(Files.Settings);
    int32 StoredLength = -1;
    Unrelated.Empty();
    TestTrue(TEXT("Saving AI difficulty preserves the selected length and unrelated settings"),
        DifficultySettings.GetInt(TEXT("Skirmish"), TEXT("MatchLength"), StoredLength)
        && StoredLength == static_cast<int32>(cinder::MatchLength::Long)
        && DifficultySettings.GetString(TEXT("Unrelated"), TEXT("KeepMe"), Unrelated) && Unrelated == TEXT("preserved"));
    for (const TCHAR* Invalid : {TEXT("-1"), TEXT("3"), TEXT("999"), TEXT("junk"), TEXT("1junk")})
    {
        const FString Settings = FString::Printf(TEXT("[Skirmish]\nAIDifficulty=4\nMatchLength=%s\n"), Invalid);
        if (!TestTrue(TEXT("Invalid-length fixture writes to its temporary INI"),
            FFileHelper::SaveStringToFile(Settings, *Files.Settings))) return false;
        Controller.MenuMatchLength = cinder::MatchLength::Long;
        Controller.LoadMatchLengthPreference(Files.Settings);
        TestTrue(FString::Printf(TEXT("Invalid stored length '%s' resets to Standard"), Invalid),
            Controller.SelectedMatchLength() == cinder::MatchLength::Standard);
    }
    if (!TestTrue(TEXT("Trimmed numeric length fixture writes to its temporary INI"),
        FFileHelper::SaveStringToFile(TEXT("[Skirmish]\nMatchLength= 2 \n"), *Files.Settings))) return false;
    Controller.LoadMatchLengthPreference(Files.Settings);
    TestTrue(TEXT("Canonical numeric match lengths accept surrounding whitespace"),
        Controller.SelectedMatchLength() == cinder::MatchLength::Long);

    Battle.StartMatch(1, cinder::AIDifficulty::Expert, cinder::MatchLength::Long);
    if (!TestTrue(TEXT("Expert match writes a temporary save"),
        Battle.Sim().save(TCHAR_TO_UTF8(*Files.Match)))) return false;
    Battle.StartMatch(0, cinder::AIDifficulty::Easy, cinder::MatchLength::Short);
    if (!TestTrue(TEXT("Continue loads the temporary match"), Battle.LoadMatchFrom(Files.Match))) return false;
    TestTrue(TEXT("Continue restores the difficulty saved with the match"),
        Battle.MatchDifficulty() == cinder::AIDifficulty::Expert);
    TestTrue(TEXT("Continue restores the saved Long match dimensions"),
        Battle.Sim().config().matchLength == cinder::MatchLength::Long && Battle.Sim().worldSize() == 6000.0f);
    Controller.MenuDifficulty = cinder::AIDifficulty::VeryEasy;
    Controller.MenuMatchLength = cinder::MatchLength::Short;
    Controller.ExecuteAction(TEXT("start"), Battle.MapIndex());
    TestTrue(TEXT("A rematch after Continue retains the saved match difficulty"),
        Battle.MatchDifficulty() == cinder::AIDifficulty::Expert);
    TestTrue(TEXT("A rematch after Continue retains the saved match length"),
        Battle.Sim().config().matchLength == cinder::MatchLength::Long && Battle.Sim().worldSize() == 6000.0f);

    Battle.ReturnToMenu();
    Controller.ExecuteAction(TEXT("difficulty"), static_cast<int32>(cinder::AIDifficulty::Hard));
    Controller.ExecuteAction(TEXT("matchlength"), static_cast<int32>(cinder::MatchLength::Long));
    Controller.ExecuteAction(TEXT("tutorial"));
    TestTrue(TEXT("Guided training remains a separate no-AI scenario"),
        Battle.Tutorial().IsActive() && !Battle.Sim().config().ai
        && Battle.Sim().config().seed == FCinderTutorial::Seed
        && Battle.Tutorial().OpponentOrdersIssued() == 0);
    TestTrue(TEXT("Guided training always uses Standard dimensions without replacing the solo length"),
        Battle.Sim().config().matchLength == cinder::MatchLength::Standard && Battle.Sim().worldSize() == 4800.0f
        && Controller.SelectedMatchLength() == cinder::MatchLength::Long);
    Controller.ExecuteAction(TEXT("tutorialend"));
    Controller.ExecuteAction(TEXT("start"), 0);
    TestTrue(TEXT("Training does not replace the selected solo difficulty"),
        !Battle.Tutorial().IsActive() && Battle.Sim().config().ai
        && Battle.Sim().config().seed != FCinderTutorial::Seed
        && Battle.MatchDifficulty() == cinder::AIDifficulty::Hard);
    TestTrue(TEXT("Leaving training restores the selected Long solo match"),
        Battle.Sim().config().matchLength == cinder::MatchLength::Long && Battle.Sim().worldSize() == 6000.0f);

    Battle.ReturnToMenu();
    Controller.ExecuteAction(TEXT("difficulty"), 999);
    TestTrue(TEXT("An invalid menu difficulty falls back to Normal"),
        Controller.SelectedAIDifficulty() == cinder::AIDifficulty::Normal);
    Controller.ExecuteAction(TEXT("matchlength"), 999);
    TestTrue(TEXT("An invalid menu length falls back to Standard"),
        Controller.SelectedMatchLength() == cinder::MatchLength::Standard);
    return !HasAnyErrors();
}

#endif
