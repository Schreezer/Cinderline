#include "Presentation/CinderOnlinePanel.h"

#include "Presentation/CinderOnlineSubsystem.h"
#include "Presentation/CinderLANDiscovery.h"
#include "Presentation/CinderTeamColors.h"
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
            SelectedPlayerCount = Service->PlayerCount();
            bLocalNetwork = Service->ConnectionMode() == ECinderConnectionMode::LocalNetwork;
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
                    SNew(SBox)
                    .HAlign(HAlign_Center)
                    [
                        SNew(SBox)
                        .MaxDesiredWidth(640)
                        [
                            SNew(SBorder)
                            .BorderImage(FAppStyle::GetBrush("WhiteBrush"))
                            .BorderBackgroundColor(PanelInk)
                            .Padding(FMargin(24, 12))
                            [
                                SNew(SVerticalBox)
                                + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
                                [BuildTitle()]
                                + SVerticalBox::Slot().FillHeight(1)
                                [
                                    SNew(SScrollBox)
                                    .Orientation(Orient_Vertical)
                                    .ScrollWhenFocusChanges(EScrollWhenFocusChanges::InstantScroll)
                                    + SScrollBox::Slot()[BuildContent()]
                                ]
                                + SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
                                [BuildLobbyActions()]
                            ]
                        ]
                    ]
                ]
            ]
        ];
    }

    virtual bool SupportsKeyboardFocus() const override { return true; }

    virtual ~SCinderOnlinePanel() override { LANDiscovery.Stop(); }

    virtual void Tick(const FGeometry& Geometry, double Now, float DeltaTime) override
    {
        SCompoundWidget::Tick(Geometry, Now, DeltaTime);
        // Discovery belongs to the connection screen, never to a running match.
        if (!CanEditConnection() && LANDiscovery.IsRunning()) LANDiscovery.Stop();
        if (bLocalNetwork && Now >= NextDiscoveryRefresh)
        {
            NextDiscoveryRefresh = Now + 0.5;
            RefreshLocalServers();
        }
    }

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

    TSharedRef<SWidget> BuildTitle()
    {
        // Keep exit available while the form scrolls or the keyboard is open.
        return SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()
                [SNew(STextBlock).Text(this, &SCinderOnlinePanel::MatchFormatText).ColorAndOpacity(Mint).Font(PanelFont(12, true))]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 3, 0, 0)
                [SNew(STextBlock).Text(FText::FromString(TEXT("MULTIPLAYER"))).ColorAndOpacity(White).Font(PanelFont(20, true))]
            ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12, 0, 0, 0)
            [TouchTarget(SNew(SButton).ContentPadding(FMargin(16, 0)).OnClicked(this, &SCinderOnlinePanel::Back)
                [SNew(STextBlock).Text(FText::FromString(TEXT("BACK"))).Font(PanelFont(14, true)).Justification(ETextJustify::Center)])];
    }

    TSharedRef<SWidget> BuildContent()
    {
        return SNew(SVerticalBox)
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
                .Padding(8)
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
            ];
    }

    TSharedRef<SWidget> BuildConnectionForm()
    {
        return SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
            [
                SNew(SUniformGridPanel).SlotPadding(FMargin(3))
                + SUniformGridPanel::Slot(0, 0)[NetworkButton(TEXT("INTERNET"), false)]
                + SUniformGridPanel::Slot(1, 0)[NetworkButton(TEXT("LOCAL NETWORK"), true)]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
            [
                SNew(STextBlock).Text(this, &SCinderOnlinePanel::NetworkHelp)
                .Font(PanelFont(13)).ColorAndOpacity(Muted).AutoWrapText(true)
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
            [
                SNew(SVerticalBox).Visibility_Lambda([this]() { return bLocalNetwork ? EVisibility::Visible : EVisibility::Collapsed; })
                + SVerticalBox::Slot().AutoHeight()
                [
                    TouchTarget(SNew(SButton).IsEnabled(this, &SCinderOnlinePanel::CanEditConnection)
                        .OnClicked(this, &SCinderOnlinePanel::FindServers)
                        [SNew(STextBlock).Text(FText::FromString(TEXT("FIND LOCAL SERVERS"))).Font(PanelFont(13, true))])
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 6)
                [SAssignNew(LocalServerList, SVerticalBox)]
                + SVerticalBox::Slot().AutoHeight()
                [SNew(STextBlock).Text(this, &SCinderOnlinePanel::DiscoveryText).Font(PanelFont(12)).ColorAndOpacity(Muted).AutoWrapText(true)]
            ]
            + SVerticalBox::Slot().AutoHeight()
            [SNew(STextBlock).Text(FText::FromString(TEXT("SERVER ADDRESS"))).Font(PanelFont(12, true)).ColorAndOpacity(Muted)]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 5, 0, 12)
            [
                TouchTarget(
                    SAssignNew(EndpointField, SEditableTextBox)
                    .Text(FText::FromString(InitialEndpoint))
                    .HintText_Lambda([this]() { return FText::FromString(bLocalNetwork ? TEXT("ws://192.168.1.10:8787/play") : TEXT("wss://your-server.example/play")); })
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
            [SNew(STextBlock).Text(FText::FromString(TEXT("CREATE A MATCH"))).Font(PanelFont(12, true)).ColorAndOpacity(Muted)]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 5, 0, 6)
            [
                SNew(SUniformGridPanel).SlotPadding(FMargin(3))
                + SUniformGridPanel::Slot(0, 0)[FormatButton(TEXT("1V1 DUEL"), 2)]
                + SUniformGridPanel::Slot(1, 0)[FormatButton(TEXT("4-PLAYER FREE-FOR-ALL"), 4)]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
            [SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(SelectedPlayerCount == 4
                ? TEXT("Four commanders. Every opponent is an enemy. Last Anchor standing wins.")
                : TEXT("Two commanders. Destroy your opponent's Anchor to win.")); })
                .Font(PanelFont(12)).ColorAndOpacity(Muted).AutoWrapText(true)]
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

    TSharedRef<SWidget> NetworkButton(const TCHAR* Label, bool bLAN)
    {
        return TouchTarget(SNew(SButton)
            .IsEnabled(this, &SCinderOnlinePanel::CanEditConnection)
            .ButtonColorAndOpacity_Lambda([this, bLAN]() { return bLAN == bLocalNetwork ? FLinearColor(0.04f, 0.34f, 0.29f, 1) : FLinearColor::White; })
            .OnClicked_Lambda([this, bLAN]() { return SelectNetwork(bLAN); })
            [SNew(STextBlock).Text(FText::FromString(Label)).Font(PanelFont(14, true)).Justification(ETextJustify::Center)]);
    }

    FReply SelectNetwork(bool bLAN)
    {
        UCinderOnlineSubsystem* Service = Online.Get();
        if (!Service || !CanEditConnection()) return FReply::Handled();
        if (bLocalNetwork != bLAN)
        {
            (bLocalNetwork ? LANDraft : InternetDraft) = EndpointField->GetText().ToString();
            if (!Service->SetConnectionMode(bLAN ? ECinderConnectionMode::LocalNetwork : ECinderConnectionMode::Internet)) return FReply::Handled();
            bLocalNetwork = bLAN;
            const FString& Draft = bLAN ? LANDraft : InternetDraft;
            EndpointField->SetText(FText::FromString(Draft.IsEmpty() ? Service->Endpoint() : Draft));
        }
        LocalError.Reset();
        bLocalError = false;
        if (bLAN) FindServers();
        else LANDiscovery.Stop();
        return FReply::Handled();
    }

    FReply FindServers()
    {
        if (bLocalNetwork && CanEditConnection())
        {
            LANDiscovery.Stop();
            LANDiscovery.Start();
            NextDiscoveryRefresh = 0;
        }
        return FReply::Handled();
    }

    void RefreshLocalServers()
    {
        if (!LocalServerList) return;
        const TArray<FCinderLANService> Services = LANDiscovery.Services();
        FString Signature;
        for (const FCinderLANService& Server : Services)
            Signature += Server.StableId + TEXT("|") + Server.Name + TEXT("|") + Server.Endpoint() + (Server.bCompatible ? TEXT("+;") : TEXT("-;"));
        if (Signature == LocalServerSignature) return;
        LocalServerSignature = MoveTemp(Signature);
        LocalServerList->ClearChildren();
        for (int32 Index = 0; Index < FMath::Min(Services.Num(), 8); ++Index)
        {
            const FCinderLANService Server = Services[Index];
            const FString Label = Server.bCompatible
                ? FString::Printf(TEXT("%s  ·  %s"), *Server.Name.Left(48), *Server.Endpoint())
                : FString::Printf(TEXT("%s  ·  Different game version"), *Server.Name.Left(48));
            LocalServerList->AddSlot().AutoHeight().Padding(0, 2)
            [
                TouchTarget(SNew(SButton)
                    .IsEnabled_Lambda([this, Server]() { return CanEditConnection() && Server.bCompatible; })
                    .OnClicked_Lambda([this, Server]()
                    {
                        EndpointField->SetText(FText::FromString(Server.Endpoint()));
                        LocalError = FString::Printf(TEXT("%s selected. Create a room or enter your friend's room code."), *Server.Name.Left(48));
                        bLocalError = false;
                        return FReply::Handled();
                    })
                    [SNew(STextBlock).Text(FText::FromString(Label)).Font(PanelFont(13)).AutoWrapText(true)])
            ];
        }
    }

    FText NetworkHelp() const
    {
        return FText::FromString(bLocalNetwork
            ? TEXT("Same Wi-Fi · A Mac runs the match server. Pick it below or enter its local address, then share the room code.")
            : TEXT("Play from different networks. All players use the same secure server address and room code."));
    }

    FText DiscoveryText() const
    {
        if (!LANDiscovery.Error().IsEmpty()) return FText::FromString(LANDiscovery.Error() + TEXT(" You can still enter the Mac's address below."));
        if (LANDiscovery.IsRunning()) return FText::FromString(LANDiscovery.Status() + TEXT(" Allow Local Network access if asked. Guest Wi-Fi may block discovery."));
        return FText::FromString(TEXT("No server listed? Start the LAN server on your Mac, then find servers or enter its address."));
    }

    TSharedRef<SWidget> BuildLobbyActions()
    {
        return SNew(SBox).Visibility(this, &SCinderOnlinePanel::LeaveRoomVisibility)
            [SNew(SUniformGridPanel).SlotPadding(FMargin(3))
                + SUniformGridPanel::Slot(0, 0)
                [TouchTarget(SNew(SButton).IsEnabled(this, &SCinderOnlinePanel::CanSetReady)
                    .OnClicked(this, &SCinderOnlinePanel::ToggleReady)
                    [SNew(STextBlock).Text(this, &SCinderOnlinePanel::ReadyButtonText)
                        .Font(PanelFont(14, true)).Justification(ETextJustify::Center)])]
                + SUniformGridPanel::Slot(1, 0)
                [TouchTarget(SNew(SButton).OnClicked(this, &SCinderOnlinePanel::LeaveRoom)
                    [SNew(STextBlock).Text(FText::FromString(TEXT("LEAVE ROOM")))
                        .Font(PanelFont(14, true)).Justification(ETextJustify::Center)])]];
    }

    TSharedRef<SWidget> BuildLobby()
    {
        return SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0, 3, 0, 8)
            [SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
                [SNew(STextBlock).Text(this, &SCinderOnlinePanel::RoomText).ColorAndOpacity(Mint).Font(PanelFont(24, true))]
                + SHorizontalBox::Slot().AutoWidth()
                [TouchTarget(SNew(SButton).OnClicked(this, &SCinderOnlinePanel::CopyInvite)
                    [SNew(STextBlock).Text(FText::FromString(TEXT("COPY INVITE"))).Font(PanelFont(14, true))])]]
            + SVerticalBox::Slot().AutoHeight()
            [SNew(SVerticalBox).Visibility_Lambda([this]() { const auto* Service = Online.Get();
                return Service && Service->PlayerCount() == 2 ? EVisibility::Visible : EVisibility::Collapsed; })
                + SVerticalBox::Slot().AutoHeight().Padding(0, 2)[PlayerRow(0)]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 2)[PlayerRow(1)]]
            + SVerticalBox::Slot().AutoHeight()
            [SNew(SUniformGridPanel).SlotPadding(FMargin(2))
                .Visibility_Lambda([this]() { const auto* Service = Online.Get();
                    return Service && Service->PlayerCount() == 4 ? EVisibility::Visible : EVisibility::Collapsed; })
                + SUniformGridPanel::Slot(0, 0)[PlayerRow(0)]
                + SUniformGridPanel::Slot(1, 0)[PlayerRow(1)]
                + SUniformGridPanel::Slot(0, 1)[PlayerRow(2)]
                + SUniformGridPanel::Slot(1, 1)[PlayerRow(3)]]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 4)
            [SNew(STextBlock).Text(this, &SCinderOnlinePanel::LobbyRulesText)
                .Font(PanelFont(12)).ColorAndOpacity(Muted).AutoWrapText(true)];
    }

    int32 RelativeSeat(int32 Seat) const
    {
        const auto* Service = Online.Get();
        const int32 Count = Service ? Service->PlayerCount() : 2;
        return (Seat - FMath::Max(0, Service ? Service->LocalSeat() : 0) + Count) % Count;
    }

    FText MatchFormatText() const
    {
        const auto* Service = Online.Get();
        const int32 Count = Service && Service->HasRoom() ? Service->PlayerCount() : SelectedPlayerCount;
        return FText::FromString(Count == 4 ? TEXT("4-PLAYER FREE-FOR-ALL") : TEXT("PRIVATE 1V1"));
    }

    FText LobbyRulesText() const
    {
        const auto* Service = Online.Get();
        if (!Service) return FText::GetEmpty();
        int32 Present = 0, ReadyCount = 0;
        for (int32 Seat = 0; Seat < Service->PlayerCount(); ++Seat)
        {
            Present += Service->SeatConnected(Seat) ? 1 : 0;
            ReadyCount += Service->SeatConnected(Seat) && Service->SeatReady(Seat) ? 1 : 0;
        }
        const TCHAR* Maps[] = { TEXT("Shattered Rift"), TEXT("Glass Basin"), TEXT("Iron Reach") };
        return FText::FromString(FString::Printf(TEXT("%s · %d/%d connected · %d/%d ready\n%s"),
            Maps[FMath::Clamp(Service->MapIndex(), 0, 2)], Present, Service->PlayerCount(), ReadyCount, Service->PlayerCount(),
            Service->PlayerCount() == 4
                ? TEXT("Everyone fights for themselves. Lose every Anchor and you are eliminated. Last commander wins.")
                : TEXT("Both players must be ready. Protect your Anchor and destroy your opponent's.")));
    }

    TSharedRef<SWidget> FormatButton(const TCHAR* Label, int32 Count)
    {
        return TouchTarget(SNew(SButton)
            .IsEnabled(this, &SCinderOnlinePanel::CanEditConnection)
            .ButtonColorAndOpacity_Lambda([this, Count]() { return SelectedPlayerCount == Count
                ? FLinearColor(0.04f, 0.34f, 0.29f, 1) : FLinearColor::White; })
            .OnClicked_Lambda([this, Count]() { SelectedPlayerCount = Count; return FReply::Handled(); })
            [SNew(STextBlock).Text(FText::FromString(Label)).Font(PanelFont(13, true)).Justification(ETextJustify::Center)]);
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
            .Padding(FMargin(10, 6))
            [SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()
                [SNew(STextBlock).Text_Lambda([this, Seat]() { return PlayerNameText(Seat); })
                    .Font(PanelFont(13)).OverflowPolicy(ETextOverflowPolicy::Ellipsis)
                    .ColorAndOpacity_Lambda([this, Seat]() { return FSlateColor(CinderTeamColors::Accent(RelativeSeat(Seat))); })]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 3, 0, 0)
                [SNew(STextBlock).Text_Lambda([this, Seat]() { return PlayerStateText(Seat); })
                    .Font(PanelFont(11, true)).ColorAndOpacity_Lambda([this, Seat]() { return PlayerStateColor(Seat); })]];
    }

    bool CanEditConnection() const
    {
        const UCinderOnlineSubsystem* Service = Online.Get();
        return Service && !Service->HasRoom() && !Service->HasMatch()
            && (Service->State() == ECinderOnlineState::Offline || Service->State() == ECinderOnlineState::Error);
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
        return Service && Service->CanLeaveSession() && !Service->LatestSnapshot() ? EVisibility::Visible : EVisibility::Collapsed;
    }

    EVisibility ReconnectVisibility() const
    {
        const UCinderOnlineSubsystem* Service = Online.Get();
        if (!Service || !Service->CanReconnectSession()) return EVisibility::Collapsed;
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
            Service->CreateRoom(Endpoint, Name, SelectedMap, SelectedPlayerCount);
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
        LocalError.Reset();
        bLocalError = false;
        if (UCinderOnlineSubsystem* Service = Online.Get())
        {
            const int32 Seat = Service->LocalSeat();
            if (Seat >= 0) Service->SetReady(!Service->SeatReady(Seat));
        }
        return FReply::Handled();
    }

    FReply Reconnect()
    {
        LocalError.Reset();
        bLocalError = false;
        if (UCinderOnlineSubsystem* Service = Online.Get()) Service->Reconnect();
        return FReply::Handled();
    }

    FReply CopyInvite()
    {
        if (const UCinderOnlineSubsystem* Service = Online.Get())
        {
            const FString Invite = FString::Printf(TEXT("Join my Cinderline %s room %s at %s"), Service->PlayerCount() == 4 ? TEXT("4-player free-for-all") : TEXT("1v1"), *Service->RoomCode(), *Service->Endpoint());
            FPlatformApplicationMisc::ClipboardCopy(*Invite);
            LocalError = TEXT("Invite copied.");
            bLocalError = false;
        }
        return FReply::Handled();
    }

    FReply LeaveRoom()
    {
        if (UCinderOnlineSubsystem* Service = Online.Get(); Service && Service->CanLeaveSession() && !Service->LatestSnapshot()) Service->Leave();
        Close();
        return FReply::Handled();
    }

    FReply Back()
    {
        if (UCinderOnlineSubsystem* Service = Online.Get(); Service && Service->CanLeaveSession() && !Service->LatestSnapshot()) Service->Leave();
        Close();
        return FReply::Handled();
    }

    void Close()
    {
        LANDiscovery.Stop();
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
        const FString Prefix = FString(CinderTeamColors::Label(RelativeSeat(Seat))) + TEXT("  ");
        return FText::FromString(Prefix + (Name.IsEmpty() ? TEXT("Waiting for player") : Name));
    }

    FText PlayerStateText(int32 Seat) const
    {
        const UCinderOnlineSubsystem* Service = Online.Get();
        if (!Service || Service->SeatName(Seat).IsEmpty()) return FText::FromString(TEXT("OPEN"));
        if (const auto* Snapshot = Service->LatestSnapshot(); Snapshot && (Snapshot->eliminatedMask & (1u << RelativeSeat(Seat))))
            return FText::FromString(TEXT("ELIMINATED"));
        if (!Service->SeatConnected(Seat)) return FText::FromString(TEXT("DISCONNECTED"));
        if (Service->HasMatch()) return FText::FromString(TEXT("IN MATCH"));
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
        if (bLocalError && !LocalError.IsEmpty()) return FText::FromString(LocalError);
        const UCinderOnlineSubsystem* Service = Online.Get();
        if (!Service) return FText::FromString(TEXT("Online service unavailable."));
        if (!Service->ErrorText().IsEmpty()) return FText::FromString(Service->ErrorText() + (bLocalNetwork
            ? TEXT(" Check the Mac server and Wi-Fi. On iPhone, allow Cinderline in Settings > Privacy & Security > Local Network, then retry.") : TEXT("")));
        switch (Service->State())
        {
        case ECinderOnlineState::Connecting: return FText::FromString(TEXT("Connecting..."));
        case ECinderOnlineState::Starting: return FText::FromString(TEXT("All players ready. Starting match..."));
        case ECinderOnlineState::Reconnecting: return FText::FromString(TEXT("Connection lost. Reconnecting..."));
        default: return FText::FromString(LocalError.IsEmpty() ? Service->StatusText() : LocalError);
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
    TSharedPtr<SVerticalBox> LocalServerList;
    FCinderLANDiscovery LANDiscovery;
    FString LocalServerSignature;
    FString InternetDraft;
    FString LANDraft;
    double NextDiscoveryRefresh = 0;
    bool bLocalNetwork = false;
    FString InitialEndpoint;
    FString InitialName;
    FString InitialCode;
    FString LocalError;
    bool bLocalError = false;
    int32 SelectedMap = 0;
    int32 SelectedPlayerCount = 2;
};
}

TSharedRef<SWidget> MakeCinderOnlinePanel(UCinderOnlineSubsystem* Online, TFunction<void()> OnClose)
{
    return SNew(SCinderOnlinePanel)
        .Online(TWeakObjectPtr<UCinderOnlineSubsystem>(Online))
        .OnClose(MoveTemp(OnClose));
}
