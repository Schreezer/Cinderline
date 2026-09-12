#include "Presentation/CinderOnlinePanel.h"

#include "Presentation/CinderOnlineSubsystem.h"
#include "Engine/GameViewportClient.h"
#include "Engine/UserInterfaceSettings.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformApplicationMisc.h"
#include "InputCoreTypes.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SDPIScaler.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
const FLinearColor PanelInk(0.012f, 0.027f, 0.037f, 0.99f);
const FLinearColor White(0.88f, 0.94f, 0.97f);
const FLinearColor Muted(0.38f, 0.50f, 0.55f);
const FLinearColor Mint(0.10f, 0.90f, 0.74f);
const FLinearColor Amber(1.0f, 0.64f, 0.24f);

class SCinderOnlinePanel final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SCinderOnlinePanel) {}
        SLATE_ARGUMENT(TWeakObjectPtr<UCinderOnlineSubsystem>, Online)
        SLATE_ARGUMENT(TFunction<void()>, OnClose)
    SLATE_END_ARGS()

    void Construct(const FArguments& Args)
    {
        Online = Args._Online;
        OnClose = Args._OnClose;
        if (const UCinderOnlineSubsystem* Service = Online.Get())
        {
            InitialEndpoint = Service->Endpoint();
            InitialName = Service->PlayerName();
            InitialCode = Service->RoomCode();
            SelectedMap = FMath::Clamp(Service->MapIndex(), 0, 2);
        }

        ChildSlot
        [
            SNew(SDPIScaler)
            .DPIScale(this, &SCinderOnlinePanel::PanelDPIScale)
            [
                SNew(SBorder)
                .BorderImage(FAppStyle::GetBrush("WhiteBrush"))
                .BorderBackgroundColor(FLinearColor(0.002f, 0.008f, 0.014f, 0.97f))
                .Padding(16)
                [
                    SNew(SScrollBox)
                    .Orientation(Orient_Vertical)
                    .ScrollWhenFocusChanges(EScrollWhenFocusChanges::InstantScroll)
                    + SScrollBox::Slot()
                    [
                        SNew(SBox)
                        .HAlign(HAlign_Center)
                        [
                            SNew(SBox)
                            .MaxDesiredWidth(640)
                            [
                                SNew(SBorder)
                                .BorderImage(FAppStyle::GetBrush("WhiteBrush"))
                                .BorderBackgroundColor(PanelInk)
                                .Padding(FMargin(24, 20))
                                [
                                    BuildContent()
                                ]
                            ]
                        ]
                    ]
                ]
            ]
        ];
    }

    virtual bool SupportsKeyboardFocus() const override { return true; }

    virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& KeyEvent) override
    {
        if (KeyEvent.GetKey() == EKeys::Escape) return Back();
        return SCompoundWidget::OnKeyDown(MyGeometry, KeyEvent);
    }

    virtual FReply OnPreviewKeyDown(const FGeometry& MyGeometry, const FKeyEvent& KeyEvent) override
    {
        if (KeyEvent.GetKey() == EKeys::Escape) return Back();
        return SCompoundWidget::OnPreviewKeyDown(MyGeometry, KeyEvent);
    }

