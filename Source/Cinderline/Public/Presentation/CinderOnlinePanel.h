#pragma once
#include "CoreMinimal.h"
class UCinderOnlineSubsystem;
class SWidget;

/** Native Slate lobby. OnClose asks the owning controller to remove the widget. */
TSharedRef<SWidget> MakeCinderOnlinePanel(UCinderOnlineSubsystem* Online, TFunction<void()> OnClose);
