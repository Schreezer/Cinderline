#include "Presentation/CinderHUD.h"

#include "Presentation/CinderBattlefield.h"
#include "Presentation/CinderCampaign.h"
#include "Presentation/CinderCampaignSave.h"
#include "Presentation/CinderPlayerController.h"

namespace
{
const FLinearColor CampaignInk(0.004f, 0.010f, 0.018f, 0.97f);
const FLinearColor CampaignPanel(0.012f, 0.027f, 0.037f, 0.98f);
const FLinearColor CampaignWhite(0.88f, 0.94f, 0.97f);
const FLinearColor CampaignMuted(0.38f, 0.50f, 0.55f);
const FLinearColor CampaignMint(0.10f, 0.90f, 0.74f);
const FLinearColor CampaignAmber(1.0f, 0.64f, 0.24f);

FString CampaignStatus(const FCinderCampaignProgress& Progress, int32 Mission)
{
    const uint32 Bit = uint32{1} << Mission;
    if (!Progress.IsComplete(Mission)) return TEXT("PRACTICE");
    if ((Progress.UnassistedMask & Bit) != 0) return TEXT("COMPLETE");
    if ((Progress.AssistedMask & Bit) != 0) return TEXT("ASSISTED");
    return TEXT("COMPLETE");
}

FString CampaignFailureText(const FString& Code)
{
    if (Code.Contains(TEXT("anchor"), ESearchCase::IgnoreCase))
        return TEXT("Your Anchor was lost. Restore the last objective boundary or restart the mission.");
    if (Code.Contains(TEXT("economy"), ESearchCase::IgnoreCase))
        return TEXT("The remaining economy cannot replace the required force. Retry the last objective boundary.");
    if (Code.Contains(TEXT("draw"), ESearchCase::IgnoreCase))
        return TEXT("Neither Anchor survived. Retry from the last settled objective.");
    return Code.IsEmpty() ? TEXT("The salvage column was defeated before the objective was secured.") : Code;
}

int32 CampaignMarkCount(uint32 Mask)
{
    int32 Count = 0;
    while (Mask != 0) { Count += static_cast<int32>(Mask & 1u); Mask >>= 1; }
    return Count;
}
}

