// Copyright 2022-2023 Rally Here Interactive, Inc. All Rights Reserved.

#include "RallyHereStart.h"
#include "GameFramework/RHGameInstance.h"
#include "Engine/DemoNetDriver.h"
#include "MoviePlayer.h"
#include "Engine/ActorChannel.h"
#include "Components/ReflectionCaptureComponent.h"
#include "Engine/ReflectionCapture.h"
#include "PlatformInventoryItem/PInv_AssetManager.h"
#include "GameMapsSettings.h"
#include "Managers/RHPushNotificationManager.h"
#include "Managers/RHStoreItemHelper.h"
#include "GameFramework/RHGameModeBase.h"
#include "Misc/CoreDelegates.h"
#include "PlatformInventoryItem/PlatformStoreAsset.h"
#include "DeviceProfiles/DeviceProfileManager.h"
#include "DynamicRHI.h"
#include "RH_LocalPlayerSubsystem.h"
#include "RH_LocalPlayerLoginSubsystem.h"
#include "RH_GameInstanceSubsystem.h"
#include "Interfaces/OnlineGameMatchesInterface.h"
#include "PlayerExperience/PlayerExperienceGlobals.h"

URHGameInstance::URHGameInstance(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	CurrentNetworkError = ERHNetworkError::NoErrors;

#if defined(PLATFORM_XBOXCOMMON)
	if (PLATFORM_XBOXCOMMON)
	{
		bLogoffOnAppSuspend = true;
	}
	else
	{
		bLogoffOnAppSuspend = false;
	}
#else
	bLogoffOnAppSuspend = false;
#endif

	if (PLATFORM_IOS || PLATFORM_ANDROID)
	{
		bLogoffOnAppResume = false;
	}
	else
	{
		bLogoffOnAppResume = true;
	}
}

void URHGameInstance::Init()
{
	Super::Init();

	if (!IsRunningDedicatedServer())
	{
		FCoreUObjectDelegates::PreLoadMap.AddUObject(this, &URHGameInstance::BeginLoadingScreen);
		FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &URHGameInstance::EndLoadingScreen);

		const auto OnlineSub = IOnlineSubsystem::Get();
		if (OnlineSub)
		{
			OnlineSub->AddOnConnectionStatusChangedDelegate_Handle(FOnConnectionStatusChangedDelegate::CreateUObject(this, &URHGameInstance::HandleNetworkConnectionStatusChanged));
			const auto IdentityInterface = OnlineSub->GetIdentityInterface();

			if (IdentityInterface.IsValid())
			{
				for (int i = 0; i < MAX_LOCAL_PLAYERS; ++i)
				{
					IdentityInterface->AddOnLoginStatusChangedDelegate_Handle(i, FOnLoginStatusChangedDelegate::CreateUObject(this, &URHGameInstance::HandleUserLoginChanged));
				}
			}
		}

		// Register for application activated event (returning from suspension on consoles)
		AppSuspendHandle = FCoreDelegates::ApplicationWillEnterBackgroundDelegate.AddUObject(this, &URHGameInstance::AppSuspendCallback);
		AppResumeHandle = FCoreDelegates::ApplicationHasEnteredForegroundDelegate.AddUObject(this, &URHGameInstance::AppResumeCallback);

		AppDeactivatedHandle = FCoreDelegates::ApplicationWillDeactivateDelegate.AddUObject(this, &URHGameInstance::AppDeactivatedCallback);
		AppReactivatedHandle = FCoreDelegates::ApplicationHasReactivatedDelegate.AddUObject(this, &URHGameInstance::AppReactivatedCallback);
	}

	if (UPInv_AssetManager* pAssetManager = Cast<UPInv_AssetManager>(UAssetManager::GetIfInitialized()))
	{
		if (URH_GameInstanceSubsystem* pGISS = GetSubsystem<URH_GameInstanceSubsystem>())
		{
			if (URH_ConfigSubsystem* ConfigSubsystem = pGISS->GetConfigSubsystem())
			{
				ConfigSubsystem->OnKVsUpdated.AddWeakLambda(this, [this](URH_ConfigSubsystem* pUpdatedConfigSubsystem)
					{
						auto* pAssetManager = Cast<UPInv_AssetManager>(UAssetManager::GetIfInitialized());
						if (pAssetManager != nullptr)
						{
							pAssetManager->InitializeDisabledItems(pUpdatedConfigSubsystem);
						}
					});
			}
		}
	}

	UPlayerExperienceGlobals::Get().InitGlobalData(this);

    if (!HasAnyFlags(RF_ClassDefaultObject))
    {
		if ((StoreItemHelper = NewObject<URHStoreItemHelper>(this)) != nullptr)
		{
			StoreItemHelper->Initialize(this);
		}

        if ((UISessionManager = NewObject<URHUISessionManager>(this)) != nullptr)
        {
            UISessionManager->Initialize(this);
        }

		if ((JsonDataFactory = NewObject<URHJsonDataFactory>(this)) != nullptr)
		{
			JsonDataFactory->GameInstance = this;
			JsonDataFactory->Initialize(nullptr);
		}

		PushNotificationManager = NewObject<URHPushNotificationManager>(this);
		if(PushNotificationManager)
		{
			PushNotificationManager->Initialize(JsonDataFactory);
		}

		if ((LootBoxManager = NewObject<URHLootBoxManager>(this)) != nullptr)
		{
			LootBoxManager->Initialize(this, StoreItemHelper);
		}

        if ((OrderManager = NewObject<URHOrderManager>(this)) != nullptr)
        {
            OrderManager->Initialize(StoreItemHelper);
        }

		if (!IsRunningDedicatedServer())
		{
			if ((LoadoutDataFactory = NewObject<URHLoadoutDataFactory>(this)) != nullptr)
			{
				LoadoutDataFactory->Initialize();
			}
		}

		if ((EventManager = NewObject<URHEventManager>(this)) != nullptr)
		{
			EventManager->Initialize();
		}
	}
}

