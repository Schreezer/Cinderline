#include "Presentation/CinderHelpContent.h"
#include "Presentation/CinderUnitInfo.h"

#include "Sim/Simulation.h"

namespace
{
FString Name(cinder::Kind Kind)
{
    return UTF8_TO_TCHAR(cinder::definition(Kind).name);
}

void Add(FCinderHelpPage& Page, const TCHAR* Heading, const FString& Body)
{
    Page.Sections.Add({Heading, Body});
}

FString UnlockText(const cinder::Definition& Definition)
{
    using cinder::Kind;
    if (Definition.kind == Kind::Resource)
        return TEXT("Neutral map feature. It cannot be built, trained, or claimed.");
    if (!Definition.building)
        return FString::Printf(TEXT("Tier %d. %d ore and %d crew. Trained at %s in %.0f seconds."),
            Definition.tier, Definition.cost, Definition.supply, *Name(Definition.producer), Definition.buildTime);

    FString Requirement;
    switch (Definition.kind)
    {
    case Kind::Headquarters: Requirement = TEXT("Requires an operational Anchor."); break;
    case Kind::Processor: Requirement = TEXT("Requires an operational Anchor."); break;
    case Kind::Foundry: Requirement = TEXT("Requires an operational Anchor."); break;
    case Kind::MotorPool: Requirement = TEXT("Requires tier 2 and an operational Kiln."); break;
    case Kind::Laboratory: Requirement = TEXT("Requires an operational Kiln."); break;
    case Kind::Turret: Requirement = TEXT("Requires an operational Kiln."); break;
    default: break;
    }
    return FString::Printf(TEXT("Tier %d. %d ore, %.0f seconds. One Drudge builds it. %s"),
        Definition.tier, Definition.cost, Definition.buildTime, *Requirement);
}

FCinderHelpPage ReferencePage(int32 ReferenceIndex)
{
    const int32 Index = FMath::Clamp(ReferenceIndex, 0, CinderHelp::ReferenceCount - 1);
    const cinder::Definition& Definition = cinder::definitions()[Index];
    FCinderHelpPage Result;
    Result.Title = UTF8_TO_TCHAR(Definition.name);
    Result.Subtitle = FString::Printf(TEXT("Reference %d of %d"), Index + 1, CinderHelp::ReferenceCount);
    Add(Result, TEXT("Role"), FString::Printf(TEXT("%s. %s"), UTF8_TO_TCHAR(Definition.role), *CinderUnitInfo::Purpose(Definition.kind)));
    if (Definition.kind == cinder::Kind::Resource)
    {
        Add(Result, TEXT("Stats"), FString::Printf(TEXT("Neutral ore deposit. Footprint radius %.0f. Deposits have no sight, armor, or crew cost."), Definition.radius));
    }
    else if (Definition.building)
    {
        Add(Result, TEXT("Stats"), FString::Printf(TEXT("HP %.0f, armor %d, vision %.0f, footprint radius %.0f."),
            Definition.hp, Definition.armor, Definition.vision, Definition.radius));
    }
    else
    {
        FString Weapon = Definition.damage > 0
            ? FString::Printf(TEXT("damage %.0f every %.2f s, range %.0f"), Definition.damage, Definition.cooldown, Definition.range)
            : TEXT("no weapon");
        Add(Result, TEXT("Stats"), FString::Printf(TEXT("HP %.0f, armor %d, speed %.0f, %s, vision %.0f%s."),
            Definition.hp, Definition.armor, Definition.speed, *Weapon, Definition.vision,
            Definition.air ? TEXT(", flying") : Definition.antiAir ? TEXT(", attacks air") : TEXT("")));
    }
    Add(Result, TEXT("Unlock"), UnlockText(Definition));
    return Result;
}
}

FString CinderHelp::TopicTitle(int32 Page)
{
    static const TCHAR* Titles[TopicCount] =
    {
        TEXT("Controls"), TEXT("Economy"), TEXT("Construction"), TEXT("Army"),
        TEXT("Scouting and fog"), TEXT("Technology"), TEXT("Troubleshooting"),
        TEXT("Victory and saves"), TEXT("Unit and building reference")
    };
    return Titles[FMath::Clamp(Page, 0, TopicCount - 1)];
}