void ACinderHUD::DrawCampaignMenu(ACinderPlayerController* PC, ACinderBattlefield* Battle)
{
    if (!PC || !Battle) return;
    const float S = UIScale;
    const float SafeLeft = bCompactLayout ? MobileLayout.Left : Margin;
    const float SafeRight = bCompactLayout ? MobileLayout.Right : Width - Margin;
    const float SafeTop = bCompactLayout ? MobileLayout.Top : Margin;
    const float SafeBottom = bCompactLayout ? MobileLayout.Bottom : Height - Margin;
    const float W = SafeRight - SafeLeft;
    const int32 Selected = FMath::Clamp(PC->SelectedCampaignMission(), 0, FCinderCampaign::MissionCount - 1);
    const FCinderCampaignProgress& Progress = Battle->CampaignProgress();
    const FString& StorageMessage = Battle->CampaignSaveMessage();

    const float HeaderH = 48 * S;
    Label(TEXT("EMBERLINE CAMPAIGN"), SafeLeft, SafeTop + 2 * S, CampaignWhite,
        bCompactLayout ? 1.18f : 1.55f);
    SingleLineLabel(TEXT("Restore the route. Learn each command by using it."),
        SafeLeft, SafeTop + 29 * S, W - 118 * S, CampaignMuted, 0.64f);
    Button(TEXT("BACK"), TEXT("campaignclose"), 0, SafeRight - 104 * S, SafeTop, 104 * S);

    const float Gap = 7 * S;
    const float TilesY = SafeTop + HeaderH;
    const float TileH = 44 * S;
    const float TileW = (W - Gap * 2) / 3;
    for (int32 Mission = 0; Mission < FCinderCampaign::MissionCount; ++Mission)
    {
        const int32 Row = Mission / 3;
        const int32 Column = Mission % 3;
        const float X = SafeLeft + Column * (TileW + Gap);
        const float Y = TilesY + Row * (TileH + Gap);
        const FCinderCampaignMissionDefinition& Definition = FCinderCampaign::Definition(Mission);
        const FString Prefix = Progress.IsComplete(Mission) ? TEXT("✓") : FString::Printf(TEXT("%d"), Mission + 1);
        Button(Prefix + TEXT("  ") + Definition.Name, TEXT("campaignselect"), Mission,
            X, Y, TileW, Mission == Selected);
    }

    const FCinderCampaignMissionDefinition& Definition = FCinderCampaign::Definition(Selected);
    const float DetailsY = TilesY + 2 * (TileH + Gap) + 5 * S;
    const float FooterH = (StorageMessage.IsEmpty() ? 52 : 80) * S;
    const float DetailsH = FMath::Max(82 * S, SafeBottom - DetailsY - FooterH);
    Surface(SafeLeft, DetailsY, W, DetailsH, CampaignPanel, 8 * S);
    Surface(SafeLeft, DetailsY + 8 * S, 3 * S, DetailsH - 16 * S,
        Progress.IsComplete(Selected) ? CampaignMint : CampaignAmber, S);
    Label(FString::Printf(TEXT("MISSION %d / 6   %s"), Selected + 1, *CampaignStatus(Progress, Selected)),
        SafeLeft + 14 * S, DetailsY + 9 * S,
        Progress.IsComplete(Selected) ? CampaignMint : CampaignAmber, 0.58f);
    Label(Definition.Name, SafeLeft + 14 * S, DetailsY + 28 * S, CampaignWhite, 1.00f);

    const float CopyX = SafeLeft + FMath::Min(205 * S, W * 0.38f);
    const FString Outcomes[] = {Definition.Outcome1, Definition.Outcome2, Definition.Outcome3};
    const float OutcomeY = DetailsY + 51 * S;
    const float OutcomeBottomPad = Selected > 0 ? 22 * S : 6 * S;
    const int32 OutcomeCount = FMath::Clamp(
        FMath::FloorToInt((DetailsY + DetailsH - OutcomeBottomPad - OutcomeY) / (15 * S)), 0, 3);
    for (int32 Index = 0; Index < OutcomeCount; ++Index)
        if (!Outcomes[Index].IsEmpty())
            SingleLineLabel(TEXT("•  ") + Outcomes[Index], SafeLeft + 14 * S,
                OutcomeY + Index * 15 * S, CopyX - SafeLeft - 24 * S, CampaignMuted, 0.55f);

    const float CopyW = SafeRight - CopyX - 12 * S;
    SingleLineLabel(Definition.Skills, CopyX, DetailsY + 11 * S, CopyW, CampaignMint, 0.65f);
    WrappedLabel(Definition.Story, CopyX, DetailsY + 32 * S, CopyW, CampaignWhite, 0.66f);
    if (Selected > 0)
    {
        const FString Assumed = FString::Printf(TEXT("ASSUMES  %s"),
            *FCinderCampaign::Definition(Selected - 1).Skills);
        SingleLineLabel(Assumed, SafeLeft + 14 * S, DetailsY + DetailsH - 18 * S,
            CopyX - SafeLeft - 24 * S, CampaignMuted, 0.55f);
    }

    const float ButtonY = SafeBottom - 44 * S;
    if (!StorageMessage.IsEmpty())
        WrappedLabel(StorageMessage, SafeLeft + 4 * S, ButtonY - 28 * S, W - 8 * S,
            CampaignAmber, 0.55f);
    const float ButtonGap = 7 * S;
    const bool bCheckpoint = Battle->HasCampaignCheckpoint();
    const int32 ButtonCount = bCheckpoint ? 3 : 2;
    const float ButtonW = (W - ButtonGap * (ButtonCount - 1)) / ButtonCount;
    const bool bRecommended = !Progress.IsComplete(Selected) && Selected == Progress.RecommendedMission();
    Button(Progress.IsComplete(Selected) ? TEXT("REPLAY MISSION")
            : bRecommended ? TEXT("CONTINUE RECOMMENDED") : TEXT("START MISSION"),
        TEXT("campaignstart"), Selected, SafeLeft, ButtonY, ButtonW, true);
    if (bCheckpoint)
        Button(TEXT("RESUME CHECKPOINT"), TEXT("campaignresume"), 0,
            SafeLeft + ButtonW + ButtonGap, ButtonY, ButtonW);
    Button(TEXT("QUICK TRAINING"), TEXT("tutorial"), 0,
        SafeLeft + (ButtonCount - 1) * (ButtonW + ButtonGap), ButtonY, ButtonW);

    UIRegions.Add(FBox2D(FVector2D(SafeLeft, SafeTop), FVector2D(SafeRight, SafeBottom)));
}