void URHGameInstance::Shutdown()
{
	UPlayerExperienceGlobals::Get().UninitGlobalData(this);

    Super::Shutdown();

    if (StoreItemHelper)
    {
        StoreItemHelper->Uninitialize();
    }

    if (OrderManager)
    {
        OrderManager->Uninitialize();
    }

    if (JsonDataFactory)
    {
        JsonDataFactory->Uninitialize();
    }

    if (UISessionManager)
    {
        UISessionManager->Uninitialize();
    }

	if (LootBoxManager)
	{
		LootBoxManager->Uninitialize();
	}

	if (EventManager)
	{
		EventManager->Uninitialize();
	}

	if (AppSuspendHandle.IsValid())
	{
		FCoreDelegates::ApplicationWillEnterBackgroundDelegate.Remove(AppSuspendHandle);
	}
	if (AppResumeHandle.IsValid())
	{
		FCoreDelegates::ApplicationHasEnteredForegroundDelegate.Remove(AppResumeHandle);
	}
	if (AppDeactivatedHandle.IsValid())
	{
		FCoreDelegates::ApplicationWillDeactivateDelegate.Remove(AppDeactivatedHandle);
	}
	if (AppReactivatedHandle.IsValid())
	{
		FCoreDelegates::ApplicationHasReactivatedDelegate.Remove(AppReactivatedHandle);
	}
}

void URHGameInstance::AppReactivatedCallbackInGameThread()
{
	RefreshMuteStatusesHelper();
}

void URHGameInstance::AppResumeCallback()
{
	UE_LOG(LogOnlineGame, Log, TEXT("URHGameInstance::AppResumeCallback()"));
	// This delegate can be called from 'any' thread, so do the Logoff() call on the game thread
	DECLARE_CYCLE_STAT(TEXT("URHGameInstance::AppResumeCallback"), STAT_RHClient_AppResumeCallback, STATGROUP_TaskGraphTasks);

	const FGraphEventRef Task = FSimpleDelegateGraphTask::CreateAndDispatchWhenReady(
		FSimpleDelegateGraphTask::FDelegate::CreateUObject(this, &URHGameInstance::AppResumeCallbackInGameThread),
		GET_STATID(STAT_RHClient_AppResumeCallback),
		nullptr,
		ENamedThreads::GameThread);

	FTaskGraphInterface::Get().WaitUntilTaskCompletes(Task);
}

