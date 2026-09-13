#pragma once

#include "CoreMinimal.h"

/** Stable presentation palette for the normalized local team and up to three opponents. */
namespace CinderTeamColors
{
constexpr int32 Count = 4;

inline bool IsValid(int32 Team)
{
    return Team >= 0 && Team < Count;
}

inline FLinearColor Color(int32 Team, float Alpha = 1.0f)
{
    switch (Team)
    {
    case 0: return FLinearColor(0.04f, 0.82f, 0.72f, Alpha);
    case 1: return FLinearColor(0.96f, 0.24f, 0.17f, Alpha);
    case 2: return FLinearColor(0.64f, 0.34f, 0.96f, Alpha);
    case 3: return FLinearColor(1.00f, 0.68f, 0.14f, Alpha);
    default: return FLinearColor(0.58f, 0.64f, 0.68f, Alpha);
    }
}

inline FLinearColor Accent(int32 Team, float Alpha = 1.0f)
{
    switch (Team)
    {
    case 0: return FLinearColor(0.68f, 1.00f, 0.92f, Alpha);
    case 1: return FLinearColor(1.00f, 0.53f, 0.43f, Alpha);
    case 2: return FLinearColor(0.82f, 0.65f, 1.00f, Alpha);
    case 3: return FLinearColor(1.00f, 0.86f, 0.40f, Alpha);
    default: return FLinearColor(0.76f, 0.80f, 0.82f, Alpha);
    }
}

inline const TCHAR* Label(int32 Team)
{
    switch (Team)
    {
    case 0: return TEXT("YOU");
    case 1: return TEXT("ENEMY 1");
    case 2: return TEXT("ENEMY 2");
    case 3: return TEXT("ENEMY 3");
    default: return TEXT("NEUTRAL");
    }
}
}
