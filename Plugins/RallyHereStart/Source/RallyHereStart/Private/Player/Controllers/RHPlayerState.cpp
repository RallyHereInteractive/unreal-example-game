// Copyright 2016-2049 Hi-Rez Studios, Inc. All Rights Reserved.

#include "RallyHereStart.h"
#include "Player/Controllers/RHPlayerState.h"
#include "GameFramework/RHGameInstance.h"
#include "Shared/HUD/RHHUDCommon.h"
#include "Managers/RHStoreItemHelper.h"
#include "RHUIBlueprintFunctionLibrary.h"
#include "Player/Controllers/RHPlayerController.h"
#include "RH_LocalPlayer.h"
#include "RH_FriendSubsystem.h"
#include "Net/UnrealNetwork.h"

ARHPlayerState::ARHPlayerState(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
	RHPlayerId = 0;
}

void ARHPlayerState::BeginPlay()
{
    Super::BeginPlay();

	if (ARHPlayerController* PlayerController = Cast<ARHPlayerController>(GetOwningController()))
	{
		if (!SavedSonyMatchId.IsEmpty() || !SavedSonyActivityId.IsEmpty())
		{
			PlayerController->ClientUpdateSonyMatchData(SavedSonyMatchId, SavedSonyActivityId);
		}
	}

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	// TODO: Hook up Stats Tracking system again for ELO and EOM/EMO data (including rewards)
/*
    if (GetLocalRole() == ROLE_Authority)
    {
        // validate the stats tracker
        if (ARHPlayerController* const pRHPC = Cast<ARHPlayerController>(GetOwner()))
        {
            if (IsPlayer())
            {
                pRHPC->ValidateStatsTracker();
            }
        }

        if (ARHAIController* const AIController = Cast<ARHAIController>(GetOwner()))
        {
            AIController->ValidateStatsTracker();
        }
    }
*/
}

void ARHPlayerState::OnRep_UniqueId()
{
	Super::OnRep_UniqueId();

	if (GetUniqueId().IsValid())
	{
		UpdateMultiplayerFeaturesForOSS();

		auto* PC = URHUIBlueprintFunctionLibrary::GetLocalPlayerController(this, 0);
		if (PC != nullptr && PC->Player != nullptr)
		{
			auto* LocalPlayer = Cast<URH_LocalPlayer>(PC->Player);
			if (LocalPlayer != nullptr)
			{
				URH_FriendSubsystem::UpdateRecentPlayerForOSS(LocalPlayer->GetRH_LocalPlayerSubsystem(), GetUniqueId().GetUniqueNetId());
			}
		}
	}
}

URH_PlayerInfo* ARHPlayerState::GetPlayerInfo(ARHHUDCommon* pHud) const
{
	if (pHud == nullptr)
	{
		UE_LOG(RallyHereStart, Error, TEXT("RHPlayerState::GetPlayerInfo null HUD"));
		return nullptr;
	}

	auto PlayerUuid = GetRHPlayerUuid();
	UE_LOG(RallyHereStart, Log, TEXT("RHPlayerState::GetPlayerInfo Getting player info for %s"), *PlayerUuid.ToString());
	return pHud->GetOrCreatePlayerInfo(PlayerUuid);
}

void ARHPlayerState::HandleWelcomeMessage()
{
	Super::HandleWelcomeMessage();

	UpdateMultiplayerFeaturesForOSS();
}

void ARHPlayerState::UpdateMultiplayerFeaturesForOSS()
{
	const FString PlatformName = UGameplayStatics::GetPlatformName();
	const bool bIsSonyPlatform = PlatformName == TEXT("PS4") || PlatformName == TEXT("PS5");
	if (!bIsSonyPlatform)
	{
		return;
	}

	IOnlineSubsystem* OSS = IOnlineSubsystem::Get();
	if (!OSS)
	{
		return;
	}

	UWorld* MyWorld = GetWorld();
	bool bIsUsingMultiplayerFeatures = MyWorld && MyWorld->GetNetMode() != ENetMode::NM_Standalone;
	if (!bIsUsingMultiplayerFeatures)
	{
		return;
	}

	auto* PC = GetPlayerController();

	FUniqueNetIdWrapper LocalPlayerUniqueId;
	if (PC != nullptr && PC->Player != nullptr)
	{
		auto* LocalPlayer = Cast<URH_LocalPlayer>(PC->Player);
		if (LocalPlayer != nullptr)
		{
			LocalPlayerUniqueId = LocalPlayer->GetRH_LocalPlayerSubsystem()->GetOSSUniqueId();
		}
	}

	if (!LocalPlayerUniqueId.IsValid())
	{
		return;
	}

	// Determine:
	// - If local player exists, mark if they are a spectator
	// - Determine if there are multiple live players in the match - to classify as
	//   truly using multiplayer features.
	int32 LivePlayerCount = 0;
	for (TActorIterator<APlayerState> It(MyWorld); It; ++It)
	{
		APlayerState* PlayerState = *It;
		if (!PlayerState)
		{
			continue;
		}

		if (!PlayerState->IsABot())
		{
			LivePlayerCount++;
		}
	}

	if (LivePlayerCount < 2)
	{
		return;
	}

	OSS->SetUsingMultiplayerFeatures(*LocalPlayerUniqueId, true);
}

void ARHPlayerState::GetLifetimeReplicatedProps(TArray< FLifetimeProperty >& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ARHPlayerState, RHPlayerId);
	DOREPLIFETIME(ARHPlayerState, RHPlayerUuid);
}

void ARHPlayerState::CopyProperties(APlayerState* PlayerState)
{
	Super::CopyProperties(PlayerState);

	//Copy ARHPlayerState-specific stats to the inactive state for restoration
	ARHPlayerState* OtherPS = Cast<ARHPlayerState>(PlayerState);
	if (OtherPS != nullptr)
	{
		OtherPS->SetRHPlayerId(GetRHPlayerId());
		OtherPS->SetRHPlayerUuid(GetRHPlayerUuid());

		OtherPS->SavedSonyActivityId = SavedSonyActivityId;
		OtherPS->SavedSonyMatchId = SavedSonyMatchId;
	}
}