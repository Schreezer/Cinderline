#include "Presentation/CinderUnitInfo.h"

namespace
{
FString Name(cinder::Kind Kind)
{
    return UTF8_TO_TCHAR(cinder::definition(Kind).name);
}

FString UnlockText(const cinder::Definition& Definition, float TimeScale)
{
    using cinder::Kind;
    if (Definition.kind == Kind::Resource)
        return TEXT("Neutral ore deposit.");
    if (!Definition.building)
        return FString::Printf(TEXT("Tier %d. Costs %d ore and %d crew. Trained at %s in %.0f seconds."),
            Definition.tier, Definition.cost, Definition.supply, *Name(Definition.producer), Definition.buildTime * TimeScale);

    FString Requirement;
    switch (Definition.kind)
    {
    case Kind::Headquarters:
    case Kind::Processor:
    case Kind::Foundry: Requirement = TEXT("Requires an operational Anchor."); break;
    case Kind::MotorPool: Requirement = TEXT("Requires tier 2 and an operational Kiln."); break;
    case Kind::Laboratory:
    case Kind::Turret: Requirement = TEXT("Requires an operational Kiln."); break;
    default: break;
    }
    return FString::Printf(TEXT("Tier %d. Costs %d ore. One Drudge builds it in %.0f seconds. %s"),
        Definition.tier, Definition.cost, Definition.buildTime * TimeScale, *Requirement);
}

FString MatchupNote(const cinder::Definition& Definition)
{
    using cinder::Kind;
    switch (Definition.kind)
    {
    case Kind::Striker: return TEXT("Deals 1.35x damage to Needle and Skim.");
    case Kind::Lancer: return TEXT("Deals 1.8x damage to armored targets and aircraft.");
    case Kind::Scout: return TEXT("Deals 1.5x damage to Drudges.");
    case Kind::Bastion: return TEXT("Deals 1.4x damage to Drudge, Ember, and Needle infantry.");
    case Kind::Mortar: return TEXT("Deals 1.65x damage to buildings and 0.35x inside 150 range; splash deals 42% within 100.");
    case Kind::Turret: return FString::Printf(TEXT("Actual reach is %.0f plus the target's footprint radius."), Definition.range);
    default: return TEXT("Final damage is reduced by the target's armor, with a minimum of 1.");
    }
}
}

FString CinderUnitInfo::Purpose(cinder::Kind Kind)
{
    using cinder::Kind;
    switch (Kind)
    {
    case Kind::Worker: return TEXT("Mine ore and construct your base.");
    case Kind::Striker: return TEXT("Reliable ranged fire against light ground units.");
    case Kind::Lancer: return TEXT("Pierce armored targets and aircraft from long range.");
    case Kind::Scout: return TEXT("Reveal routes quickly and harass enemy workers.");
    case Kind::Bastion: return TEXT("Lead ground pushes and shield lighter infantry.");
    case Kind::Mortar: return TEXT("Shell buildings and groups from behind the front line.");
    case Kind::Mender: return TEXT("Repair nearby allied units while protected by combat units.");
    case Kind::Kite: return TEXT("Cross ground obstacles and intercept aircraft.");
    case Kind::Headquarters: return TEXT("Receive ore and train Drudges.");
    case Kind::Processor: return TEXT("Shorten ore delivery routes and add 14 crew capacity.");
    case Kind::Foundry: return TEXT("Train Ember, Needle, and Skim infantry.");
    case Kind::MotorPool: return TEXT("Produce Anvil, Cinderthrow, and Veil units.");
    case Kind::Laboratory: return TEXT("Research tiers, weapons, and armor, and train Mend support units.");
    case Kind::Turret: return TEXT("Defend a fixed ground and air approach.");
    case Kind::Resource: return TEXT("Send a Drudge here to gather ore.");
    }
    return {};
}

FString CinderUnitInfo::OrderName(cinder::Order Order)
{
    using cinder::Order;
    switch (Order)
    {
    case Order::Idle: return TEXT("Idle");
    case Order::Move: return TEXT("Moving");
    case Order::Attack: return TEXT("Attacking");
    case Order::AttackMove: return TEXT("Attack-moving");
    case Order::Gather: return TEXT("Gathering ore");
    case Order::Hold: return TEXT("Holding position");
    case Order::Construct: return TEXT("Constructing");
    case Order::Defend: return TEXT("Defending");
    case Order::Patrol: return TEXT("Patrolling");
    case Order::Escort: return TEXT("Escorting");
    }
    return TEXT("Idle");
}

