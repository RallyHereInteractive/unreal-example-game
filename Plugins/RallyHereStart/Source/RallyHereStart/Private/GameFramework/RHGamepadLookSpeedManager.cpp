// Copyright 2022-2023 Rally Here Interactive, Inc. All Rights Reserved.

#include "RallyHereStart.h"
#include "GameFramework/RHGamepadLookSpeedManager.h"
#include "Player/Controllers/RHPlayerController.h"

URHGamepadLookSpeedManager::URHGamepadLookSpeedManager(const FObjectInitializer& ObjectInitializer /*= FObjectInitializer::Get()*/)
	: Super(ObjectInitializer)
{
	PlayerController = nullptr;
}

void URHGamepadLookSpeedManager::Init()
{
	PlayerController = Cast<ARHPlayerController>(GetOuter());
}

FVector2D URHGamepadLookSpeedManager::UpdateGamepadLook(FVector2D GamepadInput, float DeltaTime)
{
	return GamepadInput;
}