void URHGameInstance::AppResumeCallbackInGameThread()
{
	UE_LOG(LogOnlineGame, Log, TEXT("URHGameInstance::AppResumeCallbackInGameThread()"));

	if (bLogoffOnAppResume)
	{
		CurrentNetworkError = ERHNetworkError::DisconnectedUnknown;
		for (const auto& LocalPlayer : LocalPlayers)
		{
			if (URH_LocalPlayerSubsystem* LPSS = LocalPlayer->GetSubsystem<URH_LocalPlayerSubsystem>())
			{
				if (URH_LocalPlayerLoginSubsystem* LoginSubsystem = LPSS->GetLoginSubsystem())
				{
					LoginSubsystem->Logout();
				}
			}
		}
	}

	RefreshMuteStatusesHelper();
}

void URHGameInstance::RefreshMuteStatusesHelper()
{
	/* #RHTODO - Voice - convert to looking at session members
#if defined(PLATFORM_XBOXCOMMON) && PLATFORM_XBOXCOMMON

	IOnlineSubsystem* OSS = IOnlineSubsystem::Get();
	if (OSS == nullptr)
	{
		return;
	}

	FPComClient* pcomClient = pcomGetClient(0);
	if (pcomClient == nullptr)
	{
		return;
	}

	FString AuthTypeToExclude;
	IMessageSanitizerPtr MessageSanitizer = OSS->GetMessageSanitizer(pcomClient->GetActiveControllerIndex(), AuthTypeToExclude);
	if (!MessageSanitizer.IsValid())
	{
		return;
	}

	if (CMctsMatchMaking* mctsMatchMaking = pcomClient->GetMatchMaking())
	{
		if (mctsMatchMaking->GetMatch() != nullptr && mctsMatchMaking->GetMatch()->GetMemberCount() > 0)
		{
			MessageSanitizer->ResetBlockedUserCache();

			for (DWORD m = 0; m < mctsMatchMaking->GetMatch()->GetMemberCount(); m++)
			{
				CMctsMatchMember* pMatchMember = nullptr;
				if (!mctsMatchMaking->GetMatch()->GetMemberAt(m, &pMatchMember) || pMatchMember == nullptr)
				{
					continue;
				}

				const CMctsPlayerPortalInfo* PortalInfo = pMatchMember->GetPortalInfo(pMatchMember->GetLoggedInPortalId());
				if (PortalInfo == nullptr)
				{
					continue;
				}

				CTgPortalIdentity PortalId = PortalInfo->GetPortalIdentity();

				MessageSanitizer->QueryBlockedUser(
					pcomClient->GetActiveControllerIndex(),
					pcomGetUniqueNetIdStringFromPortalIdentityUnfiltered(PortalId),
					pcomGetPlatformNameFromPortal(PortalId.GetPortal()),
					FOnQueryUserBlockedResponse::CreateUObject(this, &URHGameInstance::HandleCheckPerUserPrivilegeResults, (int32)m));
			}
		}
	}
#endif
	*/
}

void URHGameInstance::HandleCheckPerUserPrivilegeResults(const FBlockedQueryResult& QueryResult, int32 MessageId) //Note: we're just passing index into the party member array into the MessageId.
{
	const bool bShouldMute = (QueryResult.bIsBlocked || QueryResult.bIsBlockedNonFriends);
	if (bShouldMute) //Coming back from the check privilege results, we're only going to mute unmuted people, which lowers risk of corner case cert failure.
	{
		ChangeMuteStatusOfPlayerHelper(bShouldMute, MessageId);
	}
}