float CinderUnitInfo::Range(cinder::Kind Kind)
{
    return cinder::definition(Kind).range;
}

FCinderUnitDetails CinderUnitInfo::Describe(const cinder::Simulation& Simulation, const cinder::Entity& Entity)
{
    const cinder::Definition& Definition = cinder::definition(Entity.kind);
    FCinderUnitDetails Result;
    Result.Name = UTF8_TO_TCHAR(Definition.name);
    Result.Role = UTF8_TO_TCHAR(Definition.role);
    Result.Purpose = Purpose(Entity.kind);
    Result.Capabilities = UnlockText(Definition,
        cinder::matchLengthProfile(Simulation.config().matchLength).productionTimeScale);
    Result.Health = Entity.hp;
    Result.MaxHealth = Definition.hp;
    Result.Range = Range(Entity.kind);
    Result.Cooldown = Definition.cooldown;
    Result.Speed = Definition.speed;
    Result.Vision = Definition.vision;
    Result.bHealer = Entity.kind == cinder::Kind::Mender;
    Result.bBuilding = Definition.building;
    Result.bArmed = Definition.damage > 0;
    Result.bComplete = Entity.progress >= 1.0f;

    const bool bFriendly = Entity.team == 0;
    const cinder::Player* Player = bFriendly ? &Simulation.players()[0] : nullptr;
    Result.Damage = Definition.damage * (Player ? 1.0f + 0.12f * Player->weapons : 1.0f);
    Result.Armor = static_cast<float>(Definition.armor + (Player ? Player->armor : 0));

    if (Result.bHealer)
    {
        Result.HealAmount = 16.0f;
        Result.HealInterval = Definition.cooldown;
        Result.DamageNote = FString::Printf(
            TEXT("No weapon. Heals allied units for 16 HP every %.2f seconds within %.0f center range; buildings cannot be healed."),
            Definition.cooldown, Definition.range);
    }
    else if (Result.bArmed)
    {
        Result.DamageNote = bFriendly
            ? FString::Printf(TEXT("%.1f damage per hit before armor at weapons level %d. %s"),
                Result.Damage, Player->weapons, *MatchupNote(Definition))
            : FString::Printf(TEXT("%.1f base damage per hit before armor; enemy upgrades are unknown. %s"),
                Result.Damage, *MatchupNote(Definition));
    }
    else
    {
        Result.DamageNote = TEXT("No weapon.");
    }

    Result.OrderText = OrderName(Entity.order);
    if (Entity.order == cinder::Order::Gather)
        Result.OrderText = Entity.navigationExhausted
            ? (Entity.returning ? TEXT("Ore delivery blocked") : TEXT("Mining route blocked"))
            : (Entity.returning ? TEXT("Returning ore") : TEXT("Gathering ore"));
    else if (Entity.order == cinder::Order::Defend)
    {
        const float DeltaX = Entity.pos.x - Entity.goal.x;
        const float DeltaY = Entity.pos.y - Entity.goal.y;
        Result.OrderText = DeltaX * DeltaX + DeltaY * DeltaY > 5.0f * 5.0f
            ? TEXT("Moving to defend") : TEXT("Defending");
    }
    else if (Entity.order == cinder::Order::Patrol)
    {
        Result.OrderText = Entity.navigationExhausted ? TEXT("Patrol route blocked")
            : Entity.sustained.phase == cinder::SustainedOrderPhase::Pursuit ? TEXT("Patrol engaging")
            : Entity.sustained.phase == cinder::SustainedOrderPhase::Return ? TEXT("Returning to patrol endpoint")
            : TEXT("Patrolling");
    }
    else if (Entity.order == cinder::Order::Escort)
    {
        Result.OrderText = Entity.navigationExhausted ? TEXT("Escort route blocked")
            : Entity.sustained.phase == cinder::SustainedOrderPhase::Pursuit ? TEXT("Escort engaging")
            : Entity.sustained.phase == cinder::SustainedOrderPhase::Return ? TEXT("Returning to escort slot")
            : FString::Printf(TEXT("Escorting unit #%u"), Entity.sustained.escortTarget);
    }

    return Result;
}
