#pragma once

#include "CoreMinimal.h"
#include "RH_LocalPlayerSubsystem.h"
#include "RH_GameInstanceSubsystem.h"
#include "RHExampleSubsystems.generated.h"

UCLASS()
class RHEXAMPLEGAME_API URHExampleLocalPlayerSubsystem : public URH_LocalPlayerSubsystem
{
	GENERATED_BODY()	
};

UCLASS()
class RHEXAMPLEGAME_API URHExampleGameInstanceSubsystem : public URH_GameInstanceSubsystem
{
	GENERATED_BODY()
};
