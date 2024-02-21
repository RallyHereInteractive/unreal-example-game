// Copyright 2022-2023 Rally Here Interactive, Inc. All Rights Reserved.

#include "RallyHereStart.h"
#include "Engine/StreamableManager.h"
#include "Engine/AssetManager.h"
#include "RH_GameInstanceSubsystem.h"
#include "RH_LocalPlayerSubsystem.h"
#include "RH_PlayerInfoSubsystem.h"
#include "RHUIBlueprintFunctionLibrary.h"
#include "Managers/RHPartyManager.h"
#include "Shared/HUD/RHHUDCommon.h"
#include "Lobby/Widgets/RHPlayerInfoDisplay.h"

void URHPlayerInfoDisplay::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	// Load our player progression class on creation if its not in memory, we need to display player levels.
	if (PlayerProgressionXpClass.IsValid())
	{
		TSoftObjectPtr<URHProgression> PlayerXpProgressionWeak = TSoftObjectPtr<URHProgression>(PlayerProgressionXpClass);
		
		if (!PlayerXpProgressionWeak.IsValid())
		{
			UAssetManager::GetStreamableManager().RequestAsyncLoad(PlayerXpProgressionWeak.ToSoftObjectPath(), FStreamableDelegate::CreateWeakLambda(this, [this, PlayerXpProgressionWeak]()
				{
					PlayerXpProgression = PlayerXpProgressionWeak.Get();
					OnPlayerProgressionLoaded();
				}));
		}
		else
		{
			PlayerXpProgression = PlayerXpProgressionWeak.Get();
			OnPlayerProgressionLoaded();
		}
	}

	if (auto rhFriendSS = GetRH_LocalPlayerFriendSubsystem())
	{
		rhFriendSS->FriendUpdatedDelegate.AddUObject(this, &URHPlayerInfoDisplay::RHUpdateFriends);
	}

	if (MyHud != nullptr)
	{
		if (URHPartyManager* PartyManager = MyHud->GetPartyManager())
		{
			PartyManager->OnPartyMemberStatusChanged.AddDynamic(this, &URHPlayerInfoDisplay::OnPartyMemberChanged);
		}
	}
}

void URHPlayerInfoDisplay::UninitializeWidget_Implementation()
{
	Super::UninitializeWidget_Implementation();

	if (auto rhFriendSS = GetRH_LocalPlayerFriendSubsystem())
	{
		rhFriendSS->FriendUpdatedDelegate.RemoveAll(this);
	}

	if (MyHud != nullptr)
	{
		if (URHPartyManager* PartyManager = MyHud->GetPartyManager())
		{
			PartyManager->OnPartyMemberStatusChanged.RemoveAll(this);
		}
	}

	if (MyPlayerInfo != nullptr)
	{
		if (URH_PlayerPresence* MyPresence = MyPlayerInfo->GetPresence())
		{
			MyPresence->OnUpdatedDelegate.RemoveAll(this);
		}
	}
}

void URHPlayerInfoDisplay::OnPartyMemberChanged(const FGuid& PlayerUuid)
{
	if (MyPlayerInfo != nullptr)
	{
		if (PlayerUuid == MyPlayerInfo->GetRHPlayerUuid())
		{
			OnPlayerPresenceUpdated(MyPlayerInfo->GetPresence());
			return;
		}
	}
}

void URHPlayerInfoDisplay::RHUpdateFriends(URH_RHFriendAndPlatformFriend* Friend)
{
	if (MyPlayerInfo != nullptr && Friend != nullptr)
	{
		if (Friend->GetRHPlayerUuid() == MyPlayerInfo->GetRHPlayerUuid())
		{
			OnPlayerPresenceUpdated(MyPlayerInfo->GetPresence());
			return;
		}
	}
}

void URHPlayerInfoDisplay::SetPlayerInfo(URH_PlayerInfo* PlayerInfo)
{
	if (MyPlayerInfo != nullptr)
	{
		if (URH_PlayerPresence* MyPresence = MyPlayerInfo->GetPresence())
		{
			MyPresence->OnUpdatedDelegate.RemoveAll(this);
		}
	}

	MyPlayerInfo = PlayerInfo;
	
	if (URH_PlayerPresence* MyPresence = MyPlayerInfo->GetPresence())
	{
		MyPresence->OnUpdatedDelegate.AddWeakLambda(this, [this](URH_PlayerInfoSubobject* PlayerPresence)
			{
				if (MyPlayerInfo != nullptr && MyPlayerInfo->GetPresence() != nullptr)
				{
					OnPlayerPresenceUpdated(MyPlayerInfo->GetPresence());
				}
			});
		MyPresence->RequestUpdate();
	}
}

void URHPlayerInfoDisplay::GetPlayerLevel(const FRH_GetInventoryCountBlock& Delegate)
{
	if (PlayerXpProgression != nullptr && MyPlayerInfo != nullptr)
	{
		if (const URH_PlayerInventory* PlayerInventory = MyPlayerInfo->GetPlayerInventory())
		{
			PlayerInventory->GetInventoryCount(PlayerXpProgression->GetItemId(), FRH_GetInventoryCountDelegate::CreateWeakLambda(this, [this, Delegate](int32 Count)
				{
					Delegate.ExecuteIfBound(PlayerXpProgression->GetProgressionLevel(this, Count));
				}));
			return;
		}
	}
	Delegate.ExecuteIfBound(0);
}