void URHGameInstance::ChangeMuteStatusOfPlayerHelper(bool bShouldMute, int32 PlayerIndex)
{
	/* #RHTODO - Voice
	auto pcomClient = pcomGetClient(0);
	if (pcomClient == nullptr)
	{
		return;
	}

	if (CMctsMatchMaking* mctsMatchMaking = pcomClient->GetMatchMaking())
	{
		if (mctsMatchMaking->GetMatch() != nullptr)
		{
			DWORD m = (DWORD)PlayerIndex;

			CMctsMatchMember* pMatchMember = nullptr;
			if (!mctsMatchMaking->GetMatch()->GetMemberAt(m, &pMatchMember) || pMatchMember == nullptr)
			{
				return;
			}

			if (pcomGetVoice() != nullptr && pcomGetVoice()->IsInitialized())
			{
				pcomGetVoice()->MutePlayer(pMatchMember->GetPlayerId(), bShouldMute);
			}
		}
	}
	*/
}

void URHGameInstance::SetupAndroidAffinity()
{
#if PLATFORM_ANDROID
	// On Android - some cores are going to be much more powerful than others.
	// Typically - this will be the highest index cores, see: https://developer.android.com/agi/sys-trace/threads-scheduling
	// UE4 does have ability to set this up per-device, see: https://docs.unrealengine.com/4.26/en-US/SharingAndReleasing/Mobile/Android/AndroidConfigRulesSystem/
	// but this is very difficult to maintain and verify.
	// What we will do in the following code is only configure affinity if a per-device override has not been set.

	// TODO: There is the ability to analyze max frequency per core. Will be good to factor that in too.

	if (FAndroidAffinity::GameThreadMask != FPlatformAffinity::GetNoAffinityMask())
	{
		// Affinity has already been configured - leave!
		return;
	}

	// Build "big core" affinity from number of cores:
	int32 NumberOfCores = FPlatformMisc::NumberOfCores();

	int32 LittleCoreAffinityMask = FPlatformAffinity::GetNoAffinityMask();
	int32 BigCoreAffinityMask = FPlatformAffinity::GetNoAffinityMask();
	if (NumberOfCores > 1)
	{
		BigCoreAffinityMask = 1 << (NumberOfCores - 1);

		if (NumberOfCores > 2)
		{
			BigCoreAffinityMask |= 1 << (NumberOfCores - 2);
		}

		if (NumberOfCores > 3)
		{
			BigCoreAffinityMask |= 1 << (NumberOfCores - 3);
		}

		LittleCoreAffinityMask &= (~BigCoreAffinityMask);
	}

	FAndroidAffinity::GameThreadMask = BigCoreAffinityMask;
	FAndroidAffinity::RenderingThreadMask = BigCoreAffinityMask;

	// From FRunnableThreadPThread
	auto TranslateThreadPriority = [](EThreadPriority Priority)
	{
		// these are some default priorities
		switch (Priority)
		{
			// 0 is the lowest, 31 is the highest possible priority for pthread
		case TPri_Highest: case TPri_TimeCritical: return 30;
		case TPri_AboveNormal: return 25;
		case TPri_Normal: return 15;
		case TPri_BelowNormal: return 5;
		case TPri_Lowest: return 1;
		case TPri_SlightlyBelowNormal: return 14;
		default: UE_LOG(LogHAL, Fatal, TEXT("Unknown Priority passed to FRunnableThreadPThread::TranslateThreadPriority()"));
		}
	};

	auto SetGameRenderThreadPriority = [TranslateThreadPriority]()
	{
		sched_param Sched;
		FMemory::Memzero(Sched);
		int32 Policy = SCHED_RR;

		// Read the current policy
		pthread_getschedparam(pthread_self(), &Policy, &Sched);

		// set the priority appropriately
		Sched.sched_priority = TranslateThreadPriority(TPri_Highest);
		pthread_setschedparam(pthread_self(), Policy, &Sched);
	};

	TFunction<void(ENamedThreads::Type CurrentThread)> SetAffinityThreadFunc = [LittleCoreAffinityMask, SetGameRenderThreadPriority](ENamedThreads::Type MyThread)
	{
		if (MyThread == ENamedThreads::GameThread || MyThread == ENamedThreads::GameThread_Local)
		{
			FPlatformProcess::SetThreadAffinityMask(FAndroidAffinity::GameThreadMask);
			SetGameRenderThreadPriority();
		}
		else if (MyThread == ENamedThreads::ActualRenderingThread || MyThread == ENamedThreads::RHIThread)
		{
			FPlatformProcess::SetThreadAffinityMask(FAndroidAffinity::RenderingThreadMask);
			SetGameRenderThreadPriority();
		}
		else
		{
			FPlatformProcess::SetThreadAffinityMask(LittleCoreAffinityMask);
		}
	};
	FTaskGraphInterface::Get().BroadcastSlow_OnlyUseForSpecialPurposes(true, true, SetAffinityThreadFunc);
#endif // PLATFORM_ANDROID
}

