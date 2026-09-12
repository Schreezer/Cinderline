#pragma once

#include "CoreMinimal.h"

struct FCinderHelpSection { FString Heading, Body; };
struct FCinderHelpPage { FString Title, Subtitle; TArray<FCinderHelpSection> Sections; };

namespace CinderHelp
{
    constexpr int32 TopicCount = 9;
    constexpr int32 ReferencePage = 8;
    constexpr int32 ReferenceCount = 15;
    CINDERLINE_API FString TopicTitle(int32 Page);
    CINDERLINE_API FCinderHelpPage Page(int32 PageIndex, bool bTouch, int32 ReferenceIndex = 0);
}