void URHPlayerInfoDisplay::GetPlayerPlatform(const FRH_GetPlayerPlatformDynamicDelegate& Delegate)
{
	if (MyRHFriend)
	{
		if (const auto* MyPlatformFriend = MyRHFriend->GetPlatformFriend(URHUIBlueprintFunctionLibrary::GetLoggedInPlatformId(MyHud.Get())))
		{
			Delegate.ExecuteIfBound(URHUIBlueprintFunctionLibrary::ConvertPlatformTypeToDisplayType(MyHud.Get(), MyPlatformFriend->GetPlatform()));
			return;
		}
	}

	GetPlayerPresence(FRH_OnRequestPlayerInfoSubobjectDelegate::CreateUObject(this, &URHPlayerInfoDisplay::OnGetPlayerPlatformPresenceResponse, Delegate));
}

void URHPlayerInfoDisplay::OnGetPlayerPlatformPresenceResponse(bool bSuccessful, URH_PlayerInfoSubobject* PlayerSubobject, const FRH_GetPlayerPlatformDynamicDelegate Delegate)
{
	auto PlayerPresence = Cast<URH_PlayerPresence>(PlayerSubobject);
	if (bSuccessful && PlayerPresence != nullptr)
	{
		if (!PlayerPresence->Platform.IsEmpty())
		{
			ERHAPI_Platform PlayerPlatform;
			EnumFromString(PlayerPresence->Platform, PlayerPlatform);
			Delegate.ExecuteIfBound(URHUIBlueprintFunctionLibrary::ConvertPlatformTypeToDisplayType(MyHud.Get(), PlayerPlatform));
			return;
		}

		OnPlayerPresenceUpdated(PlayerPresence);
	}

	if (MyPlayerInfo != nullptr)
	{
		MyPlayerInfo->GetLinkedPlatformInfo(FTimespan(), true, FRH_PlayerInfoGetPlatformsDelegate::CreateUObject(this, &URHPlayerInfoDisplay::OnGetPlayerPlatformPlatformsResponse, Delegate));
		return;
	}

	Delegate.ExecuteIfBound(URHUIBlueprintFunctionLibrary::ConvertPlatformTypeToDisplayType(MyHud.Get(), ERHAPI_Platform::Anon));
}

void URHPlayerInfoDisplay::OnGetPlayerPlatformPlatformsResponse(bool bSuccess, const TArray<URH_PlayerPlatformInfo*>& Platforms, const FRH_GetPlayerPlatformDynamicDelegate Delegate)
{
	if (bSuccess && Platforms.Num())
	{
		// TODO: If possible get the platform that matches the username they have
		Delegate.ExecuteIfBound(URHUIBlueprintFunctionLibrary::ConvertPlatformTypeToDisplayType(MyHud.Get(), Platforms[0]->GetPlatform()));
		return;
	}

	Delegate.ExecuteIfBound(URHUIBlueprintFunctionLibrary::ConvertPlatformTypeToDisplayType(MyHud.Get(), ERHAPI_Platform::Anon));
}

void URHPlayerInfoDisplay::GetPlayerPresence(const FRH_OnRequestPlayerInfoSubobjectDelegate& Delegate) const
{
	if (MyPlayerInfo == nullptr || MyPlayerInfo->GetPresence() == nullptr)
	{
		Delegate.ExecuteIfBound(false, nullptr);
		return;
	}

	MyPlayerInfo->GetPresence()->RequestUpdate(false, Delegate);
}

URH_PlayerInfoSubsystem* URHPlayerInfoDisplay::GetRH_PlayerInfoSubsystem() const
{
	if (!MyHud.IsValid())
	{
		return nullptr;
	}

	APlayerController* PC = MyHud->GetOwningPlayerController();
	if (!PC)
	{
		UE_LOG(RallyHereStart, Error, TEXT("[%s] Failed to get owning player controller"), ANSI_TO_TCHAR(__FUNCTION__));
		return nullptr;
	}

	ULocalPlayer* LocalPlayer = PC->GetLocalPlayer();
	if (!LocalPlayer)
	{
		UE_LOG(RallyHereStart, Error, TEXT("[%s] Failed to get local player for controller"), ANSI_TO_TCHAR(__FUNCTION__));
		return nullptr;
	}

	URH_LocalPlayerSubsystem* RHSS = LocalPlayer->GetSubsystem<URH_LocalPlayerSubsystem>();
	if (!RHSS)
	{
		UE_LOG(RallyHereStart, Error, TEXT("[%s] RallyHere failed to get player info subsystem for controller"), ANSI_TO_TCHAR(__FUNCTION__));
		return nullptr;
	}

	return RHSS->GetPlayerInfoSubsystem();
}

URH_FriendSubsystem* URHPlayerInfoDisplay::GetRH_LocalPlayerFriendSubsystem() const
{
	if (!MyHud.IsValid())
	{
		return nullptr;
	}

	APlayerController* PC = MyHud->GetOwningPlayerController();
	if (!PC)
	{
		UE_LOG(RallyHereStart, Error, TEXT("[%s] Failed to get owning player controller"), ANSI_TO_TCHAR(__FUNCTION__));
		return nullptr;
	}

	ULocalPlayer* LocalPlayer = PC->GetLocalPlayer();
	if (!LocalPlayer)
	{
		UE_LOG(RallyHereStart, Error, TEXT("[%s] Failed to get local player for controller"), ANSI_TO_TCHAR(__FUNCTION__));
		return nullptr;
	}

	URH_LocalPlayerSubsystem* RHSS = LocalPlayer->GetSubsystem<URH_LocalPlayerSubsystem>();
	if (!RHSS)
	{
		UE_LOG(RallyHereStart, Error, TEXT("[%s] RallyHere failed to get friend subsystem for controller"), ANSI_TO_TCHAR(__FUNCTION__));
		return nullptr;
	}

	return RHSS->GetFriendSubsystem();
}