ERHNetworkError::Type URHGameInstance::RetrieveCurrentNetworkError() const
{
	return CurrentNetworkError;
}

void URHGameInstance::ClearCurrentNetworkError()
{
	CurrentNetworkError = ERHNetworkError::NoErrors;
}

void URHGameInstance::HandleNetworkConnectionStatusChanged(const FString& ServiceName, EOnlineServerConnectionStatus::Type LastConnectionStatus, EOnlineServerConnectionStatus::Type ConnectionStatus)
{
	UE_LOG(LogOnlineGame, Warning, TEXT("URHGameInstance::HandleNetworkConnectionStatusChanged %s: %s"), *ServiceName, EOnlineServerConnectionStatus::ToString(ConnectionStatus));

	// If we are disconnected from server, and not currently at (or heading to) the welcome screen
	// then display a message on consoles
	if (ConnectionStatus != EOnlineServerConnectionStatus::Connected)
	{
		const FString PlatformName = UGameplayStatics::GetPlatformName();

		UE_LOG(LogOnlineGame, Log, TEXT("URHGameInstance::HandleNetworkConnectionStatusChanged: Logging out"));
		if (PlatformName == TEXT("XSX") || PlatformName == TEXT("XboxOne"))
		{
			CurrentNetworkError = ERHNetworkError::DisconnectedXboxLive;
		}
		else if (PlatformName == TEXT("PS4") || PlatformName == TEXT("PS5"))
		{
			CurrentNetworkError = ERHNetworkError::DisconnectedPlaystationNetwork;
		}
		else
		{
			CurrentNetworkError = ERHNetworkError::DisconnectedUnknown;
		}

		bool bForceLogoff = !(PLATFORM_DESKTOP);

		if (bForceLogoff)
		{
			for (const auto& LocalPlayer : LocalPlayers)
			{
				if (URH_LocalPlayerSubsystem* LPSS = LocalPlayer->GetSubsystem<URH_LocalPlayerSubsystem>())
				{
					if (URH_LocalPlayerLoginSubsystem* LoginSubsystem = LPSS->GetLoginSubsystem())
					{
						LoginSubsystem->Logout();
					}
				}
			}
		}
	}
}

void URHGameInstance::HandleUserLoginChanged(int32 GameUserIndex, ELoginStatus::Type PreviousLoginStatus, ELoginStatus::Type LoginStatus, const FUniqueNetId& ChangedUserId)
{
	UE_LOG(LogOnlineGame, Warning, TEXT("URHGameInstance::HandleUserLoginChanged: User %d, Status %s"), GameUserIndex, ELoginStatus::ToString(LoginStatus));

	// If we are disconnected from server, and not currently at (or heading to) the welcome screen
	// then display a message on consoles
	if (LoginStatus != ELoginStatus::LoggedIn)
	{
		for (const auto& LocalPlayer : LocalPlayers)
		{
			if (URH_LocalPlayerSubsystem* LPSS = LocalPlayer->GetSubsystem<URH_LocalPlayerSubsystem>())
			{
				if (URH_LocalPlayerLoginSubsystem* LoginSubsystem = LPSS->GetLoginSubsystem())
				{
					if (auto LoginOSS = LPSS->GetOSS(LoginSubsystem->LoginOSSName))
					{
						if (auto IdentityInterface = LoginOSS->GetIdentityInterface())
						{
							if (IdentityInterface.IsValid())
							{
								auto UniqueNetId = IdentityInterface->CreateUniquePlayerId(LPSS->GetPlayerPlatformId().UserId);
								if (IdentityInterface->GetLoginStatus(*UniqueNetId) != ELoginStatus::LoggedIn)
								{
									UE_LOG(LogOnlineGame, Log, TEXT("URHGameInstance::HandleNetworkConnectionStatusChanged: Logging out"));
									const FString PlatformName = UGameplayStatics::GetPlatformName();

									if (PlatformName == TEXT("XSX") || PlatformName == TEXT("XboxOne"))
									{
										CurrentNetworkError = ERHNetworkError::SignoutXboxLive;
									}
									else if (PlatformName == TEXT("PS4") || PlatformName == TEXT("PS5"))
									{
										CurrentNetworkError = ERHNetworkError::SignoutPlaystationNetwork;
									}
									else
									{
										CurrentNetworkError = ERHNetworkError::SignoutUnknown;
									}
									LoginSubsystem->Logout();
								}
							}
						}
					}
				}
			}
		}
	}
}