void ACinderHUD::DrawCampaignCard(ACinderBattlefield* Battle, bool bForceShow)
{
    auto* PC = Cast<ACinderPlayerController>(PlayerOwner);
    if (!PC || !Battle || !Battle->Campaign().IsRunning()) return;
    const FCinderCampaignText Text = Battle->Campaign().Text(Battle->Sim());
    const int32 CardKey = (Text.Mission + 1) * 64 + Text.Phase;
    if (TutorialCardStep != CardKey)
    {
        TutorialCardStep = CardKey;
        bTutorialTrainKindChosen = bTutorialTrainQuantityChosen = false;
    }
    const FCinderTutorialGuide Guide = CurrentTutorialGuide();
    const FString Instruction = Guide.Instruction.IsEmpty() ? Text.Body : Guide.Instruction;
    if (Instruction.IsEmpty()) return;

    const float S = UIScale;
    const bool bWorld = Guide.Target == ECinderTutorialGuideTarget::Entity
        || Guide.Target == ECinderTutorialGuideTarget::Ground;
    TutorialCardBounds.Init(); TutorialTargetBounds.Init();
    bTutorialTargetVisible = bTutorialNeedsShow = bTutorialTargetClear = false;
    TutorialPointer = FVector2D::ZeroVector;
    if (Guide.Target == ECinderTutorialGuideTarget::Button)
    {
        for (const FButton& Entry : Buttons)
        {
            if (Entry.Action != Guide.ButtonAction || Entry.Argument != Guide.ButtonArgument
                || Entry.EntityId != Guide.ButtonEntity) continue;
            TutorialTargetBounds = Entry.Bounds;
            TutorialPointer = Entry.Bounds.GetCenter();
            bTutorialTargetVisible = bTutorialTargetClear = true;
            break;
        }
    }
    else if (bWorld)
    {
        const bool bProjected = PC->ProjectTutorialTarget(Guide.Entity, Guide.Point, TutorialPointer);
        const FBox2D View(FVector2D(Margin + 26 * S, 84 * S + SafeTopOffset),
            FVector2D(Width - Margin - 26 * S, Height - 74 * S - SafeBottomOffset));
        bTutorialTargetVisible = bProjected && View.IsInside(TutorialPointer);
        bTutorialTargetClear = bTutorialTargetVisible && !ContainsUI(TutorialPointer);
        bTutorialNeedsShow = bForceShow || !bTutorialTargetClear;
        TutorialTargetBounds = FBox2D(TutorialPointer - FVector2D(24 * S),
            TutorialPointer + FVector2D(24 * S));
    }

    const bool bHintRevealed = Battle->Campaign().IsCurrentPhaseAssisted();
    const bool bShow = bTutorialNeedsShow;
    const bool bHint = !bShow && !bHintRevealed
        && Guide.Target == ECinderTutorialGuideTarget::None;
    const bool bAction = bShow || bHint;
    const FString VisibleInstruction = bShow
        ? (bCompactLayout ? TEXT("Tap SHOW to find the objective.") : TEXT("Click SHOW to find the objective."))
        : Instruction;
    const FString Explanation = (Guide.Target != ECinderTutorialGuideTarget::None || bHintRevealed)
        ? Guide.Explanation : FString();
    const float Pad = 10 * S;
    const float Top = 65 * S + SafeTopOffset;
    const bool bSheet = CompactSheet != 0 || PC->bBuildMenu;
    const float Right = bCompactLayout ? MobileLayout.Right : Width - Margin;
    const float Left = MobileLayout.Drawer.Min.X;
    TArray<FBox2D> Candidates;
    const auto AddCandidate = [&](float X, float CandidateW, float Y)
    {
        if (CandidateW < 192 * S || X < Left || X + CandidateW > Right + 1) return;
        const float TextW = CandidateW - 2 * Pad;
        const float CardH = 28 * S + WrappedHeight(VisibleInstruction, TextW, 0.78f)
            + (Text.Progress.IsEmpty() ? 0 : 18 * S)
            + (Explanation.IsEmpty() ? 0 : 3 * S + WrappedHeight(Explanation, TextW, 0.63f))
            + 9 * S + (bAction ? 46 * S : 0);
        if (Y + CardH > Height - 66 * S - SafeBottomOffset) return;
        Candidates.Add(FBox2D(FVector2D(X, Y), FVector2D(X + CandidateW, Y + CardH)));
    };
    if (bSheet)
        AddCandidate(MobileLayout.Drawer.Max.X + 8 * S,
            Right - MobileLayout.Drawer.Max.X - 8 * S,
            FMath::Max(Top, static_cast<float>(MobileLayout.Navigation.Max.Y) + 8 * S));
    const float NormalW = FMath::Min(300 * S, Right - Left);
    AddCandidate(Left, NormalW, Top);
    AddCandidate(Right - NormalW, NormalW, Top);
    AddCandidate((Left + Right - NormalW) * 0.5f, NormalW, Top);
    AddCandidate(Left, NormalW, Top + 112 * S);
    AddCandidate(Right - 220 * S, 220 * S, Top + 112 * S);
    const auto OverlapArea = [](const FBox2D& A, const FBox2D& B)
    {
        return FMath::Max(0.0, FMath::Min(A.Max.X, B.Max.X) - FMath::Max(A.Min.X, B.Min.X))
            * FMath::Max(0.0, FMath::Min(A.Max.Y, B.Max.Y) - FMath::Max(A.Min.Y, B.Min.Y));
    };
    double BestScore = TNumericLimits<double>::Max();
    bool bFoundTargetClear = false;
    bool bFoundControlClear = false;
    for (const FBox2D& Candidate : Candidates)
    {
        double RegionOverlap = 0;
        double ButtonOverlap = 0;
        for (const FBox2D& Region : UIRegions) RegionOverlap += OverlapArea(Candidate, Region);
        for (const FButton& Entry : Buttons) ButtonOverlap += OverlapArea(Candidate, Entry.Bounds);
        const double TargetOverlap = bTutorialTargetVisible
            ? OverlapArea(Candidate, TutorialTargetBounds.ExpandBy(12 * S)) : 0;
        const bool bTargetClear = TargetOverlap <= 0;
        const bool bControlClear = RegionOverlap <= 0 && ButtonOverlap <= 0;
        const bool bBetterClass = (bTargetClear && !bFoundTargetClear)
            || (bTargetClear == bFoundTargetClear && bControlClear && !bFoundControlClear);
        const double Score = RegionOverlap + ButtonOverlap + 20 * TargetOverlap;
        if (bBetterClass || (bTargetClear == bFoundTargetClear
            && bControlClear == bFoundControlClear && Score < BestScore))
        {
            BestScore = Score;
            TutorialCardBounds = Candidate;
            bFoundTargetClear = bTargetClear;
            bFoundControlClear = bControlClear;
        }
    }
    if (!TutorialCardBounds.bIsValid) return;
    if (bWorld && !bAction && TutorialCardBounds.Intersect(TutorialTargetBounds))
    {
        DrawCampaignCard(Battle, true);
        return;
    }

    // Candidate space can become crowded when a command drawer is open. Never
    // leave an interactive control hidden below the opaque objective card:
    // HandleTap dispatches buttons before consulting UIRegions.
    Buttons.RemoveAll([this](const FButton& Entry)
    {
        return TutorialCardBounds.Intersect(Entry.Bounds);
    });

    const float X = TutorialCardBounds.Min.X;
    const float Y = TutorialCardBounds.Min.Y;
    const float CardW = TutorialCardBounds.GetSize().X;
    const float CardH = TutorialCardBounds.GetSize().Y;
    Surface(X, Y, CardW, CardH, CampaignInk, 8 * S);
    Surface(X, Y + 8 * S, 2 * S, CardH - 16 * S,
        Guide.bWaiting ? CampaignMint : CampaignAmber, S);
    Label(FString::Printf(TEXT("MISSION %d/6   PHASE %d/%d"), Text.Mission + 1,
        Text.Phase + 1, FMath::Max(1, Text.PhaseCount)), X + Pad, Y + 6 * S,
        Guide.bWaiting ? CampaignMint : CampaignAmber, 0.56f);
    float TextY = Y + 26 * S;
    TextY += WrappedLabel(VisibleInstruction, X + Pad, TextY, CardW - 2 * Pad,
        CampaignWhite, 0.78f);
    if (!Text.Progress.IsEmpty())
    {
        SingleLineLabel(Text.Progress, X + Pad, TextY + 2 * S, CardW - 2 * Pad,
            CampaignMint, 0.60f);
        TextY += 18 * S;
    }
    if (!Explanation.IsEmpty())
        WrappedLabel(Explanation, X + Pad, TextY + 3 * S, CardW - 2 * Pad,
            CampaignMuted, 0.63f);
    UIRegions.Add(TutorialCardBounds);
    if (bAction)
    {
        Button(bShow ? TEXT("SHOW") : TEXT("HINT"),
            bShow ? TEXT("campaignshow") : TEXT("campaignhint"), 0,
            X + Pad, Y + CardH - 44 * S, bShow ? 76 * S : CardW - 2 * Pad, true);
        if (bShow)
        {
            TutorialTargetBounds = Buttons.Last().Bounds;
            TutorialPointer = TutorialTargetBounds.GetCenter();
            bTutorialTargetVisible = bTutorialTargetClear = true;
            DrawTutorialPointer(TutorialPointer, TutorialTargetBounds, false);
        }
    }
    else if (bTutorialTargetVisible && bTutorialTargetClear)
    {
        if (!bWorld || !TutorialCardBounds.IsInside(TutorialPointer))
            DrawTutorialPointer(TutorialPointer, TutorialTargetBounds, bWorld);
    }
}

