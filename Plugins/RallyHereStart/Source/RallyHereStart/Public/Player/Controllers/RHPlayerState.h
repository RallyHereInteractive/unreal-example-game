// Copyright 2022-2023 Rally Here Interactive, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/PlayerState.h"
#include "GameFramework/RHGameModeBase.h"
#include "RHPlayerState.generated.h"

class APlayerController;

UCLASS(Config = Game)
class RALLYHERESTART_API ARHPlayerState : public APlayerState
{
    GENERATED_BODY()

public:
    ARHPlayerState(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
    virtual void BeginPlay();
	
	virtual void OnRep_UniqueId() override;

	UFUNCTION(BlueprintCallable, Category = "Player State")
	class URH_PlayerInfo* GetPlayerInfo(ARHHUDCommon* Hud) const;

	virtual void HandleWelcomeMessage() override;
    virtual void UpdateMultiplayerFeaturesForOSS();

	FORCEINLINE void SetRHPlayerId(const int32& InRHPlayerId) { RHPlayerId = InRHPlayerId; }
	UFUNCTION(BlueprintPure, Category = "Player State")
    FORCEINLINE int32 GetRHPlayerId() const { return RHPlayerId; }

	FORCEINLINE void SetRHPlayerUuid(const FGuid& InRHPlayerUuid) { RHPlayerUuid = InRHPlayerUuid; }
	UFUNCTION(BlueprintPure, Category = "Player State")
	FORCEINLINE FGuid GetRHPlayerUuid() const { return RHPlayerUuid; }

	virtual void GetLifetimeReplicatedProps(TArray< FLifetimeProperty > &OutLifetimeProps) const override;

	UPROPERTY()
	FString SavedSonyActivityId;
	UPROPERTY()
	FString SavedSonyMatchId;

protected:
    virtual void CopyProperties(APlayerState* PlayerState) override;

	UPROPERTY(Replicated)
    int32 RHPlayerId;
	UPROPERTY(Replicated)
	FGuid RHPlayerUuid;
};