FCinderHelpPage CinderHelp::Page(int32 PageIndex, bool bTouch, int32 ReferenceIndex)
{
    PageIndex = FMath::Clamp(PageIndex, 0, TopicCount - 1);
    if (PageIndex == ReferencePage) return ::ReferencePage(ReferenceIndex);

    FCinderHelpPage Result;
    Result.Title = TopicTitle(PageIndex);
    Result.Subtitle = FString::Printf(TEXT("Field guide %d of %d"), PageIndex + 1, TopicCount);
    switch (PageIndex)
    {
    case 0:
        if (bTouch)
        {
            Add(Result, TEXT("Navigate"), TEXT("Drag empty ground to pan. Use two fingers to pan or pinch to zoom."));
            Add(Result, TEXT("Select"), TEXT("Tap a friendly unit to select it, even while it moves. DESELECT clears selection without stopping units or changing squads. Hold, then drag for a selection box."));
            Add(Result, TEXT("Command and formation"), TEXT("Choose MOVE, ATTACK or DEFEND, then tap a destination. ORDERS cycles TIGHT, STANDARD or WIDE spacing. FACE NEXT uses one press-drag: press the destination center, drag toward the arrival facing and release. A short drag sends nothing and stays armed. Queue next appends one Move or Attack waypoint."));
        }
        else
        {
#if PLATFORM_MAC
            Add(Result, TEXT("Navigate"), TEXT("Option-drag to pan. Scroll to zoom. Arrow keys also move the camera."));
#else
            Add(Result, TEXT("Navigate"), TEXT("Alt-drag to pan. Scroll to zoom. Arrow keys also move the camera."));
#endif
            Add(Result, TEXT("Select"), TEXT("Click a friendly unit to select it, even while it moves. DESELECT clears selection without stopping units or changing squads. Drag a box or double-click for its visible type."));
            Add(Result, TEXT("Command and formation"), TEXT("Choose MOVE, ATTACK or DEFEND, then click a destination. ORDERS cycles TIGHT, STANDARD or WIDE spacing. FACE NEXT uses one press-drag from the destination center toward the arrival facing. Hold Shift at release to append Move or Attack. A short drag sends nothing and stays armed."));
        }
        break;
    case 1:
        Add(Result, TEXT("Ore"), TEXT("New Drudges find reachable explored ore automatically. An explicit Anchor rally takes priority. Select the Anchor, open its JOBS, and choose AUTO MINE to restore automatic mining."));
        Add(Result, TEXT("Delivery"), TEXT("A Drudge carries ore to an operational Anchor or Siphon. Shorter routes improve income."));
        Add(Result, TEXT("Crew"), TEXT("An Anchor provides 30 capacity. Each completed Siphon adds 14, up to the 200 cap. Queued units reserve crew."));
        break;
    case 2:
        Add(Result, TEXT("Place"), TEXT("Open BUILD, choose a structure, then place it on currently visible clear ground. A reachable idle Drudge is assigned first, then a miner."));
        Add(Result, TEXT("Build"), TEXT("One Drudge constructs each site. Mining pauses while assigned and resumes afterward; progress stops if the worker leaves."));
        Add(Result, TEXT("Jobs"), TEXT("Open JOBS to inspect progress or cancel. Select an unfinished site when you need to assign a different Drudge."));
        break;
    case 3:
        Add(Result, TEXT("Produce"), FString::Printf(TEXT("Open TRAIN for single or batched orders. Work is balanced across available producers; each queue holds up to %d paid items. Select a combat producer and choose RALLY to override it; USE DEFAULT restores the shared army rally."), cinder::Simulation::MaxQueue));
        Add(Result, TEXT("Organize and rally"), TEXT("ARMY opens the roster and squads. TACTICS recalls a saved squad and opens its ORDERS. ALL selects every combat unit. ARMY > RALLY > SET FLAG sets the destination inherited by current and future combat producers."));
        Add(Result, TEXT("Move, patrol, and escort"), TEXT("MOVE and ATTACK use numbered future waypoints. PATROL repeats between accepted A/B points and returns to its interrupted endpoint after a pursuit; ESCORT follows an owned mobile leader at a stable slot. Both fight inside their marked leash, and the selected leader keeps its order. CLEAR QUEUED keeps the current order; STOP clears all. Queued steps wait behind persistent orders."));
        break;
    case 4:
        Add(Result, TEXT("Vision"), TEXT("Bright ground is visible now. Dim ground was explored earlier. Unexplored ground hides terrain and enemies."));
        Add(Result, TEXT("Memory"), TEXT("Enemy units disappear when sight is lost. Do not treat an old position as current intelligence."));
        Add(Result, TEXT("Skim"), FString::Printf(TEXT("%s is the fast scout with %.0f vision. Check routes before sending slower units."), *Name(cinder::Kind::Scout), cinder::definition(cinder::Kind::Scout).vision));
        break;
    case 5:
        Add(Result, TEXT("Tier 2 path"), FString::Printf(TEXT("Build a %s, then a %s. TECH TIER costs 500 ore and takes 100 seconds."), *Name(cinder::Kind::Foundry), *Name(cinder::Kind::Laboratory)));
        Add(Result, TEXT("Crucible"), FString::Printf(TEXT("After tier 2 completes, build a %s for %d ore. The unlock remains if the Resonator is later lost."), *Name(cinder::Kind::MotorPool), cinder::definition(cinder::Kind::MotorPool).cost));
        Add(Result, TEXT("Upgrades"), TEXT("Open RESEARCH for technology, weapons, or armor. An available Resonator is chosen automatically; select one to override its queue."));
        break;
    case 6:
        Add(Result, TEXT("Locked"), TEXT("Open BUILD, TRAIN, or RESEARCH and choose a locked item to read its requirement. Common causes are missing technology, producer, Kiln, or Anchor."));
        Add(Result, TEXT("Cannot queue"), FString::Printf(TEXT("Check ore, crew capacity, technology, available producers, and the %d-item queue limit. JOBS shows cancellation and unused-cost refunds."), cinder::Simulation::MaxQueue));
        Add(Result, TEXT("Cannot build"), TEXT("Choose visible clear ground reachable by a Drudge. If progress stopped, open JOBS or select the site to assign another worker."));
        break;
    case 7:
        Add(Result, TEXT("Victory"), TEXT("Play 1v1 or 4-player free-for-all online or on a local network. Every opponent is an enemy. Losing every Anchor eliminates your forces; the last commander standing wins. Leaving an active online match forfeits your seat."));
        Add(Result, TEXT("Skirmish saves"), TEXT("Pause a normal skirmish to save or restore it on this device. The save keeps simulation state and issued commands."));
        Add(Result, TEXT("Guided tutorial"), TEXT("The guided battle teaches economy, construction, research, defense, and a final assault. Destroy the opposing Anchor to complete it. Tutorial progress is not saved and never overwrites your skirmish save."));
        break;
    }
    return Result;
}