void ACinderHUD::DrawCampaignOverlay(ACinderPlayerController* PC, ACinderBattlefield* Battle)
{
    if (!PC || !Battle || !Battle->Campaign().IsActive()) return;
    const FCinderCampaign& Campaign = Battle->Campaign();
    const bool bTerminal = Campaign.IsTerminal();
    if (!Battle->IsPaused() && !bTerminal) return;

    const float S = UIScale;
    Panel(0, 0, Width, Height, FLinearColor(0.002f, 0.008f, 0.014f, 0.92f));
    Buttons.Reset(); UIRegions.Reset(); Minimap.Init();
    UIRegions.Add(FBox2D(FVector2D::ZeroVector, FVector2D(Width, Height)));
    const float SafeLeft = bCompactLayout ? MobileLayout.Left : Margin;
    const float SafeRight = bCompactLayout ? MobileLayout.Right : Width - Margin;
    const float SafeTop = bCompactLayout ? MobileLayout.Top : Margin;
    const float SafeBottom = bCompactLayout ? MobileLayout.Bottom : Height - Margin;
    const float W = FMath::Min(560 * S, SafeRight - SafeLeft);
    const float X = FMath::Clamp((Width - W) * 0.5f, SafeLeft, SafeRight - W);
    const float Y = SafeTop + 8 * S;
    const FCinderCampaignMissionDefinition& Definition = FCinderCampaign::Definition(Campaign.MissionIndex());
    const FString& StorageMessage = Battle->CampaignSaveMessage();
    const bool bVictory = Campaign.Outcome() == ECinderCampaignOutcome::Victory;
    const FString Heading = bTerminal
        ? bVictory ? TEXT("MISSION COMPLETE")
            : Campaign.Outcome() == ECinderCampaignOutcome::Draw ? TEXT("MISSION DRAWN") : TEXT("MISSION FAILED")
        : TEXT("CAMPAIGN PAUSED");
    Label(Heading, X, Y, bVictory ? CampaignMint : bTerminal ? CampaignAmber : CampaignWhite, 1.42f);
    Label(FString::Printf(TEXT("%d / 6   %s"), Campaign.MissionIndex() + 1, *Definition.Name),
        X, Y + 35 * S, CampaignMint, 0.70f);

    if (bTerminal)
    {
        const auto& Stats = Battle->Sim().players()[0].stats;
        const FString Result = bVictory
            ? Campaign.WasAssisted() ? TEXT("Completed with guidance. Replay anytime to earn an unassisted result.")
                : TEXT("Completed without campaign hints.")
            : CampaignFailureText(Campaign.FailureCode());
        WrappedLabel(Result, X, Y + 58 * S, W, bVictory ? CampaignWhite : CampaignAmber, 0.72f);
        SingleLineLabel(FString::Printf(TEXT("%s   Ore %d   Produced %d   Lost %d   Destroyed %d   Marks %d"),
            *FString::Printf(TEXT("%02d:%02d"), static_cast<int32>(Battle->Sim().time()) / 60,
                static_cast<int32>(Battle->Sim().time()) % 60),
            Stats.gathered, Stats.produced, Stats.lost, Stats.killed,
            CampaignMarkCount(Campaign.OptionalOutcomes())),
            X, Y + 105 * S, W, CampaignMuted, 0.62f);
        if (!StorageMessage.IsEmpty())
            WrappedLabel(StorageMessage, X, Y + 128 * S, W, CampaignAmber, 0.55f);
        const float Gap = 7 * S;
        const float ButtonY = FMath::Min(Y + (StorageMessage.IsEmpty() ? 145 : 164) * S,
            SafeBottom - 52 * S);
        if (bVictory)
        {
            const bool bHasNext = Campaign.MissionIndex() + 1 < FCinderCampaign::MissionCount;
            const int32 Count = 3;
            const float ButtonW = (W - Gap * (Count - 1)) / Count;
            if (bHasNext)
                Button(TEXT("NEXT MISSION"), TEXT("campaignstart"), Campaign.MissionIndex() + 1,
                    X, ButtonY, ButtonW, true);
            else
                Button(TEXT("PLAY SKIRMISH"), TEXT("start"), Battle->MapIndex(),
                    X, ButtonY, ButtonW, true);
            Button(TEXT("REPLAY"), TEXT("campaignstart"), Campaign.MissionIndex(),
                X + ButtonW + Gap, ButtonY, ButtonW);
            Button(TEXT("MISSION SELECT"), TEXT("campaignexit"), 0,
                X + (Count - 1) * (ButtonW + Gap), ButtonY, ButtonW);
        }
        else
        {
            const int32 Count = Battle->HasCampaignCheckpoint() ? 3 : 2;
            const float ButtonW = (W - Gap * (Count - 1)) / Count;
            int32 Column = 0;
            if (Battle->HasCampaignCheckpoint())
            {
                Button(TEXT("RETRY"), TEXT("campaignretry"), 0, X, ButtonY, ButtonW, true);
                ++Column;
            }
            Button(TEXT("RESTART"), TEXT("campaignrestart"), 0,
                X + Column++ * (ButtonW + Gap), ButtonY, ButtonW, !Battle->HasCampaignCheckpoint());
            Button(TEXT("MISSION SELECT"), TEXT("campaignexit"), 0,
                X + Column * (ButtonW + Gap), ButtonY, ButtonW);
        }
    }
    else
    {
        const FCinderCampaignText Text = Campaign.Text(Battle->Sim());
        WrappedLabel(Text.Body, X, Y + 61 * S, W, CampaignWhite, 0.72f);
        SingleLineLabel(Text.Progress, X, Y + 103 * S, W, CampaignMint, 0.62f);
        if (!StorageMessage.IsEmpty())
            WrappedLabel(StorageMessage, X, Y + 120 * S, W, CampaignAmber, 0.55f);
        const float Gap = 7 * S;
        const int32 FirstRowCount = Battle->HasCampaignCheckpoint() ? 3 : 2;
        const float ButtonW = (W - Gap * (FirstRowCount - 1)) / FirstRowCount;
        const float Row1 = Y + (StorageMessage.IsEmpty() ? 137 : 158) * S;
        Button(TEXT("RESUME"), TEXT("resume"), 0, X, Row1, ButtonW, true);
        int32 Column = 1;
        if (Battle->HasCampaignCheckpoint())
        {
            Button(TEXT("RETRY CHECKPOINT"), TEXT("campaignretry"), 0,
                X + Column * (ButtonW + Gap), Row1, ButtonW);
            ++Column;
        }
        Button(TEXT("RESTART"), TEXT("campaignrestart"), 0,
            X + Column * (ButtonW + Gap), Row1, ButtonW);
        const float Row2 = Row1 + 51 * S;
        Button(TEXT("FIELD GUIDE"), TEXT("help"), 0, X, Row2, (W - Gap) * 0.5f);
        Button(TEXT("MISSION SELECT"), TEXT("campaignexit"), 0,
            X + (W + Gap) * 0.5f, Row2, (W - Gap) * 0.5f);
    }
}

