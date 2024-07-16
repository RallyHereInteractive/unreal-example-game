#pragma once
#include "GameFramework/RHGameModeBase.h"
#include "Player/Controllers/RHPlayerController.h"
#include "Player/Controllers/RHPlayerState.h"
#include "RHExampleGameMode.generated.h"

UCLASS(MinimalAPI, Config=Game, hideCategories=(HUD))
class ARHExampleGameMode : public ARHGameModeBase
{
	GENERATED_BODY()

public:
	ARHExampleGameMode(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual void PostInitializeComponents() override;

	virtual void PostLogin(APlayerController* NewPlayer) override;

	UFUNCTION(BlueprintPure, Category = "Match Timer")
	virtual float GetMatchTimeElapsed() const;

protected:
	virtual void HandleMatchHasStarted() override;
	virtual void HandleMatchHasEnded() override;
	virtual void HandleMatchAborted() override;

	UPROPERTY(VisibleInstanceOnly, Category = Time)
	float MatchStartTime;

	UPROPERTY(VisibleInstanceOnly, Category = Time)
	float MatchEndTime;

	UPROPERTY()
	class URHExampleStatsMgr* StatsMgr;
};

UCLASS(MinimalAPI, Config=Game, hideCategories=(HUD))
class ARHExampleGameModePersistent : public ARHGameModeBase
{
	GENERATED_BODY()

public:
	ARHExampleGameModePersistent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	
	virtual void NotifySoftStopRequested();
};