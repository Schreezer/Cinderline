#include "Presentation/CinderHelpContent.h"

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

FString ReferenceAdvice(cinder::Kind Kind)
{
    using cinder::Kind;
    switch (Kind)
    {
    case Kind::Worker: return TEXT("Keep several mining while one handles construction.");
    case Kind::Striker: return TEXT("Reliable ranged fire against light ground units.");
    case Kind::Lancer: return TEXT("Its piercing shot is strongest against armor and aircraft.");
    case Kind::Scout: return TEXT("Use its speed and wide vision to reveal routes before the army commits.");
    case Kind::Bastion: return TEXT("Lead pushes with it so lighter infantry can fire from behind.");
    case Kind::Mortar: return TEXT("Protect it from close attackers while it shells distant positions.");
    case Kind::Mender: return TEXT("It follows and repairs allies, but carries no weapon.");
    case Kind::Kite: return TEXT("It crosses ground obstacles and can engage aircraft.");
    case Kind::Headquarters: return TEXT("Ore returns here, and its queue trains Drudges.");
    case Kind::Processor: return TEXT("Place it by another ore field to shorten delivery trips.");
    case Kind::Foundry: return TEXT("This is the first military producer and the prerequisite for later structures.");
    case Kind::MotorPool: return FString::Printf(TEXT("Use it for armored support, siege units, and %s aircraft."), *Name(Kind::Kite));
    case Kind::Laboratory: return FString::Printf(TEXT("It queues technology tiers, weapons, armor, and %s support units."), *Name(Kind::Mender));
    case Kind::Turret: return TEXT("Its static weapon covers both ground and air approaches.");
    case Kind::Resource: return TEXT("Select a Drudge, then command this deposit to begin or redirect mining.");
    }
    return {};
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
    Add(Result, TEXT("Role"), FString::Printf(TEXT("%s. %s"), UTF8_TO_TCHAR(Definition.role), *ReferenceAdvice(Definition.kind)));
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
            Add(Result, TEXT("Select"), TEXT("Tap one unit. Hold, then drag for a selection box. ARMY selects every combat unit."));
            Add(Result, TEXT("Command"), TEXT("Tap terrain, ore, or a visible enemy after selecting units. Buttons arm attack-move and building placement."));
        }
        else
        {
#if PLATFORM_MAC
            Add(Result, TEXT("Navigate"), TEXT("Option-drag to pan. Scroll to zoom. Arrow keys also move the camera."));
#else
            Add(Result, TEXT("Navigate"), TEXT("Alt-drag to pan. Scroll to zoom. Arrow keys also move the camera."));
#endif
            Add(Result, TEXT("Select"), TEXT("Click one unit or drag a box. Double-click selects its visible type. ARMY selects combat units."));
            Add(Result, TEXT("Command"), TEXT("Click a destination or target. Secondary click issues a command without changing selection and cancels building placement."));
        }
        break;
    case 1:
        Add(Result, TEXT("Ore"), TEXT("Drudges begin tier 1 already mining. Redirect one by commanding an explored ore deposit."));
        Add(Result, TEXT("Delivery"), TEXT("A Drudge carries ore to an operational Anchor or Siphon. Shorter routes improve income."));
        Add(Result, TEXT("Crew"), TEXT("An Anchor provides 30 capacity. Each completed Siphon adds 14, up to the 200 cap. Queued units reserve crew."));
        break;
    case 2:
        Add(Result, TEXT("Place"), TEXT("Select a Drudge and choose BUILD. Place the site on currently visible, clear ground near that Drudge."));
        Add(Result, TEXT("Build"), TEXT("One Drudge constructs each site. Its mining pauses while assigned, and progress stops if it leaves."));
        Add(Result, TEXT("Recover"), TEXT("Select an unfinished site to assign another Drudge. Canceling refunds the unbuilt portion and removes the foundation."));
        break;
    case 3:
        Add(Result, TEXT("Produce"), FString::Printf(TEXT("Select an operational producer to train units. A queue holds up to %d paid items and runs in order."), cinder::Simulation::MaxQueue));
        Add(Result, TEXT("Position"), TEXT("Command a producer's rally point so new units gather safely. STOP clears orders; HOLD keeps a unit in place."));
        Add(Result, TEXT("Fight"), TEXT("Direct attack focuses a visible target. Attack-move advances while fighting enemies encountered along the route."));
        break;
    case 4:
        Add(Result, TEXT("Vision"), TEXT("Bright ground is visible now. Dim ground was explored earlier. Unexplored ground hides terrain and enemies."));
        Add(Result, TEXT("Memory"), TEXT("Enemy units disappear when sight is lost. Do not treat an old position as current intelligence."));
        Add(Result, TEXT("Skim"), FString::Printf(TEXT("%s is the fast scout with %.0f vision. Check routes before sending slower units."), *Name(cinder::Kind::Scout), cinder::definition(cinder::Kind::Scout).vision));
        break;
    case 5:
        Add(Result, TEXT("Tier 2 path"), FString::Printf(TEXT("Build a %s, then a %s. TECH TIER costs 500 ore and takes 100 seconds."), *Name(cinder::Kind::Foundry), *Name(cinder::Kind::Laboratory)));
        Add(Result, TEXT("Crucible"), FString::Printf(TEXT("After tier 2 completes, build a %s for %d ore. The unlock remains if the Resonator is later lost."), *Name(cinder::Kind::MotorPool), cinder::definition(cinder::Kind::MotorPool).cost));
        Add(Result, TEXT("Upgrades"), TEXT("A Resonator also queues weapons and armor up to the current technology tier. Research uses its normal queue."));
        break;
    case 6:
        Add(Result, TEXT("Locked"), TEXT("Select a locked item to read its requirement. Common causes are missing technology, producer, Kiln, or Anchor."));
        Add(Result, TEXT("Cannot queue"), FString::Printf(TEXT("Check ore, crew capacity, technology, and the %d-item queue limit. Canceling an unfinished item refunds its unused cost."), cinder::Simulation::MaxQueue));
        Add(Result, TEXT("Cannot build"), TEXT("Use a Drudge near currently visible clear ground. If progress stopped, select the site and assign a Drudge."));
        break;
    case 7:
        Add(Result, TEXT("Victory"), TEXT("Destroy the opposing Anchor while keeping yours alive. A match ends as soon as either team has no living Anchor."));
        Add(Result, TEXT("Skirmish saves"), TEXT("Pause a normal skirmish to save or restore it on this device. The save keeps simulation state and issued commands."));
        Add(Result, TEXT("Training"), TEXT("Guided practice is not saved and never overwrites the normal skirmish save. Leaving training ends that session."));
        break;
    }
    return Result;
}