#if UE_BUILD_DEVELOPMENT
void ACinderHUD::LogCampaignHUD() const
{
    const auto* PC = Cast<ACinderPlayerController>(PlayerOwner);
    const auto* Battle = PC ? PC->Battlefield() : nullptr;
    if (!PC || !Battle) return;
    const FCinderCampaign& Campaign = Battle->Campaign();
    UE_LOG(LogTemp, Display,
        TEXT("CINDERLINE_CAMPAIGN_HUD menu=%d selected=%d active=%d running=%d terminal=%d mission=%d phase=%d buttons=%d card=(%.1f,%.1f)-(%.1f,%.1f) target=(%.1f,%.1f)-(%.1f,%.1f) visible=%d show=%d clear=%d"),
        PC->IsCampaignMenuOpen(), PC->SelectedCampaignMission(), Campaign.IsActive(), Campaign.IsRunning(),
        Campaign.IsTerminal(), Campaign.MissionIndex(), Campaign.Phase(), Buttons.Num(),
        TutorialCardBounds.Min.X, TutorialCardBounds.Min.Y, TutorialCardBounds.Max.X, TutorialCardBounds.Max.Y,
        TutorialTargetBounds.Min.X, TutorialTargetBounds.Min.Y, TutorialTargetBounds.Max.X, TutorialTargetBounds.Max.Y,
        bTutorialTargetVisible, bTutorialNeedsShow, bTutorialTargetClear);
}
#endif