void URHGameInstance::AppSuspendCallback()
{
    UE_LOG(LogOnlineGame, Log, TEXT("URHGameInstance::AppSuspendCallback()"));
    // This delegate can be called from 'any' thread, so do the Logoff() call on the game thread
    DECLARE_CYCLE_STAT(TEXT("URHGameInstance::AppSuspendCallback"), STAT_FPComClient_AppSuspendCallback, STATGROUP_TaskGraphTasks);

    const FGraphEventRef Task = FSimpleDelegateGraphTask::CreateAndDispatchWhenReady(
        FSimpleDelegateGraphTask::FDelegate::CreateUObject(this, &URHGameInstance::AppSuspendCallbackInGameThread),
        GET_STATID(STAT_FPComClient_AppSuspendCallback),
        nullptr,
        ENamedThreads::GameThread);

    FTaskGraphInterface::Get().WaitUntilTaskCompletes(Task);
}

void URHGameInstance::AppSuspendCallbackInGameThread()
{
    UE_LOG(LogOnlineGame, Log, TEXT("URHGameInstance::AppSuspendCallbackInGameThread()"));

	// On Xbox, this is called when the player kills the game via their dashboard overlay
    if (bLogoffOnAppSuspend)
    {
        CurrentNetworkError = ERHNetworkError::DisconnectedUnknown;
		for (const auto& LocalPlayer : LocalPlayers)
		{
			if (URH_LocalPlayerSubsystem* LPSS = LocalPlayer->GetSubsystem<URH_LocalPlayerSubsystem>())
			{
				if (URH_LocalPlayerLoginSubsystem* LoginSubsystem = LPSS->GetLoginSubsystem())
				{
					LoginSubsystem->Logout();
				}
			}
		}
    }
}


void URHGameInstance::AppDeactivatedCallback()
{
    UE_LOG(LogOnlineGame, Log, TEXT("URHGameInstance::AppDeactivatedCallback()"));
    // This delegate can be called from 'any' thread, so do the Logoff() call on the game thread
    DECLARE_CYCLE_STAT(TEXT("URHGameInstance::AppDeactivatedCallback"), STAT_FPComClient_AppDeactivatedCallback, STATGROUP_TaskGraphTasks);

    if (IsInGameThread())
    {
        AppDeactivatedCallbackInGameThread();
    }
    else
    {
        const FGraphEventRef Task = FSimpleDelegateGraphTask::CreateAndDispatchWhenReady(
            FSimpleDelegateGraphTask::FDelegate::CreateUObject(this, &URHGameInstance::AppDeactivatedCallbackInGameThread),
            GET_STATID(STAT_FPComClient_AppDeactivatedCallback),
            nullptr,
            ENamedThreads::GameThread);

        FTaskGraphInterface::Get().WaitUntilTaskCompletes(Task);
    }
}

void URHGameInstance::AppDeactivatedCallbackInGameThread()
{
    UE_LOG(LogOnlineGame, Log, TEXT("URHGameInstance::AppDeactivatedCallbackInGameThread()"));
    // Do nothing... yet...
}

