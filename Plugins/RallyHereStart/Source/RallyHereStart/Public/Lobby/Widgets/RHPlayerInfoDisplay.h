// Copyright 2022-2023 Rally Here Interactive, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Shared/Widgets/RHWidget.h"
#include "Inventory/RHProgression.h"
#include "RH_FriendSubsystem.h"
#include "RH_PlayerInfoSubsystem.h"
#include "RHPlayerInfoDisplay.generated.h"

DECLARE_DYNAMIC_DELEGATE_OneParam(FRH_GetPlayerPlatformDynamicDelegate, ERHPlatformDisplayType, PlatformType);

UCLASS(Config = game)
class RALLYHERESTART_API URHPlayerInfoDisplay : public URHWidget
{
    GENERATED_BODY()

public:
	virtual void NativeOnInitialized() override;
	virtual void UninitializeWidget_Implementation() override;

	UFUNCTION(BlueprintCallable)
	void SetPlayerInfo(URH_PlayerInfo* PlayerInfo);

	UFUNCTION(BlueprintPure, Category = "Progression")
	URHProgression* GetPlayerXpProgression() const { return PlayerXpProgression; }

	UFUNCTION(BlueprintCallable, Category = "Progression", meta = (DisplayName = "Get Player Level", AutoCreateRefTerm = "Delegate"))
	void BLUEPRINT_GetPlayerLevel(const FRH_GetInventoryCountDynamicDelegate& Delegate) { GetPlayerLevel(Delegate); }
	void GetPlayerLevel(const FRH_GetInventoryCountBlock& Delegate = FRH_GetInventoryCountBlock());

	UFUNCTION(BlueprintImplementableEvent)
	void OnPlayerPresenceUpdated(URH_PlayerPresence* PlayerPresence);

	UFUNCTION(BlueprintImplementableEvent)
	void OnPlayerProgressionLoaded();

	UFUNCTION(BlueprintCallable)
	void GetPlayerPlatform(const FRH_GetPlayerPlatformDynamicDelegate& Delegate);

protected:
	UPROPERTY(BlueprintReadWrite)
	URH_RHFriendAndPlatformFriend* MyRHFriend;

	UPROPERTY(BlueprintReadOnly)
	URH_PlayerInfo* MyPlayerInfo;

	URH_PlayerInfoSubsystem* GetRH_PlayerInfoSubsystem() const;
	URH_FriendSubsystem* GetRH_LocalPlayerFriendSubsystem() const;

	UFUNCTION()
	void OnGetPlayerPlatformPresenceResponse(bool bSuccessful, URH_PlayerInfoSubobject* NewPresence, const FRH_GetPlayerPlatformDynamicDelegate Delegate);

	UFUNCTION()
	void OnGetPlayerPlatformPlatformsResponse(bool bSuccess, const TArray<URH_PlayerPlatformInfo*>& Platforms, const FRH_GetPlayerPlatformDynamicDelegate Delegate);

	void GetPlayerPresence(const FRH_OnRequestPlayerInfoSubobjectDelegate& Delegate) const;

	UFUNCTION()
	void RHUpdateFriends(URH_RHFriendAndPlatformFriend* Friend);

	UFUNCTION()
	void OnPartyMemberChanged(const FGuid& PlayerUuid);

private:
	UPROPERTY(config)
	FSoftObjectPath PlayerProgressionXpClass;

	UPROPERTY()
	URHProgression* PlayerXpProgression;
};
