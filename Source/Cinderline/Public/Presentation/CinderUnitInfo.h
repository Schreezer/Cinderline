#pragma once

#include "CoreMinimal.h"
#include "Sim/Simulation.h"

struct FCinderUnitDetails
{
    FString Name;
    FString Role;
    FString Purpose;
    FString Capabilities;
    FString OrderText;
    FString DamageNote;
    float Health = 0;
    float MaxHealth = 0;
    float Damage = 0;
    float Range = 0;
    float Cooldown = 0;
    float Armor = 0;
    float Speed = 0;
    float Vision = 0;
    float HealAmount = 0;
    float HealInterval = 0;
    bool bHealer = false;
    bool bBuilding = false;
    bool bArmed = false;
    bool bComplete = false;
};

namespace CinderUnitInfo
{
    CINDERLINE_API FCinderUnitDetails Describe(const cinder::Simulation& Simulation, const cinder::Entity& Entity);
    CINDERLINE_API FString Purpose(cinder::Kind Kind);
    CINDERLINE_API FString OrderName(cinder::Order Order);
    CINDERLINE_API float Range(cinder::Kind Kind);
}