void URHGameInstance::AppReactivatedCallback()
{
    UE_LOG(LogOnlineGame, Log, TEXT("URHGameInstance::AppReactivatedCallback()"));
    // This delegate can be called from 'any' thread, so do the Logoff() call on the game thread
    DECLARE_CYCLE_STAT(TEXT("URHGameInstance::AppReactivatedCallback"), STAT_FPComClient_AppReactivatedCallback, STATGROUP_TaskGraphTasks);

    if (IsInGameThread())
    {
        AppReactivatedCallbackInGameThread();
    }
    else
    {
        const FGraphEventRef Task = FSimpleDelegateGraphTask::CreateAndDispatchWhenReady(
            FSimpleDelegateGraphTask::FDelegate::CreateUObject(this, &URHGameInstance::AppReactivatedCallbackInGameThread),
            GET_STATID(STAT_FPComClient_AppReactivatedCallback),
            nullptr,
            ENamedThreads::GameThread);

        FTaskGraphInterface::Get().WaitUntilTaskCompletes(Task);
    }
}

void URHGameInstance::BeginLoadingScreen(const FString& MapName)
{
    if (!IsRunningDedicatedServer() && !GetMoviePlayer()->IsMovieCurrentlyPlaying())
    {
        FLoadingScreenAttributes LoadingScreen = FLoadingScreenAttributes();
        LoadingScreen.bAutoCompleteWhenLoadingCompletes = false;
        LoadingScreen.WidgetLoadingScreen = FLoadingScreenAttributes::NewTestLoadingScreenWidget();
        GetMoviePlayer()->SetupLoadingScreen(LoadingScreen);
    }
}

void URHGameInstance::EndLoadingScreen(class UWorld* World)
{
    LoadingScreenEndedEvent.Broadcast();
}

void URHGameInstance::SetPresenceForLocalPlayers(const FText& DisplayStatus, const FVariantData& PresenceData)
{
	const auto Presence = Online::GetPresenceInterface();
	if (Presence.IsValid())
	{
		for (int i = 0; i < LocalPlayers.Num(); ++i)
		{
			const TSharedPtr<const FUniqueNetId> UserId = LocalPlayers[i]->GetPreferredUniqueNetId().GetUniqueNetId();

			if (UserId.IsValid())
			{
				FOnlineUserPresenceStatus PresenceStatus;
				PresenceStatus.StatusStr = DisplayStatus.ToString();
				PresenceStatus.Properties.Add(DefaultPresenceKey, PresenceData);

				Presence->SetPresence(*UserId, PresenceStatus);
			}
		}
	}
}

int32 URHGameInstance::AddLocalPlayer(ULocalPlayer* NewPlayer, FPlatformUserId UserId)
{
	int32 Index = Super::AddLocalPlayer(NewPlayer, UserId);

	if (NewPlayer != nullptr && Index != INDEX_NONE)
	{
		if (URH_LocalPlayerSubsystem* RHSS = NewPlayer->GetSubsystem<URH_LocalPlayerSubsystem>())
		{
			if (RHSS->GetAuthContext().IsValid())
			{
				RHSS->GetAuthContext()->OnLoginUserChanged().AddWeakLambda(this, [this, NewPlayer]()
					{
						OnLocalPlayerLoginChanged.Broadcast(NewPlayer);
					});
			}
		}
	}

	return Index;
}

bool URHGameInstance::RemoveLocalPlayer(ULocalPlayer* ExistingPlayer)
{
	if (Super::RemoveLocalPlayer(ExistingPlayer))
	{
		if (ExistingPlayer != nullptr)
		{
			if (URH_LocalPlayerSubsystem* RHSS = ExistingPlayer->GetSubsystem<URH_LocalPlayerSubsystem>())
			{
				if (RHSS->GetAuthContext().IsValid())
				{
					RHSS->GetAuthContext()->OnLoginUserChanged().RemoveAll(this);
				}
			}
		}
		return true;
	}

	return false;
}