private:
    static FSlateFontInfo PanelFont(int32 Size, bool bBold = false)
    {
        FSlateFontInfo Font = FAppStyle::GetFontStyle(bBold ? "NormalFontBold" : "NormalFont");
        Font.Size = Size;
        return Font;
    }

    float PanelDPIScale() const
    {
        const UCinderOnlineSubsystem* Service = Online.Get();
        const UWorld* World = Service ? Service->GetWorld() : nullptr;
        const UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr;
        if (!Viewport) return 1.0f;

        FVector2D Size = FVector2D::ZeroVector;
        Viewport->GetViewportSize(Size);
        if (Size.X <= 0 || Size.Y <= 0) return 1.0f;
        const float WindowDPI = FMath::Max(1.0f, Viewport->GetDPIScale());
        const bool bCompact = Size.X / Size.Y > 2.0f || Size.Y / WindowDPI < 500.0f;
        const float DesiredScale = bCompact
            ? FMath::Min(Size.X / 667.0f, Size.Y / 375.0f)
            : FMath::Max(0.45f, FMath::Min(Size.X / 1280.0f, Size.Y / 720.0f));
        const FIntPoint PixelSize(FMath::RoundToInt(Size.X), FMath::RoundToInt(Size.Y));
        const float GameUIScale = FMath::Max(0.01f, GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(PixelSize));
        // The parent game layer already applies the project UI curve. Divide it
        // out, then apply the same viewport scale used by the Canvas HUD.
        return DesiredScale / GameUIScale;
    }

    static TSharedRef<SWidget> TouchTarget(const TSharedRef<SWidget>& Widget)
    {
        return SNew(SBox).MinDesiredHeight(44)[Widget];
    }

    TSharedRef<SWidget> BuildContent()
    {
        return SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(STextBlock)
                .Text(FText::FromString(TEXT("PRIVATE 1V1")))
                .ColorAndOpacity(Mint)
                .Font(PanelFont(12, true))
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 18)
            [
                SNew(STextBlock)
                .Text(FText::FromString(TEXT("CINDERLINE ONLINE")))
                .ColorAndOpacity(White)
                .Font(PanelFont(24, true))
            ]
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SBox)
                .Visibility(this, &SCinderOnlinePanel::ConnectionFormVisibility)
                [BuildConnectionForm()]
            ]
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SBorder)
                .Visibility(this, &SCinderOnlinePanel::LobbyVisibility)
                .BorderImage(FAppStyle::GetBrush("WhiteBrush"))
                .BorderBackgroundColor(FLinearColor(0.02f, 0.07f, 0.075f, 1))
                .Padding(16)
                [
                    BuildLobby()
                ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 14, 0, 0)
            [
                SNew(STextBlock)
                .Text(this, &SCinderOnlinePanel::FeedbackText)
                .ColorAndOpacity(this, &SCinderOnlinePanel::FeedbackColor)
                .Font(PanelFont(14))
                .AutoWrapText(true)
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 12, 0, 0)
            [
                SNew(SUniformGridPanel)
                .SlotPadding(FMargin(3))
                + SUniformGridPanel::Slot(0, 0)
                [
                    SNew(SBox)
                    .MinDesiredHeight(44)
                    .Visibility(this, &SCinderOnlinePanel::ReconnectVisibility)
                    [
                        SNew(SButton)
                        .OnClicked(this, &SCinderOnlinePanel::Reconnect)
                        [SNew(STextBlock).Text(FText::FromString(TEXT("RECONNECT"))).Font(PanelFont(14, true)).Justification(ETextJustify::Center)]
                    ]
                ]
                + SUniformGridPanel::Slot(1, 0)
                [
                    TouchTarget(
                        SNew(SButton)
                        .OnClicked(this, &SCinderOnlinePanel::Back)
                        [SNew(STextBlock).Text(FText::FromString(TEXT("BACK"))).Font(PanelFont(14, true)).Justification(ETextJustify::Center)])
                ]
            ];
    }

    TSharedRef<SWidget> BuildConnectionForm()
    {
        return SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [SNew(STextBlock).Text(FText::FromString(TEXT("SERVER ENDPOINT"))).Font(PanelFont(12, true)).ColorAndOpacity(Muted)]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 5, 0, 12)
            [
                TouchTarget(
                    SAssignNew(EndpointField, SEditableTextBox)
                    .Text(FText::FromString(InitialEndpoint))
                    .HintText(FText::FromString(TEXT("wss://server.example/play")))
                    .Font(PanelFont(14))
                    .IsEnabled(this, &SCinderOnlinePanel::CanEditConnection)
                    .SelectAllTextWhenFocused(true))
            ]
            + SVerticalBox::Slot().AutoHeight()
            [SNew(STextBlock).Text(FText::FromString(TEXT("DISPLAY NAME"))).Font(PanelFont(12, true)).ColorAndOpacity(Muted)]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 5, 0, 12)
            [
                TouchTarget(
                    SAssignNew(NameField, SEditableTextBox)
                    .Text(FText::FromString(InitialName))
                    .HintText(FText::FromString(TEXT("Commander")))
                    .Font(PanelFont(14))
                    .IsEnabled(this, &SCinderOnlinePanel::CanEditConnection)
                    .SelectAllTextWhenFocused(true))
            ]
            + SVerticalBox::Slot().AutoHeight()
            [SNew(STextBlock).Text(FText::FromString(TEXT("DEPLOYMENT SECTOR"))).Font(PanelFont(12, true)).ColorAndOpacity(Muted)]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 5, 0, 12)
            [
                SNew(SUniformGridPanel)
                .SlotPadding(FMargin(3))
                + SUniformGridPanel::Slot(0, 0)[MapButton(TEXT("SHATTERED RIFT"), 0)]
                + SUniformGridPanel::Slot(1, 0)[MapButton(TEXT("GLASS BASIN"), 1)]
                + SUniformGridPanel::Slot(2, 0)[MapButton(TEXT("IRON REACH"), 2)]
            ]
            + SVerticalBox::Slot().AutoHeight()
            [SNew(STextBlock).Text(FText::FromString(TEXT("ROOM CODE"))).Font(PanelFont(12, true)).ColorAndOpacity(Muted)]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 5, 0, 10)
            [
                TouchTarget(
                    SAssignNew(CodeField, SEditableTextBox)
                    .Text(FText::FromString(InitialCode))
                    .HintText(FText::FromString(TEXT("6 characters")))
                    .Font(PanelFont(14))
                    .IsEnabled(this, &SCinderOnlinePanel::CanEditConnection)
                    .SelectAllTextWhenFocused(true)
                    .OnTextChanged(this, &SCinderOnlinePanel::CodeChanged))
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 14)
            [
                SNew(SUniformGridPanel)
                .SlotPadding(FMargin(3))
                + SUniformGridPanel::Slot(0, 0)
                [
                    TouchTarget(
                        SNew(SButton)
                        .IsEnabled(this, &SCinderOnlinePanel::CanStartConnection)
                        .OnClicked(this, &SCinderOnlinePanel::CreateRoom)
                        [SNew(STextBlock).Text(FText::FromString(TEXT("CREATE ROOM"))).Font(PanelFont(14, true)).Justification(ETextJustify::Center)])
                ]
                + SUniformGridPanel::Slot(1, 0)
                [
                    TouchTarget(
                        SNew(SButton)
                        .IsEnabled(this, &SCinderOnlinePanel::CanJoin)
                        .OnClicked(this, &SCinderOnlinePanel::JoinRoom)
                        [SNew(STextBlock).Text(FText::FromString(TEXT("JOIN ROOM"))).Font(PanelFont(14, true)).Justification(ETextJustify::Center)])
                ]
            ];
    }

    TSharedRef<SWidget> BuildLobby()
    {
        return SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(STextBlock).Text(FText::FromString(TEXT("ROOM"))).Font(PanelFont(12, true)).ColorAndOpacity(Muted)
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 3, 0, 10)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1)
                [
                    SNew(STextBlock)
                    .Text(this, &SCinderOnlinePanel::RoomText)
                    .ColorAndOpacity(Mint)
                    .Font(PanelFont(24, true))
                ]
                + SHorizontalBox::Slot().AutoWidth()
                [
                    TouchTarget(
                        SNew(SButton).OnClicked(this, &SCinderOnlinePanel::CopyInvite)
                        [SNew(STextBlock).Text(FText::FromString(TEXT("COPY INVITE"))).Font(PanelFont(14, true))])
                ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 2)
            [PlayerRow(0)]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 12)
            [PlayerRow(1)]
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SBox)
                .Visibility(this, &SCinderOnlinePanel::LeaveRoomVisibility)
                [
                    SNew(SUniformGridPanel)
                    .SlotPadding(FMargin(3))
                    + SUniformGridPanel::Slot(0, 0)
                    [
                        TouchTarget(
                            SNew(SButton)
                            .IsEnabled(this, &SCinderOnlinePanel::CanSetReady)
                            .OnClicked(this, &SCinderOnlinePanel::ToggleReady)
                            [SNew(STextBlock).Text(this, &SCinderOnlinePanel::ReadyButtonText).Font(PanelFont(14, true)).Justification(ETextJustify::Center)])
                    ]
                    + SUniformGridPanel::Slot(1, 0)
                    [
                        TouchTarget(
                            SNew(SButton)
                            .OnClicked(this, &SCinderOnlinePanel::LeaveRoom)
                            [SNew(STextBlock).Text(FText::FromString(TEXT("LEAVE ROOM"))).Font(PanelFont(14, true)).Justification(ETextJustify::Center)])
                    ]
                ]
            ];
    }

    TSharedRef<SWidget> MapButton(const TCHAR* Label, int32 MapIndex)
    {
        return TouchTarget(
            SNew(SButton)
                .IsEnabled(this, &SCinderOnlinePanel::CanEditConnection)
                .ButtonColorAndOpacity_Lambda([this, MapIndex]()
                {
                    return SelectedMap == MapIndex ? FLinearColor(0.04f, 0.34f, 0.29f, 1) : FLinearColor::White;
                })
                .OnClicked_Lambda([this, MapIndex]()
                {
                    SelectedMap = MapIndex;
                    return FReply::Handled();
                })
                [
                    SNew(STextBlock).Text(FText::FromString(Label)).Font(PanelFont(14, true)).Justification(ETextJustify::Center)
                ]);
    }

    TSharedRef<SWidget> PlayerRow(int32 Seat)
    {
        return SNew(SBorder)
            .BorderImage(FAppStyle::GetBrush("WhiteBrush"))
            .BorderBackgroundColor(FLinearColor(0.015f, 0.035f, 0.045f, 1))
            .Padding(FMargin(10, 8))
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1)
                [
                    SNew(STextBlock)
                    .Text_Lambda([this, Seat]() { return PlayerNameText(Seat); })
                    .Font(PanelFont(14))
                    .ColorAndOpacity(White)
                ]
                + SHorizontalBox::Slot().AutoWidth()
                [
                    SNew(STextBlock)
                    .Text_Lambda([this, Seat]() { return PlayerStateText(Seat); })
                    .Font(PanelFont(12, true))
                    .ColorAndOpacity_Lambda([this, Seat]() { return PlayerStateColor(Seat); })
                ]
            ];
    }

    bool CanEditConnection() const
    {
        const UCinderOnlineSubsystem* Service = Online.Get();
        return Service && !Service->HasRoom() && Service->State() != ECinderOnlineState::Connecting;
    }

    EVisibility ConnectionFormVisibility() const
    {
        const UCinderOnlineSubsystem* Service = Online.Get();
        return Service && Service->HasRoom() ? EVisibility::Collapsed : EVisibility::Visible;
    }

    bool CanStartConnection() const
    {
        if (!CanEditConnection() || !EndpointField.IsValid() || !NameField.IsValid()) return false;
        return !EndpointField->GetText().ToString().TrimStartAndEnd().IsEmpty()
            && !NameField->GetText().ToString().TrimStartAndEnd().IsEmpty();
    }

    bool CanJoin() const
    {
        return CanStartConnection() && CodeField.IsValid() && NormalizedCode().Len() == 6;
    }

    bool CanSetReady() const
    {
        const UCinderOnlineSubsystem* Service = Online.Get();
        return Service && Service->State() == ECinderOnlineState::Lobby && Service->LocalSeat() >= 0;
    }

    EVisibility LobbyVisibility() const
    {
        const UCinderOnlineSubsystem* Service = Online.Get();
        return Service && Service->HasRoom() ? EVisibility::Visible : EVisibility::Collapsed;
    }

    EVisibility LeaveRoomVisibility() const
    {
        const UCinderOnlineSubsystem* Service = Online.Get();
        return Service && Service->HasMatch() ? EVisibility::Collapsed : EVisibility::Visible;
    }

    EVisibility ReconnectVisibility() const
    {
        const UCinderOnlineSubsystem* Service = Online.Get();
        if (!Service || !Service->HasRoom()) return EVisibility::Collapsed;
        return Service->State() == ECinderOnlineState::Reconnecting || Service->State() == ECinderOnlineState::Error
            ? EVisibility::Visible : EVisibility::Collapsed;
    }

    FString NormalizedCode() const
    {
        FString Code = CodeField.IsValid() ? CodeField->GetText().ToString() : FString();
        Code.TrimStartAndEndInline();
        Code.ToUpperInline();
        return Code;
    }

    void CodeChanged(const FText& NewText)
    {
        FString Code = NewText.ToString().ToUpper();
        FString Filtered;
        Filtered.Reserve(6);
        for (const TCHAR Character : Code)
        {
            if (FChar::IsAlnum(Character) && Filtered.Len() < 6) Filtered.AppendChar(Character);
        }
        if (Filtered != NewText.ToString() && CodeField.IsValid()) CodeField->SetText(FText::FromString(Filtered));
        LocalError.Reset();
        bLocalError = false;
    }

    bool ReadConnection(FString& Endpoint, FString& Name)
    {
        Endpoint = EndpointField.IsValid() ? EndpointField->GetText().ToString().TrimStartAndEnd() : FString();
        Name = NameField.IsValid() ? NameField->GetText().ToString().TrimStartAndEnd() : FString();
        if (Endpoint.IsEmpty()) LocalError = TEXT("Enter the WebSocket endpoint.");
        else if (Name.IsEmpty()) LocalError = TEXT("Enter a display name.");
        else return true;
        bLocalError = true;
        return false;
    }

    FReply CreateRoom()
    {
        FString Endpoint, Name;
        if (UCinderOnlineSubsystem* Service = Online.Get(); Service && ReadConnection(Endpoint, Name))
        {
            LocalError.Reset();
            bLocalError = false;
            Service->CreateRoom(Endpoint, Name, SelectedMap);
        }
        return FReply::Handled();
    }

    FReply JoinRoom()
    {
        FString Endpoint, Name;
        const FString Code = NormalizedCode();
        if (Code.Len() != 6)
        {
            LocalError = TEXT("Room codes contain 6 letters or numbers.");
            bLocalError = true;
        }
        else if (UCinderOnlineSubsystem* Service = Online.Get(); Service && ReadConnection(Endpoint, Name))
        {
            LocalError.Reset();
            bLocalError = false;
            Service->JoinRoom(Endpoint, Name, Code);
        }
        return FReply::Handled();
    }

    FReply ToggleReady()
    {
        if (UCinderOnlineSubsystem* Service = Online.Get())
        {
            const int32 Seat = Service->LocalSeat();
            if (Seat >= 0) Service->SetReady(!Service->SeatReady(Seat));
        }
        return FReply::Handled();
    }

    FReply Reconnect()
    {
        if (UCinderOnlineSubsystem* Service = Online.Get()) Service->Reconnect();
        return FReply::Handled();
    }

    FReply CopyInvite()
    {
        if (const UCinderOnlineSubsystem* Service = Online.Get())
        {
            const FString Invite = FString::Printf(TEXT("Join my Cinderline room %s at %s"), *Service->RoomCode(), *Service->Endpoint());
            FPlatformApplicationMisc::ClipboardCopy(*Invite);
            LocalError = TEXT("Invite copied.");
            bLocalError = false;
        }
        return FReply::Handled();
    }

    FReply LeaveRoom()
    {
        if (UCinderOnlineSubsystem* Service = Online.Get(); Service && !Service->HasMatch()) Service->Leave();
        Close();
        return FReply::Handled();
    }

    FReply Back()
    {
        if (UCinderOnlineSubsystem* Service = Online.Get(); Service && !Service->HasMatch()) Service->Leave();
        Close();
        return FReply::Handled();
    }

    void Close()
    {
        if (OnClose)
        {
            TFunction<void()> Callback = MoveTemp(OnClose);
            Callback();
        }
    }

    FText RoomText() const
    {
        const UCinderOnlineSubsystem* Service = Online.Get();
        return FText::FromString(Service && !Service->RoomCode().IsEmpty() ? Service->RoomCode() : TEXT("WAITING"));
    }

    FText PlayerNameText(int32 Seat) const
    {
        const UCinderOnlineSubsystem* Service = Online.Get();
        if (!Service) return FText::FromString(TEXT("Unavailable"));
        const FString Name = Service->SeatName(Seat);
        const FString Prefix = Seat == Service->LocalSeat() ? TEXT("YOU  ") : TEXT("RIVAL  ");
        return FText::FromString(Prefix + (Name.IsEmpty() ? TEXT("Waiting for player") : Name));
    }

    FText PlayerStateText(int32 Seat) const
    {
        const UCinderOnlineSubsystem* Service = Online.Get();
        if (!Service || Service->SeatName(Seat).IsEmpty()) return FText::FromString(TEXT("OPEN"));
        if (!Service->SeatConnected(Seat)) return FText::FromString(TEXT("DISCONNECTED"));
        return FText::FromString(Service->SeatReady(Seat) ? TEXT("READY") : TEXT("NOT READY"));
    }

    FSlateColor PlayerStateColor(int32 Seat) const
    {
        const UCinderOnlineSubsystem* Service = Online.Get();
        return Service && Service->SeatConnected(Seat) && Service->SeatReady(Seat) ? FSlateColor(Mint) : FSlateColor(Amber);
    }

    FText ReadyButtonText() const
    {
        const UCinderOnlineSubsystem* Service = Online.Get();
        const int32 Seat = Service ? Service->LocalSeat() : -1;
        return FText::FromString(Service && Seat >= 0 && Service->SeatReady(Seat) ? TEXT("NOT READY") : TEXT("READY"));
    }

    FText FeedbackText() const
    {
        if (!LocalError.IsEmpty()) return FText::FromString(LocalError);
        const UCinderOnlineSubsystem* Service = Online.Get();
        if (!Service) return FText::FromString(TEXT("Online service unavailable."));
        if (!Service->ErrorText().IsEmpty()) return FText::FromString(Service->ErrorText());
        switch (Service->State())
        {
        case ECinderOnlineState::Connecting: return FText::FromString(TEXT("Connecting..."));
        case ECinderOnlineState::Starting: return FText::FromString(TEXT("Both players ready. Starting match..."));
        case ECinderOnlineState::Reconnecting: return FText::FromString(TEXT("Connection lost. Reconnecting..."));
        default: return FText::FromString(Service->StatusText());
        }
    }

    FSlateColor FeedbackColor() const
    {
        const UCinderOnlineSubsystem* Service = Online.Get();
        const bool bError = bLocalError || !Service || Service->State() == ECinderOnlineState::Error;
        return FSlateColor(bError ? Amber : Muted);
    }

    TWeakObjectPtr<UCinderOnlineSubsystem> Online;
    TFunction<void()> OnClose;
    TSharedPtr<SEditableTextBox> EndpointField;
    TSharedPtr<SEditableTextBox> NameField;
    TSharedPtr<SEditableTextBox> CodeField;
    FString InitialEndpoint;
    FString InitialName;
    FString InitialCode;
    FString LocalError;
    bool bLocalError = false;
    int32 SelectedMap = 0;
};
}

TSharedRef<SWidget> MakeCinderOnlinePanel(UCinderOnlineSubsystem* Online, TFunction<void()> OnClose)
{
    return SNew(SCinderOnlinePanel)
        .Online(TWeakObjectPtr<UCinderOnlineSubsystem>(Online))
        .OnClose(MoveTemp(OnClose));
}
