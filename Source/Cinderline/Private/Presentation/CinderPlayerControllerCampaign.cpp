#include "Presentation/CinderPlayerController.h"

#include "Presentation/CinderBattlefield.h"
#include "Presentation/CinderCamera.h"
#include "Presentation/CinderHUD.h"
#include "Presentation/CinderOnlineSubsystem.h"

void ACinderPlayerController::OpenCampaignMenu()
{
    if (!Battle || !Battle->IsMenu() || Battle->IsOnlineMatch()) return;
    if (const auto* Session = Online(); Session && Session->HasRoom())
    { Notify(TEXT("Leave your multiplayer room before opening the campaign.")); return; }
    bHelpOpen = bTutorialRestartPending = false;
    ResetInteraction(true);
    bCampaignMenuOpen = true;
    CampaignMenuMission = Battle->CampaignProgress().RecommendedMission();
}

void ACinderPlayerController::FocusCampaignStart()
{
    if (!Rig || !Battle || !Battle->Campaign().IsActive()) return;
    const auto Point = Battle->Campaign().TargetPoint(ECinderCampaignTargetRole::PlayerAnchor);
    Rig->Zoom(ACinderCamera::DefaultDistance * 0.9f - Rig->Distance());
    Rig->Focus(FVector(Point.x, Point.y, 0), true);
}

bool ACinderPlayerController::ExecuteCampaignAction(const FString& Action, int32 Argument)
{
    if (!Action.StartsWith(TEXT("campaign"))) return false;
    if (!Battle) return true;
    if (Battle->IsOnlineMatch() || (Online() && Online()->HasRoom()))
    { Notify(TEXT("Leave your multiplayer room before starting a campaign mission.")); return true; }
    if (bHelpOpen) return true;
    if (Action == TEXT("campaignmenu")) { OpenCampaignMenu(); return true; }
    if (Action == TEXT("campaignclose"))
    { if (Battle->IsMenu()) bCampaignMenuOpen = false; return true; }
    if (Action == TEXT("campaignselect"))
    {
        if (Battle->IsMenu() && bCampaignMenuOpen && Argument >= 0 && Argument < FCinderCampaign::MissionCount)
            CampaignMenuMission = Argument;
        return true;
    }
    if (Action == TEXT("campaignhint"))
    {
        if (IsGameplayActive() && Battle->Campaign().IsActive()) Battle->RequestCampaignHint();
        return true;
    }
    if (Action == TEXT("campaignexit"))
    {
        if (!Battle->Campaign().IsActive()) return true;
        CampaignMenuMission = Battle->Campaign().MissionIndex();
        Battle->ReturnToMenu();
        ResetInteraction(true);
        bCampaignMenuOpen = true;
        return true;
    }
    if (Action == TEXT("campaignresume") || Action == TEXT("campaignretry"))
    {
        if (!Battle->IsMenu() && (!Battle->Campaign().IsActive() || (!Battle->IsPaused() && !Battle->IsMatchOver()))) return true;
        if (!Battle->ResumeCampaignCheckpoint())
        { Notify(Battle->CampaignSaveMessage()); return true; }
        bHelpOpen = bCampaignMenuOpen = false;
        ResetInteraction(true);
        CampaignMenuMission = Battle->Campaign().MissionIndex();
        FocusCampaignStart();
        Notify(Battle->CampaignSaveMessage().IsEmpty()
            ? TEXT("Resumed from the last completed objective boundary.") : Battle->CampaignSaveMessage());
        return true;
    }
    if (Action == TEXT("campaignstart") || Action == TEXT("campaignrestart"))
    {
        int32 Mission = Argument;
        if (Action == TEXT("campaignrestart"))
        {
            if (!Battle->Campaign().IsActive() || (!Battle->IsPaused() && !Battle->IsMatchOver())) return true;
            Mission = Battle->Campaign().MissionIndex();
        }
        else if (!Battle->IsMenu() && !Battle->Campaign().IsTerminal()) return true;
        if (Mission < 0 || Mission >= FCinderCampaign::MissionCount) return true;
        if (!Battle->StartCampaignMission(Mission))
        { Notify(Battle->CampaignSaveMessage()); return true; }
        bHelpOpen = bTutorialRestartPending = bCampaignMenuOpen = false;
        CampaignMenuMission = Mission;
        ResetInteraction(true);
        FocusCampaignStart();
        Notify(FString::Printf(TEXT("%s — follow the current objective. Hints are always optional."),
            *FCinderCampaign::Definition(Mission).Name));
        return true;
    }
    return true;
}
