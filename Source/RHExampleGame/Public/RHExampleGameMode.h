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
	virtual void Logout(AController* Exiting) override;

	UFUNCTION(BlueprintPure, Category = "Match Timer")
	virtual float GetMatchTimeElapsed() const;

protected:
	virtual void HandleMatchHasStarted() override;
	virtual void HandleMatchHasEnded() override;

	UPROPERTY(VisibleInstanceOnly, Category = Time)
	float MatchStartTime;

	UPROPERTY(VisibleInstanceOnly, Category = Time)
	float MatchEndTime;

	UPROPERTY()
	class URHExampleStatsMgr* StatsMgr;

	UPROPERTY(config)
	float ShutdownOnEmptyDelay;

	FTimerHandle EmptyServerTimerHandle;

	virtual void CheckEmptyTimer(bool bForceStop = false);
	virtual void EmptyTimer();
};