#include "CoreMinimal.h"

#if UE_BUILD_DEVELOPMENT

#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Presentation/CinderBattlefield.h"
#include "Presentation/CinderHUD.h"
#include "Presentation/CinderOnlineSubsystem.h"
#include "Presentation/CinderPlayerController.h"
#include "TimerManager.h"
#include "UnrealClient.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"

DEFINE_LOG_CATEGORY_STATIC(LogCinderOnlinePreview, Log, All);

namespace
{
struct FOnlinePreviewRequest
{
    TWeakObjectPtr<UWorld> World;
    FString Mode;
    int32 Attempts = 0;
    bool bOpened = false;
    bool bInitialCaptured = false;
    int32 ScrollStage = 0;
    TSet<FString> PaintedButtons;
    float MinimumButtonHeight = TNumericLimits<float>::Max();
    UGameInstance* GuestInstances[3] = {};
    TWeakObjectPtr<UCinderOnlineSubsystem> Guests[3];
    int32 NextGuest = 0;
    bool bFourRoomCreated = false;
    bool bFourReadyRequested = false;
};

struct FOnlinePanelAudit
{
    TArray<FString> Texts;
    TArray<FBox2D> ButtonBounds;
    TArray<FString> ButtonReports;
    TSet<FString> PaintedButtons;
    TSharedPtr<SScrollBox> ScrollBox;
    int32 RawButtons = 0;
    int32 VisibleButtons = 0;
    int32 ButtonsInViewport = 0;
    float MinimumButtonHeight = TNumericLimits<float>::Max();
    float ScrollRange = 0.0f;
    bool bHasScrollBox = false;
};

FOnlinePreviewRequest PreviewRequest;
FTimerHandle PreviewReadyTimer;
FTimerHandle PreviewCaptureTimer;

void ClearPreviewTimers()
{
    if (UWorld* World = PreviewRequest.World.Get())
    {
        World->GetTimerManager().ClearTimer(PreviewReadyTimer);
        World->GetTimerManager().ClearTimer(PreviewCaptureTimer);
    }
    PreviewReadyTimer.Invalidate();
    PreviewCaptureTimer.Invalidate();
}

void DestroyGuestInstance(UGameInstance* Instance)
{
    if (!Instance) return;
    UWorld* GuestWorld = Instance->GetWorld();
    Instance->Shutdown();
    if (GuestWorld)
    {
        GEngine->DestroyWorldContext(GuestWorld);
        GuestWorld->DestroyWorld(false);
    }
    Instance->RemoveFromRoot();
}

void CleanupFourPlayerFixture()
{
    UGameInstance* HostInstance = PreviewRequest.World.IsValid() ? PreviewRequest.World->GetGameInstance() : nullptr;
    if (UCinderOnlineSubsystem* Host = HostInstance ? HostInstance->GetSubsystem<UCinderOnlineSubsystem>() : nullptr)
        Host->Leave();
    for (int32 Index = 0; Index < 3; ++Index)
    {
        if (UCinderOnlineSubsystem* Guest = PreviewRequest.Guests[Index].Get()) Guest->Leave();
        if (UGameInstance* Instance = PreviewRequest.GuestInstances[Index])
        {
            DestroyGuestInstance(Instance);
        }
        PreviewRequest.Guests[Index].Reset();
        PreviewRequest.GuestInstances[Index] = nullptr;
    }
}

void FailPreview(const FString& Reason)
{
    UE_LOG(LogCinderOnlinePreview, Error,
        TEXT("CINDERLINE_ONLINE_PREVIEW failed=%s mode=%s"), *Reason, *PreviewRequest.Mode);
    ClearPreviewTimers();
    if (PreviewRequest.Mode == TEXT("four")) CleanupFourPlayerFixture();
    PreviewRequest = {};
}

void CollectWidgetText(const TSharedRef<SWidget>& Widget, TArray<FString>& Out)
{
    if (Widget->GetType() == FName(TEXT("STextBlock")))
    {
        const FString Text = StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString();
        if (!Text.IsEmpty()) Out.Add(Text);
    }
    if (FChildren* Children = Widget->GetChildren())
        for (int32 Index = 0; Index < Children->Num(); ++Index)
            CollectWidgetText(Children->GetChildAt(Index), Out);
}

bool FullyInside(const FBox2D& Outer, const FBox2D& Inner)
{
    return Inner.Min.X >= Outer.Min.X - 0.5 && Inner.Min.Y >= Outer.Min.Y - 0.5
        && Inner.Max.X <= Outer.Max.X + 0.5 && Inner.Max.Y <= Outer.Max.Y + 0.5;
}

FBox2D Intersection(const FBox2D& Left, const FBox2D& Right)
{
    return FBox2D(
        FVector2D(FMath::Max(Left.Min.X, Right.Min.X), FMath::Max(Left.Min.Y, Right.Min.Y)),
        FVector2D(FMath::Min(Left.Max.X, Right.Max.X), FMath::Min(Left.Max.Y, Right.Max.Y)));
}

void AuditWidget(const TSharedRef<SWidget>& Widget, bool bParentVisible,
    const FBox2D& ClipBounds, const FString& Path, FOnlinePanelAudit& Out)
{
    const bool bVisible = bParentVisible && Widget->GetVisibility().IsVisible();
    const FName Type = Widget->GetType();
    FBox2D ChildClipBounds = ClipBounds;
    if (Type == FName(TEXT("STextBlock")))
    {
        Out.Texts.Add(StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString());
    }
    else if (Type == FName(TEXT("SScrollBox")) && bVisible)
    {
        Out.bHasScrollBox = true;
        Out.ScrollBox = StaticCastSharedRef<SScrollBox>(Widget);
        Out.ScrollRange = FMath::Max(Out.ScrollRange, Out.ScrollBox->GetScrollOffsetOfEnd());
        const FGeometry& Geometry = Widget->GetCachedGeometry();
        const FVector2D Position = Geometry.GetAbsolutePosition();
        const FVector2D Size = Geometry.GetAbsoluteSize();
        ChildClipBounds = Intersection(ClipBounds, FBox2D(Position, Position + Size));
    }
    else if (Type == FName(TEXT("SButton")) && bVisible)
    {
        ++Out.RawButtons;
        const FGeometry& Geometry = Widget->GetCachedGeometry();
        const FVector2D Position = Geometry.GetAbsolutePosition();
        const FVector2D Size = Geometry.GetAbsoluteSize();
        const FVector2D LocalSize = Geometry.GetLocalSize();
        const FBox2D Bounds(Position, Position + Size);
        TArray<FString> ButtonTexts;
        CollectWidgetText(Widget, ButtonTexts);
        const FString Label = FString::Join(ButtonTexts, TEXT("+"));
        const bool bInsideClip = FullyInside(ClipBounds, Bounds);
        const bool bStaleFromFirstPosition = PreviewRequest.ScrollStage > 0
            && PreviewRequest.PaintedButtons.Contains(Label) && Label != TEXT("BACK");
        Out.ButtonReports.Add(FString::Printf(
            TEXT("label=%s path=%s local=(%.1f,%.1f) physical=(%.1f,%.1f) bounds=(%.1f,%.1f)-(%.1f,%.1f) inside_clip=%d stale=%d"),
            Label.IsEmpty() ? TEXT("<internal>") : *Label, *Path,
            LocalSize.X, LocalSize.Y, Size.X, Size.Y,
            Position.X, Position.Y, Position.X + Size.X, Position.Y + Size.Y,
            bInsideClip ? 1 : 0, bStaleFromFirstPosition ? 1 : 0));
        // Editable fields and the scroll bar contain internal Slate buttons.
        // Only labelled game actions promise a 44-point interaction target.
        if (!Label.IsEmpty() && Size.X > 0.5 && Size.Y > 0.5
            && bInsideClip && !bStaleFromFirstPosition)
        {
            Out.PaintedButtons.Add(Label);
            Out.ButtonBounds.Add(Bounds);
            ++Out.VisibleButtons;
            Out.MinimumButtonHeight = FMath::Min(Out.MinimumButtonHeight, static_cast<float>(Size.Y));
            ++Out.ButtonsInViewport;
        }
    }

    if (FChildren* Children = Widget->GetChildren())
        for (int32 Index = 0; Index < Children->Num(); ++Index)
        {
            const TSharedRef<SWidget> Child = Children->GetChildAt(Index);
            AuditWidget(Child, bVisible, ChildClipBounds,
                Path + TEXT("/") + Child->GetTypeAsString() + FString::Printf(TEXT("[%d]"), Index), Out);
        }
}

bool HasText(const FOnlinePanelAudit& Audit, const TCHAR* Expected)
{
    return Audit.Texts.ContainsByPredicate([Expected](const FString& Text)
    {
        return Text.Equals(Expected, ESearchCase::CaseSensitive);
    });
}

bool HasTextContaining(const FOnlinePanelAudit& Audit, const TCHAR* Expected)
{
    return Audit.Texts.ContainsByPredicate([Expected](const FString& Text)
    {
        return Text.Contains(Expected, ESearchCase::CaseSensitive);
    });
}

FString ValidateFourPlayerLobby(const FOnlinePanelAudit& Audit)
{
    static const TCHAR* Names[] = { TEXT("PREVIEW HOST"), TEXT("EMBER GUEST"), TEXT("NEEDLE GUEST"), TEXT("SKIM GUEST") };
    if (!HasText(Audit, TEXT("4-PLAYER FREE-FOR-ALL"))) return TEXT("missing_four_player_title");
    for (const TCHAR* Name : Names)
        if (!HasTextContaining(Audit, Name)) return FString::Printf(TEXT("missing_roster_%s"), Name);
    if (!HasTextContaining(Audit, TEXT("4/4 connected")) || !HasTextContaining(Audit, TEXT("3/4 ready")))
        return TEXT("missing_four_player_lobby_counts");
    if (!Audit.PaintedButtons.Contains(TEXT("BACK"))) return TEXT("four_player_back_not_visible");
    if (!Audit.PaintedButtons.Contains(TEXT("READY")) || !Audit.PaintedButtons.Contains(TEXT("LEAVE ROOM")))
        return TEXT("four_player_ready_or_leave_not_visible");
    if (Audit.MinimumButtonHeight < 43.5f)
        return FString::Printf(TEXT("four_player_touch_target_under_44px_%.1f"), Audit.MinimumButtonHeight);
    return {};
}

FString ValidateAudit(const FOnlinePanelAudit& Audit, bool bLAN, bool bCompact)
{
    static const TCHAR* Required[] = {
        TEXT("PRIVATE 1V1"), TEXT("MULTIPLAYER"), TEXT("INTERNET"), TEXT("LOCAL NETWORK"),
        TEXT("SERVER ADDRESS"), TEXT("DISPLAY NAME"), TEXT("CREATE A MATCH"),
        TEXT("1V1 DUEL"), TEXT("4-PLAYER FREE-FOR-ALL"), TEXT("DEPLOYMENT SECTOR"),
        TEXT("SHATTERED RIFT"), TEXT("GLASS BASIN"), TEXT("IRON REACH"), TEXT("ROOM CODE"),
        TEXT("CREATE ROOM"), TEXT("JOIN ROOM"), TEXT("BACK")
    };
    for (const TCHAR* Label : Required)
        if (!HasText(Audit, Label)) return FString::Printf(TEXT("missing_label_%s"), Label);
    if (bLAN && !HasText(Audit, TEXT("FIND LOCAL SERVERS"))) return TEXT("missing_label_FIND_LOCAL_SERVERS");
    if (!Audit.bHasScrollBox) return TEXT("missing_scrollbox");
    if (Audit.ButtonsInViewport < 2) return TEXT("no_visible_navigation_buttons");
    if (Audit.MinimumButtonHeight < 43.5f)
        return FString::Printf(TEXT("touch_target_under_44px_%.1f"), Audit.MinimumButtonHeight);
    if (bCompact && Audit.ScrollRange <= 1.0f) return TEXT("compact_scroll_range_missing");
    for (int32 Left = 0; Left < Audit.ButtonBounds.Num(); ++Left)
        for (int32 Right = Left + 1; Right < Audit.ButtonBounds.Num(); ++Right)
        {
            const FBox2D& A = Audit.ButtonBounds[Left];
            const FBox2D& B = Audit.ButtonBounds[Right];
            const double OverlapX = FMath::Min(A.Max.X, B.Max.X) - FMath::Max(A.Min.X, B.Min.X);
            const double OverlapY = FMath::Min(A.Max.Y, B.Max.Y) - FMath::Max(A.Min.Y, B.Min.Y);
            if (OverlapX > 0.5 && OverlapY > 0.5) return TEXT("button_overlap");
        }
    return {};
}

void AccumulateAudit(const FOnlinePanelAudit& Audit)
{
    for (const FString& Label : Audit.PaintedButtons) PreviewRequest.PaintedButtons.Add(Label);
    PreviewRequest.MinimumButtonHeight = FMath::Min(
        PreviewRequest.MinimumButtonHeight, Audit.MinimumButtonHeight);
}

FString ValidateAllActions(bool bLAN)
{
    static const TCHAR* InternetActions[] = {
        TEXT("INTERNET"), TEXT("LOCAL NETWORK"), TEXT("SHATTERED RIFT"), TEXT("GLASS BASIN"),
        TEXT("IRON REACH"), TEXT("1V1 DUEL"), TEXT("4-PLAYER FREE-FOR-ALL"),
        TEXT("CREATE ROOM"), TEXT("JOIN ROOM"), TEXT("BACK")
    };
    for (const TCHAR* Label : InternetActions)
        if (!PreviewRequest.PaintedButtons.Contains(Label))
            return FString::Printf(TEXT("unpainted_action_%s"), Label);
    if (bLAN && !PreviewRequest.PaintedButtons.Contains(TEXT("FIND LOCAL SERVERS")))
        return TEXT("unpainted_action_FIND_LOCAL_SERVERS");
    if (PreviewRequest.MinimumButtonHeight < 43.5f)
        return FString::Printf(TEXT("touch_target_under_44px_%.1f"), PreviewRequest.MinimumButtonHeight);
    return {};
}

void ScrollPreview();

void CapturePreview()
{
    UWorld* World = PreviewRequest.World.Get();
    ACinderPlayerController* PC = World
        ? Cast<ACinderPlayerController>(World->GetFirstPlayerController()) : nullptr;
    ACinderBattlefield* Battle = PC ? PC->Battlefield() : nullptr;
    UCinderOnlineSubsystem* Online = PC ? PC->Online() : nullptr;
    const TSharedPtr<SWidget> Panel = PC ? PC->OnlinePanelForPreview() : nullptr;
    const bool bFour = PreviewRequest.Mode == TEXT("four");
    const bool bLAN = PreviewRequest.Mode == TEXT("lan") || bFour;
    if (!World || !World->IsGameWorld() || !PC || !Battle || !Online || !Panel
        || !Battle->IsMenu() || Battle->IsOnlineMatch() || (bFour && !Online->HasRoom())
        || Online->ConnectionMode() != (bLAN ? ECinderConnectionMode::LocalNetwork : ECinderConnectionMode::Internet))
    {
        FailPreview(TEXT("state_changed_before_capture"));
        return;
    }

    int32 Width = 0, Height = 0;
    PC->GetViewportSize(Width, Height);
    const FGeometry& PanelGeometry = Panel->GetCachedGeometry();
    const FVector2D PanelPosition = PanelGeometry.GetAbsolutePosition();
    const FVector2D PanelSize = PanelGeometry.GetAbsoluteSize();
    const FBox2D PanelBounds(PanelPosition, PanelPosition + PanelSize);
    FOnlinePanelAudit Audit;
    AuditWidget(Panel.ToSharedRef(), true, PanelBounds, Panel->GetTypeAsString(), Audit);
    for (const FString& Report : Audit.ButtonReports)
        UE_LOG(LogCinderOnlinePreview, Display, TEXT("CINDERLINE_ONLINE_PREVIEW_BUTTON %s"), *Report);
    if (bFour)
    {
        const FString Error = ValidateFourPlayerLobby(Audit);
        if (!Error.IsEmpty()) { FailPreview(Error); return; }
        const FString Directory = FPaths::ProjectSavedDir() / TEXT("OnlineLAN");
        IFileManager::Get().MakeDirectory(*Directory, true);
        const FString Filename = Directory / TEXT("four.png");
        FScreenshotRequest::RequestScreenshot(Filename, true, false);
        UE_LOG(LogCinderOnlinePreview, Display,
            TEXT("CINDERLINE_ONLINE_PREVIEW_FOUR viewport=%dx%d players=4 connected=4 ready=3 min_touch=%.1f include_ui=1 file=%s"),
            Width, Height, Audit.MinimumButtonHeight, *Filename);
        World->GetTimerManager().SetTimer(PreviewCaptureTimer,
            FTimerDelegate::CreateLambda([]()
            {
                CleanupFourPlayerFixture();
                ClearPreviewTimers();
                PreviewRequest = {};
            }), 0.8f, false);
        return;
    }
    const bool bCompact = Width > Height * 2 || Height < 500;
    const FString Error = ValidateAudit(Audit, bLAN, bCompact);
    if (!Error.IsEmpty())
    {
        const FString Directory = FPaths::ProjectSavedDir() / TEXT("OnlineLAN");
        IFileManager::Get().MakeDirectory(*Directory, true);
        const FString FailedFilename = Directory / (PreviewRequest.Mode + TEXT("-failed.png"));
        FScreenshotRequest::RequestScreenshot(FailedFilename, true, false);
        UE_LOG(LogCinderOnlinePreview, Error,
            TEXT("CINDERLINE_ONLINE_PREVIEW_AUDIT mode=%s error=%s raw_buttons=%d labelled_buttons=%d min_touch=%.1f failed_file=%s"),
            *PreviewRequest.Mode, *Error, Audit.RawButtons, Audit.VisibleButtons,
            Audit.MinimumButtonHeight, *FailedFilename);
        World->GetTimerManager().SetTimer(PreviewReadyTimer,
            FTimerDelegate::CreateLambda([Error]() { FailPreview(Error); }), 0.8f, false);
        return;
    }
    if (!Audit.PaintedButtons.Contains(TEXT("BACK")))
    {
        FailPreview(PreviewRequest.ScrollStage > 0
            ? TEXT("sticky_back_not_visible_after_scroll") : TEXT("sticky_back_not_visible_initially"));
        return;
    }
    AccumulateAudit(Audit);

    const FString Directory = FPaths::ProjectSavedDir() / TEXT("OnlineLAN");
    IFileManager::Get().MakeDirectory(*Directory, true);
    const FString Filename = Directory / (PreviewRequest.Mode + TEXT(".png"));
    if (!PreviewRequest.bInitialCaptured)
    {
        FScreenshotRequest::RequestScreenshot(Filename, true, false);
        PreviewRequest.bInitialCaptured = true;
        if (Audit.ScrollRange > 1.0f)
        {
            if (!Audit.ScrollBox)
            {
                FailPreview(TEXT("scrollbox_missing_for_scroll"));
                return;
            }
            World->GetTimerManager().SetTimer(PreviewCaptureTimer,
                FTimerDelegate::CreateStatic(&ScrollPreview), 0.8f, false);
            return;
        }
        World->GetTimerManager().SetTimer(PreviewCaptureTimer,
            FTimerDelegate::CreateStatic(&CapturePreview), 0.8f, false);
        return;
    }
    if (PreviewRequest.ScrollStage == 1)
    {
        const FString MiddleFilename = Directory / (PreviewRequest.Mode + TEXT("-middle.png"));
        FScreenshotRequest::RequestScreenshot(MiddleFilename, true, false);
        World->GetTimerManager().SetTimer(PreviewCaptureTimer,
            FTimerDelegate::CreateStatic(&ScrollPreview), 0.8f, false);
        return;
    }
    const FString CompleteError = ValidateAllActions(bLAN);
    if (!CompleteError.IsEmpty())
    {
        FailPreview(CompleteError);
        return;
    }
    const FString ScrolledFilename = Directory / (PreviewRequest.Mode + TEXT("-scrolled.png"));
    FScreenshotRequest::RequestScreenshot(ScrolledFilename, true, false);
    UE_LOG(LogCinderOnlinePreview, Display,
        TEXT("CINDERLINE_ONLINE_PREVIEW mode=%s viewport=%dx%d panel=(%.1f,%.1f) labels=%d buttons=%d onscreen=%d min_touch=%.1f scroll_range=%.1f include_ui=1 file=%s scrolled_file=%s"),
        *PreviewRequest.Mode, Width, Height, PanelSize.X, PanelSize.Y, Audit.Texts.Num(),
        PreviewRequest.PaintedButtons.Num(), Audit.ButtonsInViewport, PreviewRequest.MinimumButtonHeight,
        Audit.ScrollRange, *Filename, *ScrolledFilename);
    ClearPreviewTimers();
    PreviewRequest = {};
}

void ScrollPreview()
{
    UWorld* World = PreviewRequest.World.Get();
    ACinderPlayerController* PC = World
        ? Cast<ACinderPlayerController>(World->GetFirstPlayerController()) : nullptr;
    const TSharedPtr<SWidget> Panel = PC ? PC->OnlinePanelForPreview() : nullptr;
    if (!World || !Panel)
    {
        FailPreview(TEXT("panel_changed_before_scroll"));
        return;
    }
    const FGeometry& PanelGeometry = Panel->GetCachedGeometry();
    const FVector2D PanelPosition = PanelGeometry.GetAbsolutePosition();
    const FVector2D PanelSize = PanelGeometry.GetAbsoluteSize();
    FOnlinePanelAudit Audit;
    AuditWidget(Panel.ToSharedRef(), true, FBox2D(PanelPosition, PanelPosition + PanelSize),
        Panel->GetTypeAsString(), Audit);
    if (!Audit.ScrollBox || Audit.ScrollRange <= 1.0f)
    {
        FailPreview(TEXT("scroll_range_disappeared"));
        return;
    }
    if (PreviewRequest.ScrollStage == 0)
    {
        Audit.ScrollBox->SetScrollOffset(Audit.ScrollRange * 0.5f);
        PreviewRequest.ScrollStage = 1;
    }
    else
    {
        Audit.ScrollBox->ScrollToEnd();
        PreviewRequest.ScrollStage = 2;
    }
    World->GetTimerManager().SetTimer(PreviewCaptureTimer,
        FTimerDelegate::CreateStatic(&CapturePreview), 0.5f, false);
}

void ConfigureFourPlayerPreview(UWorld* World, ACinderPlayerController* PC,
    UCinderOnlineSubsystem* Host)
{
    static const TCHAR* GuestNames[] = { TEXT("EMBER GUEST"), TEXT("NEEDLE GUEST"), TEXT("SKIM GUEST") };
    if (Host->State() == ECinderOnlineState::Error)
    { FailPreview(FString(TEXT("four_player_host_error_")) + Host->ErrorText()); return; }
    for (int32 Index = 0; Index < 3; ++Index)
        if (const UCinderOnlineSubsystem* Guest = PreviewRequest.Guests[Index].Get();
            Guest && Guest->State() == ECinderOnlineState::Error)
        { FailPreview(FString::Printf(TEXT("four_player_guest_%d_error_%s"), Index + 1, *Guest->ErrorText())); return; }

    const FString Endpoint = FPlatformMisc::GetEnvironmentVariable(TEXT("CINDERLINE_TEST_SERVER"));
    if (!Endpoint.StartsWith(TEXT("ws://127.0.0.1:"), ESearchCase::CaseSensitive)
        || !Endpoint.EndsWith(TEXT("/play"), ESearchCase::CaseSensitive))
    { FailPreview(TEXT("four_player_requires_explicit_loopback_test_server")); return; }

    if (!PreviewRequest.bFourRoomCreated)
    {
        if (!Host->SetConnectionMode(ECinderConnectionMode::LocalNetwork))
        { FailPreview(TEXT("four_player_host_mode_rejected")); return; }
        for (int32 Index = 0; Index < 3; ++Index)
        {
            UGameInstance* Instance = NewObject<UGameInstance>(GEngine);
            if (!Instance) { FailPreview(TEXT("four_player_guest_instance_missing")); return; }
            Instance->AddToRoot();
            Instance->InitializeStandalone(FName(*FString::Printf(TEXT("CinderOnlinePreviewGuest%d"), Index + 1)));
            UCinderOnlineSubsystem* Guest = Instance->GetSubsystem<UCinderOnlineSubsystem>();
            if (!Guest || !Guest->SetConnectionMode(ECinderConnectionMode::LocalNetwork))
            {
                DestroyGuestInstance(Instance);
                FailPreview(TEXT("four_player_guest_subsystem_missing")); return;
            }
            PreviewRequest.GuestInstances[Index] = Instance;
            PreviewRequest.Guests[Index] = Guest;
        }
        Host->CreateRoom(Endpoint, TEXT("PREVIEW HOST"), 2, 4);
        PreviewRequest.bFourRoomCreated = true;
        return;
    }
    if (!Host->HasRoom() || Host->State() != ECinderOnlineState::Lobby) return;

    if (PreviewRequest.NextGuest < 3)
    {
        if (PreviewRequest.NextGuest > 0
            && !PreviewRequest.Guests[PreviewRequest.NextGuest - 1]->HasRoom()) return;
        UCinderOnlineSubsystem* Guest = PreviewRequest.Guests[PreviewRequest.NextGuest].Get();
        if (!Guest) { FailPreview(TEXT("four_player_guest_lost")); return; }
        Guest->JoinRoom(Endpoint, GuestNames[PreviewRequest.NextGuest], Host->RoomCode());
        ++PreviewRequest.NextGuest;
        return;
    }
    for (int32 Index = 0; Index < 3; ++Index)
        if (!PreviewRequest.Guests[Index].IsValid() || !PreviewRequest.Guests[Index]->HasRoom()) return;
    for (int32 Seat = 0; Seat < 4; ++Seat)
        if (!Host->SeatConnected(Seat) || Host->SeatName(Seat).IsEmpty()) return;

    if (!PreviewRequest.bFourReadyRequested)
    {
        for (int32 Index = 0; Index < 3; ++Index) PreviewRequest.Guests[Index]->SetReady(true);
        PreviewRequest.bFourReadyRequested = true;
        return;
    }
    if (Host->SeatReady(0)) { FailPreview(TEXT("four_player_host_unexpectedly_ready")); return; }
    for (int32 Seat = 1; Seat < 4; ++Seat) if (!Host->SeatReady(Seat)) return;

    if (!PreviewRequest.bOpened)
    {
        PC->ExecuteAction(TEXT("online"));
        PreviewRequest.bOpened = true;
        return;
    }
    if (!PC->OnlinePanelForPreview().IsValid()) return;
    World->GetTimerManager().ClearTimer(PreviewReadyTimer);
    World->GetTimerManager().SetTimer(PreviewCaptureTimer,
        FTimerDelegate::CreateStatic(&CapturePreview), 0.8f, false);
}

void ConfigurePreview()
{
    UWorld* World = PreviewRequest.World.Get();
    ACinderPlayerController* PC = World
        ? Cast<ACinderPlayerController>(World->GetFirstPlayerController()) : nullptr;
    ACinderBattlefield* Battle = PC ? PC->Battlefield() : nullptr;
    ACinderHUD* HUD = PC ? Cast<ACinderHUD>(PC->GetHUD()) : nullptr;
    UCinderOnlineSubsystem* Online = PC ? PC->Online() : nullptr;
    if (!World || !World->IsGameWorld() || !PC || !Battle || !HUD || !Online
        || !Battle->IsMenu() || Battle->IsOnlineMatch() || PC->IsHelpOpen())
    {
        FailPreview(TEXT("fresh_menu_state_changed"));
        return;
    }
    if (++PreviewRequest.Attempts > 100)
    {
        FailPreview(TEXT("panel_not_ready"));
        return;
    }
    if (PC->IsTutorialOfferPending())
    {
        HUD->TapPreviewAction(TEXT("onboardskip"));
        return;
    }

    if (PreviewRequest.Mode == TEXT("four"))
    {
        ConfigureFourPlayerPreview(World, PC, Online);
        return;
    }

    if (!PreviewRequest.bOpened)
    {
        const bool bLAN = PreviewRequest.Mode == TEXT("lan");
        if (!Online->SetConnectionMode(bLAN ? ECinderConnectionMode::LocalNetwork : ECinderConnectionMode::Internet))
        {
            FailPreview(TEXT("connection_mode_rejected"));
            return;
        }
        PC->ExecuteAction(TEXT("online"));
        PreviewRequest.bOpened = true;
        return;
    }
    if (!PC->OnlinePanelForPreview().IsValid()) return;

    World->GetTimerManager().ClearTimer(PreviewReadyTimer);
    World->GetTimerManager().SetTimer(PreviewCaptureTimer,
        FTimerDelegate::CreateStatic(&CapturePreview), 0.8f, false);
}

FAutoConsoleCommandWithWorldAndArgs OnlinePreviewCommand(
    TEXT("cinder.onlinepreview"),
    TEXT("DEVELOPMENT: unattended fresh menu only. Opens and captures the real Slate multiplayer panel for internet|lan."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
    {
        const FString Mode = Args.Num() == 1 ? Args[0].ToLower() : FString();
        ACinderPlayerController* PC = World
            ? Cast<ACinderPlayerController>(World->GetFirstPlayerController()) : nullptr;
        ACinderBattlefield* Battle = PC ? PC->Battlefield() : nullptr;
        if (Args.Num() != 1 || (Mode != TEXT("internet") && Mode != TEXT("lan") && Mode != TEXT("four"))
            || !FApp::IsUnattended() || !World || !World->IsGameWorld() || !PC || !Battle
            || !Battle->IsMenu() || Battle->IsOnlineMatch() || PC->IsHelpOpen()
            || PC->OnlinePanelForPreview().IsValid())
        {
            UE_LOG(LogCinderOnlinePreview, Warning,
                TEXT("CINDERLINE_ONLINE_PREVIEW refused=requires_unattended_fresh_menu_and_mode_internet_lan_or_four"));
            return;
        }

        ClearPreviewTimers();
        PreviewRequest.World = World;
        PreviewRequest.Mode = Mode;
        World->GetTimerManager().SetTimer(PreviewReadyTimer,
            FTimerDelegate::CreateStatic(&ConfigurePreview), 0.1f, true, 0.1f);
    }));
}

#endif
