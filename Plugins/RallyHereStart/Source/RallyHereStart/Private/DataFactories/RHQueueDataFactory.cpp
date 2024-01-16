#include "RallyHereStart.h"
#include "RH_GameInstanceSubsystem.h"
#include "RH_LocalPlayerSessionSubsystem.h"
#include "Lobby/HUD/RHLobbyHUD.h"
#include "Managers/RHPartyManager.h"
#include "Managers/RHPopupManager.h"
#include "DataFactories/RHQueueDataFactory.h"

URHQueueDataFactory::URHQueueDataFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer),
	bInitialized(false),
	RHSessionType(FString(TEXT("game"))),
	SessionLeaderNameFieldName(FString(TEXT("leader_name"))),
	CustomMatchMembers(),
	SelectedCustomMatchMapRowName(FName(TEXT("Map1"))),
	PendingConfirmPlayerId(),
	PendingSessionId(TEXT("")),
	QueueIds(),
	DefaultQueueId(TEXT("")),
	SelectedQueueId(TEXT("")),
	QueueUpdatePollInterval(30.f),
	QueueUpdateTimerHandle(),
	QueuedStartTime(FDateTime::MinValue())
{
}

void URHQueueDataFactory::Initialize(class ARHHUDCommon* InHud)
{
	UE_LOG(RallyHereStart, Log, TEXT("URHQueueDataFactory::Initialize()."));

	MyHud = InHud;

	if (QueuesDataTableClassName.ToString().Len() > 0)
	{
		QueueDetailsDT = LoadObject<UDataTable>(nullptr, *QueuesDataTableClassName.ToString(), nullptr, LOAD_None, nullptr);
	}

	if (MapsDataTableClassName.ToString().Len() > 0)
	{
		MapsDetailsDT = LoadObject<UDataTable>(nullptr, *MapsDataTableClassName.ToString(), nullptr, LOAD_None, nullptr);
	}

	SelectedQueueId = DefaultQueueId;

	bool bLoggedIn = false;
	if (MyHud != nullptr && MyHud->GetLocalPlayerSubsystem())
	{
		bLoggedIn = MyHud->GetLocalPlayerSubsystem()->IsLoggedIn();
	}

	if (bLoggedIn)
	{
		UpdateCustomMatchInfo();

		// set the timer for checking queue data
		StartQueueUpdatePollTimer();
	}

	if (URH_LocalPlayerSessionSubsystem* SessionSS = GetLocalPlayerSessionSubsystem())
	{
		SessionSS->OnSessionUpdatedDelegate.AddUObject(this, &URHQueueDataFactory::HandleSessionUpdated);
		SessionSS->OnSessionAddedDelegate.AddUObject(this, &URHQueueDataFactory::HandleSessionAdded);
		SessionSS->OnSessionRemovedDelegate.AddUObject(this, &URHQueueDataFactory::HandleSessionRemoved);
		SessionSS->OnLoginPollSessionsCompleteDelegate.AddUObject(this, &URHQueueDataFactory::HandleLoginPollSessionsComplete);
	}

	bInitialized = true;
}

void URHQueueDataFactory::Uninitialize()
{
	UE_LOG(RallyHereStart, Log, TEXT("URHQueueDataFactory::Uninitialize()."));

	// clear the timer for checking queue data
	StopQueueUpdatePollTimer();

	if (URH_LocalPlayerSessionSubsystem* SessionSS = GetLocalPlayerSessionSubsystem())
	{
		SessionSS->OnSessionUpdatedDelegate.RemoveAll(this);
		SessionSS->OnSessionAddedDelegate.RemoveAll(this);
		SessionSS->OnSessionRemovedDelegate.RemoveAll(this);
		SessionSS->OnLoginPollSessionsCompleteDelegate.RemoveAll(this);
	}
}

void URHQueueDataFactory::PostLogin()
{
	bCheckForAutoRejoin = true; // only check for autorejoin once after attempting a login

	// set the timer for checking queue data
	StartQueueUpdatePollTimer();
}

void URHQueueDataFactory::PostLogoff()
{
	// clear the timer for checking queue data
	StopQueueUpdatePollTimer();

	LastLoginPlayerGuid.Invalidate();
}

bool URHQueueDataFactory::GetQueueDetailsByQueueId(const FString& QueueId, FRHQueueDetails& QueueDetails) const
{
	if (QueueDetailsDT != nullptr)
	{
		const FRHQueueDetails* FindRow = QueueDetailsDT->FindRow<FRHQueueDetails>(FName(QueueId), "Get Queue Details");

		if (FindRow)
		{
			QueueDetails = *FindRow;
			return true;
		}
	}

	return false;
}

void URHQueueDataFactory::ClearCustomMatchData()
{
	// nothing to clear
	if (CustomMatchMembers.Num() == 0)
	{
		return;
	}

	CustomMatchMembers = {};
	OnCustomMatchDataChanged.Broadcast();
}

void URHQueueDataFactory::StartQueueUpdatePollTimer()
{
	// set timer
	UWorld* World = GetWorld();
	if (World != nullptr && !World->GetTimerManager().IsTimerActive(QueueUpdateTimerHandle))
	{
		World->GetTimerManager().SetTimer(QueueUpdateTimerHandle, this, &URHQueueDataFactory::PollQueues, QueueUpdatePollInterval, true, 0.0f);
	}
}

void URHQueueDataFactory::StopQueueUpdatePollTimer()
{
	// clear timer
	UWorld* World = GetWorld();
	if (World != nullptr && World->GetTimerManager().IsTimerActive(QueueUpdateTimerHandle))
	{
		World->GetTimerManager().ClearTimer(QueueUpdateTimerHandle);
	}
}

void URHQueueDataFactory::PollQueues()
{
	if (auto pGIMatchmakingCache = GetMatchmakingCache())
	{
		FRH_QueueSearchParams params;
		params.PageSize = 50;

		pGIMatchmakingCache->SearchQueues(params, FRH_OnQueueSearchCompleteDelegate::CreateUObject(this, &URHQueueDataFactory::HandleQueuesPopulated));
	}
}

TArray<URH_MatchmakingQueueInfo*> URHQueueDataFactory::GetQueues() const
{
	if (auto pGIMatchmakingCache = GetMatchmakingCache())
	{
		return pGIMatchmakingCache->GetAllQueues();
	}

	return TArray<URH_MatchmakingQueueInfo*>();
}

URH_MatchmakingQueueInfo* URHQueueDataFactory::GetQueueInfoById(const FString& QueueId) const
{
	if (auto pGIMatchmakingCache = GetMatchmakingCache())
	{
		return pGIMatchmakingCache->GetQueue(QueueId);
	}

	return nullptr;
}

bool URHQueueDataFactory::IsQueueActive(const FString& QueueId) const
{
	return IsQueueActiveHelper(QueueId, false);
}

bool URHQueueDataFactory::IsQueueActiveHelper(const FString& QueueId, bool bAllowHidden) const
{
	if (URH_MatchmakingQueueInfo* pQueueInfo = GetQueueInfoById(QueueId))
	{
		return pQueueInfo->IsActive();
	}

	return false;
}

bool URHQueueDataFactory::CanQueue() const
{
	if (URHPartyManager* PartyManager = GetPartyManager())
	{
		if (URH_OnlineSession* OnlineSession = Cast<URH_OnlineSession>(PartyManager->GetPartySession()))
		{
			// check if we're currently in queue
			if (OnlineSession->IsInQueue())
			{
				return false;
			}

			if (PartyManager->IsInParty())
			{
				// not leader, block queueing
				if (!PartyManager->IsLeader())
				{
					return false;
				}
			}
		}
	}

	return true;
}

bool URHQueueDataFactory::JoinQueue(FString QueueId)
{
	auto HandleJoinQueueDelegate = FRH_OnSessionUpdatedDelegate::CreateWeakLambda(this, [this, QueueId] (bool bSuccess, URH_SessionView* pSession, const FRH_ErrorInfo& ErrorInfo)
		{
			if (bSuccess)
			{
				QueuedStartTime = FDateTime::UtcNow(); // originally from MatchStatusUpdate, may need to go back
				OnQueueJoined.Broadcast(QueueId);
			}
			else
			{
				UE_LOG(RallyHereStart, Warning, TEXT("URHQueueDataFactory::JoinQueue - Failed to join queue %s"), *QueueId);
				OnQueueLeft.Broadcast();
			}

			SendMatchStatusUpdateNotify(GetCurrentQueueMatchState()); // originally from MatchStatusUpdate, may need to go back
		});

	if (QueueId.IsEmpty())
	{
		return false;
	}

	if (URHPartyManager* PartyManager = GetPartyManager())
	{
		if (URH_OnlineSession* OnlineSession = Cast<URH_OnlineSession>(PartyManager->GetPartySession()))
		{
			if (!OnlineSession->IsInQueue())
			{
				OnlineSession->JoinQueue(QueueId, TArray<FString>(), HandleJoinQueueDelegate);
				return true;
			}
			else
			{
				UE_LOG(RallyHereStart, Warning, TEXT("URHQueueDataFactory::JoinQueue - Session is already in queue."));
			}
		}
		else
		{
			UE_LOG(RallyHereStart, Warning, TEXT("URHQueueDataFactory::JoinQueue - PartySession is not a URH_OnlineSession."));
		}
	}

	return false;
}

bool URHQueueDataFactory::LeaveQueue()
{
	auto HandleLeaveQueueDelegate = FRH_OnSessionUpdatedDelegate::CreateWeakLambda(this, [this] (bool bSuccess, URH_SessionView* pSession, const FRH_ErrorInfo& ErrorInfo)
		{
			if (bSuccess)
			{
				QueuedStartTime = FDateTime::MinValue(); // originally from MatchStatusUpdate, may need to go back
				OnQueueLeft.Broadcast();
			}
			else
			{
				UE_LOG(RallyHereStart, Warning, TEXT("URHQueueDataFactory::LeaveQueue - Failed to leave queue."));
			}

			SendMatchStatusUpdateNotify(GetCurrentQueueMatchState()); // originally from MatchStatusUpdate, may need to go back
		});

	if (URHPartyManager* PartyManager = GetPartyManager())
	{
		if (URH_OnlineSession* OnlineSession = Cast<URH_OnlineSession>(PartyManager->GetPartySession()))
		{
			if (OnlineSession->IsInQueue())
			{
				OnlineSession->LeaveQueue(HandleLeaveQueueDelegate);
				return true;
			}
			else
			{
				UE_LOG(RallyHereStart, Warning, TEXT("URHQueueDataFactory::LeaveQueue - Session is NOT in a queue."));
			}
		}
		else
		{
			UE_LOG(RallyHereStart, Warning, TEXT("URHQueueDataFactory::LeaveQueue - PartySession is not a URH_OnlineSession."));
		}
	}

	return false;
}

bool URHQueueDataFactory::LeaveMatch()
{
	bool bReturn = false;
	
	if (const URH_LocalPlayerSessionSubsystem* SessionSS = GetLocalPlayerSessionSubsystem())
	{
		TArray<URH_JoinedSession*> JoinedSessions = SessionSS->GetJoinedSessionsByType(RHSessionType);
		for (URH_JoinedSession* pJoinedSession : JoinedSessions)
		{
			pJoinedSession->Leave(false);
			bReturn = true;
		}
	}
	
	return bReturn;
}

bool URHQueueDataFactory::AttemptRejoinMatch()
{
	if (const URH_LocalPlayerSessionSubsystem* SessionSS = GetLocalPlayerSessionSubsystem())
	{
		TArray<URH_JoinedSession*> JoinedSessions = SessionSS->GetJoinedSessionsByType(RHSessionType);
		for (URH_JoinedSession* pJoinedSession : JoinedSessions)
		{
			if (!pJoinedSession->IsBeaconSession())
			{
				if (auto* InstanceData = pJoinedSession->GetInstanceData())
				{
					if (InstanceData->JoinStatus == ERHAPI_InstanceJoinableStatus::Joinable)
					{
						if (auto* GISessionSubsystem = GetGameInstanceSessionSubsystem())
						{
							GISessionSubsystem->SyncToSession(pJoinedSession);
							return true;
						}
					}
				}
			}
		}
	}

	return false;
}

float URHQueueDataFactory::GetTimeInQueueSeconds() const
{
	if (QueuedStartTime.GetTicks() != 0)
	{
		FTimespan TimeInQueue = FDateTime::UtcNow() - QueuedStartTime;
		return TimeInQueue.GetTotalSeconds();
	}
	return 0.f;
}

bool URHQueueDataFactory::GetPreferredRegionId(FString& OutRegionId)
{
	return MyHud->GetPreferredRegionId(OutRegionId);
}

void URHQueueDataFactory::UpdateCustomMatchInfo()
{
	ClearCustomMatchData();

	if (MyHud == nullptr || CustomMatchSession == nullptr)
	{
		return;
	}

	if (const URH_LocalPlayerSubsystem* RHSS = MyHud->GetLocalPlayerSubsystem())
	{
		if (URH_FriendSubsystem* FriendSS = RHSS->GetFriendSubsystem())
		{
			// Populate CustomMatchMembers
			TArray<FRHAPI_SessionTeam> Teams = CustomMatchSession->GetSessionData().GetTeams();
			for (int i = 0; i < Teams.Num(); i++)
			{
				for (FRHAPI_SessionPlayer Player : Teams[i].GetPlayers())
				{
					FRH_CustomMatchMember NewPlayerData;
					NewPlayerData.TeamId = i;
					NewPlayerData.PlayerUUID = Player.GetPlayerUuid();
					NewPlayerData.bIsPendingInvite = Player.GetStatus() == ERHAPI_SessionPlayerStatus::Invited;
					if (URH_PlayerInfo* MemberPlayerInfo = MyHud->GetOrCreatePlayerInfo(Player.GetPlayerUuid()))
					{
						NewPlayerData.Friend = FriendSS->GetOrCreateFriend(MemberPlayerInfo);
					}

					CustomMatchMembers.Add(NewPlayerData);
				}
			}

			// Lobby Update delegate
			OnCustomMatchDataChanged.Broadcast();
		}
	}
}

void URHQueueDataFactory::UpdateCustomSessionBrowserInfo()
{
	if (CustomMatchSession)
	{
		if (IsLocalPlayerCustomLobbyLeader())
		{
			if (MyHud != nullptr)
			{
				if (URH_PlayerInfo* LocalPlayerInfo = MyHud->GetLocalPlayerInfo())
				{
					LocalPlayerInfo->GetLastKnownDisplayNameAsync(FTimespan(), false, ERHAPI_Platform::Anon,
						FRH_PlayerInfoGetDisplayNameDelegate::CreateWeakLambda(this, [this, LocalPlayerInfo](bool bSuccess, const FString& DisplayName)
							{
								FString NameToUse = bSuccess ? DisplayName : LocalPlayerInfo->GetRHPlayerUuid().ToString(EGuidFormats::DigitsWithHyphens);
								TMap<FString, FString> CustomData;
								CustomData.Emplace(SessionLeaderNameFieldName, NameToUse);
								if (CustomMatchSession)
								{
									CustomMatchSession->UpdateBrowserInfo(true, CustomData);
								}
							}));
				}
			}
		}
	}
}

void URHQueueDataFactory::SendMatchStatusUpdateNotify(ERH_MatchStatus MatchStatus)
{
	OnQueueStatusChange.Broadcast(MatchStatus);
}

void URHQueueDataFactory::HandleQueuesPopulated(bool bSuccess, const FRH_QueueSearchResult& SearchResult, const FRH_ErrorInfo& ErrorInfo)
{
	if (bSuccess)
	{
		// broadcast an update
		OnQueueDataUpdated.Broadcast();
	}
}

/*
* Queue status info helpers
*/

// returns true when the local player is currently queued
bool URHQueueDataFactory::IsInQueue() const
{
	if (URHPartyManager* PartyManager = GetPartyManager())
	{
		if (URH_OnlineSession* OnlineSession = Cast<URH_OnlineSession>(PartyManager->GetPartySession()))
		{
			return OnlineSession->IsInQueue();
		}
	}

	// default return
	UE_LOG(RallyHereStart, Warning, TEXT("URHQueueDataFactory::IsInQueue() returning false, but may not be reliable."));
	return false;
}

// returns true when the local player is in a custom match or the instance server is hosting a custom match
bool URHQueueDataFactory::IsInCustomMatch() const
{
	return CustomMatchSession != nullptr;
}

// returns the current queue match state
ERH_MatchStatus URHQueueDataFactory::GetCurrentQueueMatchState() const
{
	// RHTODO: Support or remove ERH_MatchStatus types: Declined, Accepted, Matching, Waiting, SpectatorLobby, and SpectatorGame
	if (const URH_LocalPlayerSessionSubsystem* SessionSS = GetLocalPlayerSessionSubsystem())
	{
		if (const URH_JoinedSession* pJoinedSession = SessionSS->GetFirstJoinedSessionByType(RHSessionType))
		{
			if (!pJoinedSession->IsBeaconSession())
			{
				if (auto* InstanceData = pJoinedSession->GetInstanceData())
				{
					if (InstanceData->JoinStatus == ERHAPI_InstanceJoinableStatus::Joinable)
					{
						return ERH_MatchStatus::InGame;
					}
				}
			}
		}

		if (SessionSS->GetFirstInvitedSessionByType(RHSessionType))
		{
			return ERH_MatchStatus::Invited;			
		}
	}

	return IsInQueue() ? ERH_MatchStatus::Queued : ERH_MatchStatus::NotQueued;
}

void URHQueueDataFactory::CreateCustomMatchSession()
{
	if (CustomMatchSession)
	{
		UE_LOG(RallyHereStart, Warning, TEXT("URHQueueDataFactory::CreateCustomMatchSession already in a custom match session"));
		return;
	}

	FString RegionId;
	if (!GetPreferredRegionId(RegionId))
	{
		UE_LOG(RallyHereStart, Warning, TEXT("URHQueueDataFactory::CreateCustomMatchSession failed to get preferred site id"));
		return;
	}

	if (URH_LocalPlayerSessionSubsystem* SessionSS = GetLocalPlayerSessionSubsystem())
	{
		FRHAPI_CreateOrJoinRequest Params = {};
		Params.SetSessionType(RHSessionType);
		Params.SetRegionId(RegionId);
		Params.SetClientVersion(URH_JoinedSession::GetClientVersionForSession());

		SessionSS->CreateOrJoinSessionByType(Params, FRH_OnSessionUpdatedDelegate::CreateUObject(this, &URHQueueDataFactory::HandleCustomMatchSessionCreated));
	}
}

void URHQueueDataFactory::HandleCustomMatchSessionCreated(bool bSuccess, URH_SessionView* JoinedSession, const FRH_ErrorInfo& ErrorInfo)
{
	if (bSuccess && JoinedSession != nullptr && JoinedSession->IsA(URH_JoinedSession::StaticClass()))
	{
		CustomMatchSession = Cast<URH_JoinedSession>(JoinedSession);

		// mark the custom session as publicly joinable
		{
			auto UpdateRequest = CustomMatchSession->GetSessionUpdateInfoDefaults();
			UpdateRequest.SetJoinable(true);
			CustomMatchSession->UpdateSessionInfo(UpdateRequest);
		}

		// Set leader's name in session custom data
		UpdateCustomSessionBrowserInfo();

		// invite party members to the session
		if (URHPartyManager* PartyManager = GetPartyManager())
		{
			if (PartyManager->IsInParty() && PartyManager->IsLeader())
			{
				TArray<FRH_PartyMemberData> PartyMembers = PartyManager->GetPartyMembers();
				for (FRH_PartyMemberData PartyMember : PartyMembers)
				{
					if (PartyMember.PlayerData != nullptr)
					{
						FGuid MemberUUID = PartyMember.PlayerData->GetRHPlayerUuid();

						auto HandleInviteSentDelegate = FRH_OnSessionUpdatedDelegate::CreateWeakLambda(this, [this, MemberUUID](bool bSuccess, URH_SessionView* pUpdatedSession, const FRH_ErrorInfo& ErrorInfo)
							{
								if (bSuccess)
								{
									UpdateCustomMatchInfo();
								}
								else
								{
									UE_LOG(RallyHereStart, Warning, TEXT("URHQueueDataFactory::HandleCustomMatchSessionCreated - could not invite party member with UUID %s to custom lobby."), *MemberUUID.ToString());
								}
							});
						CustomMatchSession->InvitePlayer(PartyMember.PlayerData->GetRHPlayerUuid(), 0, TMap<FString, FString>(), HandleInviteSentDelegate);
					}
				}
			}
		}
	}
	else
	{
		UE_LOG(RallyHereStart, Warning, TEXT("URHQueueDataFactory::CreateCustomMatchSession failed to create a session."));
	}
}

void URHQueueDataFactory::LeaveCustomMatchSession()
{
	if (!CustomMatchSession)
	{
		UE_LOG(RallyHereStart, Warning, TEXT("URHQueueDataFactory::LeaveCustomMatchSession not in a valid custom match session to leave"));
		return;
	}

	URHPopupManager* popup = MyHud->GetPopupManager();

	FRHPopupConfig leavePopupParams;
	leavePopupParams.Header = NSLOCTEXT("RHQueue", "LeaveCustomLobby", "LEAVE CUSTOM LOBBY");
	leavePopupParams.Description = NSLOCTEXT("RHQueue", "ConfirmLeaveCustomLobby", "Are you sure you want to leave this Custom Game Lobby?");
	leavePopupParams.IsImportant = true;
	leavePopupParams.CancelAction.AddDynamic(popup, &URHPopupManager::OnPopupCanceled);

	FRHPopupButtonConfig& leaveConfirmBtn = (leavePopupParams.Buttons[leavePopupParams.Buttons.Add(FRHPopupButtonConfig())]);
	leaveConfirmBtn.Label = NSLOCTEXT("General", "Confirm", "Confirm");
	leaveConfirmBtn.Type = ERHPopupButtonType::Confirm;
	leaveConfirmBtn.Action.AddDynamic(this, &URHQueueDataFactory::HandleConfirmLeaveCustomLobby);
	FRHPopupButtonConfig& leaveCancelBtn = (leavePopupParams.Buttons[leavePopupParams.Buttons.Add(FRHPopupButtonConfig())]);
	leaveCancelBtn.Label = NSLOCTEXT("General", "Cancel", "Cancel");
	leaveCancelBtn.Type = ERHPopupButtonType::Cancel;
	leaveCancelBtn.Action.AddDynamic(popup, &URHPopupManager::OnPopupCanceled);

	popup->AddPopup(leavePopupParams);
}

void URHQueueDataFactory::HandleConfirmLeaveCustomLobby()
{
	if (CustomMatchSession)
	{
		if (IsLocalPlayerCustomLobbyLeader())
		{
			FGuid LocalPlayerUUID = MyHud != nullptr ? MyHud->GetLocalPlayerUuid() : FGuid();

			// kick others
			for (FRH_CustomMatchMember LobbyMember : CustomMatchMembers)
			{
				if (LobbyMember.PlayerUUID != LocalPlayerUUID)
				{
					CustomMatchSession->KickPlayer(LobbyMember.PlayerUUID);
				}
			}
		}

		CustomMatchSession->Leave(false, FRH_OnSessionUpdatedDelegate::CreateWeakLambda(this, [this](bool bSuccess, URH_SessionView* pUpdatedSession, const FRH_ErrorInfo& ErrorInfo)
			{
				UpdateCustomMatchInfo();
			}));

		ClearCustomMatchData();
	}
}

bool URHQueueDataFactory::JoinCustomMatchSession(const URH_SessionView* InSession) const
{
	if (InSession)
	{
		const FRH_APISessionWithETag& SessionWrapper = InSession->GetSessionWithETag();
		if (SessionWrapper.Data.Joinable)
		{
			if (auto pLPSessionSubsystem = GetLocalPlayerSessionSubsystem())
			{
				if (pLPSessionSubsystem->GetSessionById(SessionWrapper.Data.SessionId) == nullptr)
				{
					pLPSessionSubsystem->JoinSessionById(SessionWrapper.Data.SessionId);
					return true;
				}
			}
		}
	}
	return false;
}

void URHQueueDataFactory::InviteToCustomMatch(const FGuid& PlayerId, int32 TeamNum)
{
	if (IsCustomInvitePending(PlayerId))
	{
		return;
	}

	if (CustomMatchSession)
	{
		const auto& Teams = CustomMatchSession->GetSessionData().Teams;
		if ((TeamNum < Teams.Num() && Teams[TeamNum].GetPlayers().Num() >= Teams[TeamNum].MaxSize) || TeamNum >= Teams.Num())
		{
			if (URHPopupManager* popupManager = MyHud->GetPopupManager())
			{
				FRHPopupConfig CustomInviteFailedParams;
				CustomInviteFailedParams.Header = NSLOCTEXT("RHCustomGames", "InviteInvalid", "Invite Failed");
				CustomInviteFailedParams.Description = NSLOCTEXT("RHCustomGames", "InviteInvalidDesc", "The team you are inviting the player to is either invalid or full.");
				CustomInviteFailedParams.CancelAction.AddDynamic(popupManager, &URHPopupManager::OnPopupCanceled);

				FRHPopupButtonConfig& OkayBtn = (CustomInviteFailedParams.Buttons[CustomInviteFailedParams.Buttons.AddDefaulted()]);
				OkayBtn.Label = NSLOCTEXT("General", "Ok", "Ok");
				OkayBtn.Type = ERHPopupButtonType::Cancel;
				OkayBtn.Action.AddDynamic(popupManager, &URHPopupManager::OnPopupCanceled);

				popupManager->AddPopup(CustomInviteFailedParams);
			}
			return;
		}

		CustomMatchSession->InvitePlayer(PlayerId, TeamNum, TMap<FString, FString>(), FRH_OnSessionUpdatedDelegate::CreateWeakLambda(this, [this](bool bSuccess, URH_SessionView* pUpdatedSession, const FRH_ErrorInfo& ErrorInfo)
			{
				if (bSuccess)
				{
					UpdateCustomMatchInfo();
				}
			}));
	}
}
void URHQueueDataFactory::KickFromCustomMatch(const FGuid& PlayerId)
{
	if (CustomMatchSession != nullptr && MyHud != nullptr && IsLocalPlayerCustomLobbyLeader() && (IsPlayerInCustomMatch(PlayerId) || IsCustomInvitePending(PlayerId)))
	{
		if (URH_PlayerInfo* PlayerInfo = MyHud->GetOrCreatePlayerInfo(PlayerId))
		{
			PlayerInfo->GetLastKnownDisplayNameAsync(FTimespan(), false, ERHAPI_Platform::Anon,
				FRH_PlayerInfoGetDisplayNameDelegate::CreateWeakLambda(this, [this, PlayerId](bool bSuccess, const FString& DisplayName)
					{
						const FString NameToUse = bSuccess ? DisplayName : PlayerId.ToString();
						if (URHPopupManager* popupManager = MyHud->GetPopupManager())
						{
							PendingConfirmPlayerId = PlayerId;

							FRHPopupConfig KickPopupParams;
							KickPopupParams.Header = NSLOCTEXT("RHCustomGames", "LobbyKick", "Kick from Lobby");
							KickPopupParams.Description = FText::Format(NSLOCTEXT("RHCustomGames", "KickConfirm", "Are you sure you want to kick {0} from the lobby?"), FText::FromString(NameToUse));
							KickPopupParams.IsImportant = true;
							KickPopupParams.CancelAction.AddDynamic(this, &URHQueueDataFactory::HandleCancelKickCustomPlayer);

							FRHPopupButtonConfig& KickConfirmBtn = (KickPopupParams.Buttons[KickPopupParams.Buttons.AddDefaulted()]);
							KickConfirmBtn.Label = NSLOCTEXT("General", "Confirm", "Confirm");
							KickConfirmBtn.Type = ERHPopupButtonType::Confirm;
							KickConfirmBtn.Action.AddDynamic(this, &URHQueueDataFactory::HandleConfirmKickCustomPlayer);

							FRHPopupButtonConfig& KickCancelBtn = (KickPopupParams.Buttons[KickPopupParams.Buttons.AddDefaulted()]);
							KickCancelBtn.Label = NSLOCTEXT("General", "Cancel", "Cancel");
							KickCancelBtn.Type = ERHPopupButtonType::Cancel;
							KickCancelBtn.Action.AddDynamic(this, &URHQueueDataFactory::HandleCancelKickCustomPlayer);

							popupManager->AddPopup(KickPopupParams);
						}
					}));
		}
	}
}

void URHQueueDataFactory::HandleConfirmKickCustomPlayer()
{
	if (CustomMatchSession != nullptr && PendingConfirmPlayerId.IsValid() && (IsPlayerInCustomMatch(PendingConfirmPlayerId) || IsCustomInvitePending(PendingConfirmPlayerId)) && IsLocalPlayerCustomLobbyLeader())
	{
		CustomMatchSession->KickPlayer(PendingConfirmPlayerId);
		PendingConfirmPlayerId.Invalidate();
	}
}

void URHQueueDataFactory::HandleCancelKickCustomPlayer()
{
	PendingConfirmPlayerId.Invalidate();
}

void URHQueueDataFactory::PromoteToCustomMatchHost(const FGuid& PlayerId)
{
	if (CustomMatchSession != nullptr && MyHud != nullptr && IsLocalPlayerCustomLobbyLeader() && IsPlayerInCustomMatch(PlayerId))
	{
		if (URH_PlayerInfo* PlayerInfo = MyHud->GetOrCreatePlayerInfo(PlayerId))
		{
			PlayerInfo->GetLastKnownDisplayNameAsync(FTimespan(), false, ERHAPI_Platform::Anon,
				FRH_PlayerInfoGetDisplayNameDelegate::CreateWeakLambda(this, [this, PlayerId](bool bSuccess, const FString& GamerTag)
					{
						const FString NameToUse = bSuccess ? GamerTag : PlayerId.ToString();
						if (URHPopupManager* popupManager = MyHud->GetPopupManager())
						{
							PendingConfirmPlayerId = PlayerId;

							FRHPopupConfig PromotePopupParams;
							PromotePopupParams.Header = NSLOCTEXT("RHCustomGames", "LobbyHost", "Promote to Lobby Host");
							PromotePopupParams.Description = FText::Format(NSLOCTEXT("RHCustomGames", "PromoteConfirm", "Are you sure you want to promote {0} to lobby host?"), FText::FromString(NameToUse));
							PromotePopupParams.IsImportant = true;
							PromotePopupParams.CancelAction.AddDynamic(this, &URHQueueDataFactory::HandleCancelPromoteCustomPlayer);

							FRHPopupButtonConfig& PromoteConfirmBtn = (PromotePopupParams.Buttons[PromotePopupParams.Buttons.AddDefaulted()]);
							PromoteConfirmBtn.Label = NSLOCTEXT("General", "Confirm", "Confirm");
							PromoteConfirmBtn.Type = ERHPopupButtonType::Confirm;
							PromoteConfirmBtn.Action.AddDynamic(this, &URHQueueDataFactory::HandleConfirmPromoteCustomPlayer);

							FRHPopupButtonConfig& PromoteCancelBtn = (PromotePopupParams.Buttons[PromotePopupParams.Buttons.AddDefaulted()]);
							PromoteCancelBtn.Label = NSLOCTEXT("General", "Cancel", "Cancel");
							PromoteCancelBtn.Type = ERHPopupButtonType::Cancel;
							PromoteCancelBtn.Action.AddDynamic(this, &URHQueueDataFactory::HandleCancelPromoteCustomPlayer);

							popupManager->AddPopup(PromotePopupParams);
						}
					}));
		}
	}
}

void URHQueueDataFactory::HandleConfirmPromoteCustomPlayer()
{
	if (CustomMatchSession != nullptr && PendingConfirmPlayerId.IsValid() && IsPlayerInCustomMatch(PendingConfirmPlayerId) && IsLocalPlayerCustomLobbyLeader())
	{
		CustomMatchSession->SetLeader(PendingConfirmPlayerId);
		PendingConfirmPlayerId.Invalidate();
	}
}

void URHQueueDataFactory::HandleCancelPromoteCustomPlayer()
{
	PendingConfirmPlayerId.Invalidate();
}

bool URHQueueDataFactory::IsCustomInvitePending(const FGuid& PlayerId) const
{
	for (FRH_CustomMatchMember Member : CustomMatchMembers)
	{
		if (Member.PlayerUUID == PlayerId && Member.bIsPendingInvite)
		{
			return true;
		}
	}
	return false;
}

bool URHQueueDataFactory::CanLocalPlayerControlCustomLobbyPlayer(const FGuid& PlayerId) const
{
	bool bIsSelf = false;
	if (MyHud != nullptr)
	{
		bIsSelf = MyHud->GetLocalPlayerUuid() == PlayerId;
	}

	return bIsSelf || IsLocalPlayerCustomLobbyLeader();
}

bool URHQueueDataFactory::CanLocalPlayerPromoteCustomLobbyPlayer(const FGuid& PlayerId) const
{
	return IsLocalPlayerCustomLobbyLeader();
}

bool URHQueueDataFactory::CanLocalPlayerKickCustomLobbyPlayer(const FGuid& PlayerId) const
{
	bool bIsSelf = false;
	if (MyHud != nullptr)
	{
		bIsSelf = MyHud->GetLocalPlayerUuid() == PlayerId;
	}

	// Can't kick self
	return !bIsSelf && IsLocalPlayerCustomLobbyLeader();
}

void URHQueueDataFactory::AcceptMatchInvite()
{
	TArray<URH_InvitedSession*> InvitedSessions = GetSessionInvites();

	auto* InvitedSession = InvitedSessions.Num() > 0 ? InvitedSessions[0] : nullptr;
	if (InvitedSession)
	{
		// Leave the custom match currently in if there is one
		HandleConfirmLeaveCustomLobby();

		LeaveQueue();
		InvitedSession->Join();
	}
}

void URHQueueDataFactory::DeclineMatchInvite()
{
	TArray<URH_InvitedSession*> InvitedSessions = GetSessionInvites();

	auto* InvitedSession = InvitedSessions.Num() > 0 ? InvitedSessions[0] : nullptr;
	if (InvitedSession)
	{
		InvitedSession->Leave();
	}
}

void URHQueueDataFactory::StartCustomMatch(bool bDedicatedInstance /*= false*/)
{
	if (CustomMatchSession)
	{
		// Remove pending invites before starting game
		for (FRH_CustomMatchMember LobbyMember : CustomMatchMembers)
		{
			if (LobbyMember.bIsPendingInvite)
			{
				CustomMatchSession->KickPlayer(LobbyMember.PlayerUUID);
			}
		}

		// Get Map path
		FString MapNameString;
		FRHMapDetails MapDetail;
		if (GetMapDetailsFromRowName(SelectedCustomMatchMapRowName, MapDetail) && !MapDetail.Map.IsNull())
		{
			MapNameString = MapDetail.Map.GetLongPackageName();
		}
		else
		{
			MapNameString = FString(TEXT("/Game/Maps/Internal/TestMap/mymultiplayertest"));
		}

		// Request an instance
		FRHAPI_InstanceStartupParams InstanceStartupParams;
		FString GameModeNameString = FString(TEXT("/Game/RHExampleGameMode"));
		InstanceStartupParams.Map = MapNameString;
		InstanceStartupParams.SetMode(GameModeNameString);

		FRHAPI_InstanceRequest InstanceRequest;
		InstanceRequest.SetInstanceStartupParams(InstanceStartupParams);
		ERHAPI_HostType HostType = bDedicatedInstance ? ERHAPI_HostType::Dedicated : ERHAPI_HostType::Player;
		InstanceRequest.SetHostType(HostType);

		CustomMatchSession->RequestInstance(InstanceRequest, FRH_OnSessionUpdatedDelegate::CreateWeakLambda(this, [this](bool bSuccess, URH_SessionView* pSession, const FRH_ErrorInfo& ErrorInfo)
			{
				auto pJoinedSession = Cast<URH_JoinedSession>(pSession);
				if (bSuccess && pJoinedSession != nullptr)
				{
					if (auto* GISessionSubsystem = GetGameInstanceSessionSubsystem())
					{
						if (GISessionSubsystem->IsReadyToJoinInstance(pJoinedSession, true))
						{
							UE_LOG(RallyHereStart, Warning, TEXT("URHQueueDataFactory::StartCustomMatch - Ready to join instance immediately, starting join"));
							GISessionSubsystem->SyncToSession(pJoinedSession);
						}
						return;
					}
					
					UE_LOG(RallyHereStart, Warning, TEXT("URHQueueDataFactory::StartCustomMatch - Failed to get URH_GameInstanceSessionSubsystem"));
				}
				else
				{
					UE_LOG(RallyHereStart, Warning, TEXT("URHQueueDataFactory::StartCustomMatch - Instance request failed"));
				}
			}));
	}
}

int32 URHQueueDataFactory::GetPlayerTeamId(const FGuid& PlayerId) const
{
	for (FRH_CustomMatchMember Member : CustomMatchMembers)
	{
		if (Member.PlayerUUID == PlayerId)
		{
			return Member.TeamId;
		}
	}

	return -1;
}

int32 URHQueueDataFactory::GetTeamMemberCount(int32 TeamId) const
{
	if (CustomMatchSession)
	{
		TArray<FRHAPI_SessionTeam> Teams = CustomMatchSession->GetSessionData().GetTeams();

		for (int i = 0; i < Teams.Num(); i++)
		{
			if (i == TeamId)
			{
				return Teams[i].GetPlayers().Num();
			}
		}
	}

	return -1;
}

void URHQueueDataFactory::SetPlayerTeamCustomMatch(const FGuid& PlayerId, int32 TeamId)
{
	if (CustomMatchSession)
	{
		CustomMatchSession->ChangePlayerTeam(PlayerId, TeamId, FRH_OnSessionUpdatedDelegate::CreateWeakLambda(this, [this](bool bSuccess, URH_SessionView* pSession, const FRH_ErrorInfo& ErrorInfo)
			{
				if (bSuccess)
				{
					UpdateCustomMatchInfo();
				}
			}));
	}
}

void URHQueueDataFactory::SetMapForCustomMatch(FName MapRowName)
{
	SelectedCustomMatchMapRowName = MapRowName;
	OnCustomMatchMapChanged.Broadcast();
}

bool URHQueueDataFactory::GetMapDetailsFromRowName(FName MapRowName, FRHMapDetails& OutMapDetails) const
{
	if (MapsDetailsDT != nullptr)
	{
		const FRHMapDetails* FindRow = MapsDetailsDT->FindRow<FRHMapDetails>(MapRowName, "URHQueueDataFactory::GetMapDetailsFromRowName");
		if (FindRow)
		{
			OutMapDetails = *FindRow;
			return true;
		}
	}
	return false;
}

bool URHQueueDataFactory::SetSelectedQueueId(const FString& QueueId)
{
	if (!QueueId.IsEmpty())
	{
		if (SelectedQueueId != QueueId)
		{
			SelectedQueueId = QueueId;
			OnSetQueueId.Broadcast();
		}

		return true;
	}

	return false;
}

FString URHQueueDataFactory::GetSelectedQueueId() const
{
	return SelectedQueueId;
}

URH_MatchmakingBrowserCache* URHQueueDataFactory::GetMatchmakingCache() const
{
	const auto pGameInstance = GetWorld()->GetGameInstance();
	if (pGameInstance == nullptr)
	{
		UE_LOG(RallyHereStart, Error, TEXT("[%s] RallyHere failed to get game instance"), ANSI_TO_TCHAR(__FUNCTION__));
		return nullptr;
	}

	const auto pGISubsystem = pGameInstance->GetSubsystem<URH_GameInstanceSubsystem>();
	if (pGISubsystem == nullptr)
	{
		UE_LOG(RallyHereStart, Error, TEXT("[%s] RallyHere failed to get game instance subsystem"), ANSI_TO_TCHAR(__FUNCTION__));
		return nullptr;
	}

	return pGISubsystem->GetMatchmakingCache();
}

URHPartyManager* URHQueueDataFactory::GetPartyManager() const
{
	if (MyHud == nullptr)
	{
		return nullptr;
	}

	if (ARHLobbyHUD* LobbyHUD = Cast<ARHLobbyHUD>(MyHud))
	{
		return LobbyHUD->GetPartyManager();
	}

	return nullptr;
}

URH_GameInstanceSessionSubsystem* URHQueueDataFactory::GetGameInstanceSessionSubsystem() const
{
	if (const auto pGameInstance = GetWorld()->GetGameInstance())
	{
		if (const URH_GameInstanceSubsystem* GISS = pGameInstance->GetSubsystem<URH_GameInstanceSubsystem>())
		{
			return GISS->GetSessionSubsystem();
		}
	}

	return nullptr;
}

URH_LocalPlayerSessionSubsystem* URHQueueDataFactory::GetLocalPlayerSessionSubsystem() const
{
	if (MyHud != nullptr)
	{
		if (URH_LocalPlayerSubsystem* RHSS = MyHud->GetLocalPlayerSubsystem())
		{
			return RHSS->GetSessionSubsystem();
		}
	}
	return nullptr;
}

bool URHQueueDataFactory::IsCustomGameSession(const URH_SessionView* pSession) const
{
	return pSession->GetSessionType() == RHSessionType && !pSession->IsCreatedByMatchmaking();
}

bool URHQueueDataFactory::IsPlayerCustomLobbyLeader(const FGuid& PlayerUUID) const
{
	if (CustomMatchSession)
	{
		if (auto* Player = CustomMatchSession->GetSessionPlayer(PlayerUUID))
		{
			return Player->Status == ERHAPI_SessionPlayerStatus::Leader;
		}
	}
	return false;
}

bool URHQueueDataFactory::IsLocalPlayerCustomLobbyLeader() const
{
	if (URH_LocalPlayerSubsystem* RHSS = MyHud->GetLocalPlayerSubsystem())
	{
		return IsPlayerCustomLobbyLeader(RHSS->GetPlayerUuid());
	}
	return false;
}

bool URHQueueDataFactory::IsPlayerInCustomMatch(const FGuid& PlayerId) const
{
	if (CustomMatchSession == nullptr)
	{
		return false;
	}

	for (FRH_CustomMatchMember Member : CustomMatchMembers)
	{
		if (PlayerId == Member.PlayerUUID && !Member.bIsPendingInvite)
		{
			return true;
		}
	}
	return false;
}

void URHQueueDataFactory::DoSearchForCustomGames()
{
	if (MyHud != nullptr)
	{
		if (URH_LocalPlayerSubsystem* RHSS = MyHud->GetLocalPlayerSubsystem())
		{
			if (URH_LocalPlayerSessionSubsystem* pLPSessionSubsystem = RHSS->GetSessionSubsystem())
			{
				FRH_SessionBrowserSearchParams params;
				params.SessionType = RHSessionType;
				params.bCacheSessionDetails = true;
				pLPSessionSubsystem->SearchForSessions(params, FRH_OnSessionSearchCompleteDelegate::CreateUObject(this, &URHQueueDataFactory::HandleCustomSessionSearchResult));
			}
		}
	}
}

void URHQueueDataFactory::HandleCustomSessionSearchResult(bool bSuccess, const FRH_SessionBrowserSearchResult& Result)
{
	if (bSuccess)
	{
		TArray<URH_SessionView*> CustomSessions;
		for (TWeakObjectPtr<URH_SessionView> Session : Result.Sessions)
		{
			if (Session.IsValid() && Session->GetSessionPlayerCount() > 0)
			{
				CustomSessions.Add(Session.Get());
			}
		}
		OnCustomSearchResultReceived.Broadcast(CustomSessions);
	}
}

bool URHQueueDataFactory::GetBrowserSessionsLeaderName(URH_SessionView* InSession, FString& OutLeaderName)
{
	if (InSession == nullptr)
	{
		return false;
	}

	TMap<FString, FString> BrowserData = InSession->GetBrowserCustomData();
	if (BrowserData.Contains(SessionLeaderNameFieldName))
	{
		OutLeaderName = BrowserData[SessionLeaderNameFieldName];
		return true;
	}

	return false;
}

void URHQueueDataFactory::PromptMatchRejoin(FString SessionId)
{
	if (URHPopupManager* PopupManager = MyHud->GetPopupManager())
	{
		PendingSessionId = SessionId;
							
		FRHPopupConfig PopupParams;
		PopupParams.Header = NSLOCTEXT("RHJoinMatch", "RejoinMatchTitle", "EXISTING MATCH FOUND");
		PopupParams.Description = NSLOCTEXT("RHJoinMatch", "RejoinMatch", "Your previous match appears to still be in progress. Would you like to rejoin it?");
		PopupParams.IsImportant = true;
		PopupParams.CancelAction.AddDynamic(this, &URHQueueDataFactory::DeclineMatchRejoin);

		FRHPopupButtonConfig& confirmBtn = (PopupParams.Buttons[PopupParams.Buttons.Add(FRHPopupButtonConfig())]);
		confirmBtn.Label = NSLOCTEXT("General", "Okay", "Okay");
		confirmBtn.Type = ERHPopupButtonType::Confirm;
		confirmBtn.Action.AddDynamic(this, &URHQueueDataFactory::AcceptMatchRejoin);
		FRHPopupButtonConfig& cancelBtn = (PopupParams.Buttons[PopupParams.Buttons.Add(FRHPopupButtonConfig())]);
		cancelBtn.Label = NSLOCTEXT("General", "Cancel", "Cancel");
		cancelBtn.Type = ERHPopupButtonType::Cancel;
		cancelBtn.Action.AddDynamic(this, &URHQueueDataFactory::DeclineMatchRejoin);

		PopupManager->AddPopup(PopupParams);
	}
}

void URHQueueDataFactory::AcceptMatchRejoin()
{
	if (const URH_LocalPlayerSessionSubsystem* SessionSS = GetLocalPlayerSessionSubsystem())
	{
		if (URH_JoinedSession* pJoinedSession = Cast<URH_JoinedSession>(SessionSS->GetSessionById(PendingSessionId)))
		{
			if (auto* GISessionSubsystem = GetGameInstanceSessionSubsystem())
			{
				GISessionSubsystem->SyncToSession(pJoinedSession);
			}
		}
	}
}

void URHQueueDataFactory::DeclineMatchRejoin()
{
	LeaveMatch();
}

void URHQueueDataFactory::HandleLoginPollSessionsComplete(bool bSuccess)
{
	if (bSuccess)
	{
		if (const URH_LocalPlayerSessionSubsystem* SessionSS = GetLocalPlayerSessionSubsystem())
		{
			TArray<URH_JoinedSession*> JoinedSessions = SessionSS->GetJoinedSessionsByType(RHSessionType);
			for (const URH_JoinedSession* pJoinedSession : JoinedSessions)
			{
				if (!pJoinedSession->IsBeaconSession())
				{
					if (auto* InstanceData = pJoinedSession->GetInstanceData())
					{
						if (InstanceData->JoinStatus == ERHAPI_InstanceJoinableStatus::Joinable)
						{
							PromptMatchRejoin(pJoinedSession->GetSessionId());
							break;
						}
					}
				}
			}
		}
	}

	LastLoginPlayerGuid = MyHud != nullptr ? MyHud->GetLocalPlayerUuid() : FGuid();
}

void URHQueueDataFactory::HandleSessionAdded(URH_SessionView* pSession)
{
	if (pSession->GetSessionType() == RHSessionType)
	{
		if (URH_InvitedSession* pInvitedSession = Cast<URH_InvitedSession>(pSession))
		{
			if (IsCustomGameSession(pInvitedSession))
			{
				// Got invited
				ProcessCustomGameInvite(pInvitedSession);
			}
			else
			{
				pInvitedSession->Join();
			}
		}
		else if (URH_JoinedSession* pJoinedSession = Cast<URH_JoinedSession>(pSession))
		{
			if (MyHud != nullptr && LastLoginPlayerGuid != MyHud->GetLocalPlayerUuid())
			{
				return;
			}
			
			if (IsCustomGameSession(pJoinedSession))
			{
				// Joined a session
				CustomMatchSession = pJoinedSession;
				UpdateCustomMatchInfo();
				OnCustomMatchJoined.Broadcast();
			}
			else
			{
				if (auto* GISessionSubsystem = GetGameInstanceSessionSubsystem())
				{
					GISessionSubsystem->SyncToSession(pJoinedSession);
				}
			}
		}
	}
}

void URHQueueDataFactory::HandleSessionRemoved(URH_SessionView* pSession)
{
	if (pSession->GetSessionType() == RHSessionType)
	{
		if (pSession->IsA(URH_JoinedSession::StaticClass()) && pSession == CustomMatchSession)
		{
			// Left the current session
			CustomMatchSession = nullptr;
			OnCustomMatchLeft.Broadcast();
		}

		SendMatchStatusUpdateNotify(GetCurrentQueueMatchState());
	}
}

void URHQueueDataFactory::HandleSessionUpdated(URH_SessionView* pSession)
{
	if (CustomMatchSession == pSession)
	{
		// Update lobby data
		UpdateCustomMatchInfo();

		if (URH_GameInstanceSessionSubsystem* GISS = GetGameInstanceSessionSubsystem())
		{
			// if we are ready to join and not already active, start join
			if (GISS->IsReadyToJoinInstance(CustomMatchSession))
			{
				UE_LOG(RallyHereStart, Warning, TEXT("URHQueueDataFactory::HandleSessionUpdated - starting instance join"));
				GISS->SyncToSession(CustomMatchSession);
			}
		}
	}
}

TArray<URH_InvitedSession*> URHQueueDataFactory::GetSessionInvites() const
{
	TArray<URH_InvitedSession*> Invites;
	if (const URH_LocalPlayerSessionSubsystem* SessionSS = GetLocalPlayerSessionSubsystem())
	{
		Invites = SessionSS->GetInvitedSessionsByType(RHSessionType);
	}
	return Invites;
}

void URHQueueDataFactory::ProcessCustomGameInvite(URH_InvitedSession* pInvitedSession)
{
	for (URH_InvitedSession* Invite : GetSessionInvites())
	{
		// Still have a pending invite for another custom match so auto-decline this one
		if (Invite->GetSessionId() != pInvitedSession->GetSessionId())
		{
			UE_LOG(RallyHereStart, Warning, TEXT("URHQueueDataFactory::ProcessCustomGameInvite - there's already a pending custom game invite, auto-declining incoming one"));
			pInvitedSession->Leave();
			return;
		}
	}

	const FGuid LocalPlayerUuid = MyHud->GetLocalPlayerUuid();
	if (LocalPlayerUuid.IsValid())
	{
		const FRHAPI_SessionPlayer* pSessionPlayer = pInvitedSession->GetSessionPlayer(LocalPlayerUuid);
		if (pSessionPlayer != nullptr)
		{
			const FGuid InviterId = pSessionPlayer->InvitingPlayerUuid_IsSet ? pSessionPlayer->InvitingPlayerUuid_Optional : FGuid();

			// If this invite is from the party leader then auto accept
			if (URHPartyManager* PartyManager = GetPartyManager())
			{
				if (PartyManager->IsInParty() && !PartyManager->IsLeader())
				{
					FRHAPI_SessionPlayer PartyLeader;
					PartyManager->GetPartyLeader(PartyLeader);
					if (InviterId == PartyLeader.GetPlayerUuid())
					{
						pInvitedSession->Join();
						return;
					}
				}
			}

			if (URH_PlayerInfo* Inviter = MyHud->GetOrCreatePlayerInfo(InviterId))
			{
				Inviter->GetLastKnownDisplayNameAsync(FTimespan(), false, ERHAPI_Platform::Anon, FRH_PlayerInfoGetDisplayNameDelegate::CreateWeakLambda(this, [this, InviterId](bool bSuccess, const FString& DisplayName)
					{
						const FString NameToUse = bSuccess ? DisplayName : InviterId.ToString();
						if (URHPopupManager* popupManager = MyHud->GetPopupManager())
						{
							FRHPopupConfig popupParams;
							popupParams.Header = NSLOCTEXT("RHPartyInvite", "Pending", "INVITE PENDING");
							popupParams.Description = FText::Format(NSLOCTEXT("RHJoinMatch", "CustomInvite", "{0} has invited you to their custom match!"), FText::FromString(NameToUse));
							popupParams.IsImportant = true;
							popupParams.CancelAction.AddDynamic(this, &URHQueueDataFactory::DeclineMatchInvite);

							FRHPopupButtonConfig& confirmBtn = (popupParams.Buttons[popupParams.Buttons.Add(FRHPopupButtonConfig())]);
							confirmBtn.Label = NSLOCTEXT("General", "Confirm", "Confirm");
							confirmBtn.Type = ERHPopupButtonType::Confirm;
							confirmBtn.Action.AddDynamic(this, &URHQueueDataFactory::AcceptMatchInvite);
							FRHPopupButtonConfig& cancelBtn = (popupParams.Buttons[popupParams.Buttons.Add(FRHPopupButtonConfig())]);
							cancelBtn.Label = NSLOCTEXT("General", "Cancel", "Cancel");
							cancelBtn.Type = ERHPopupButtonType::Cancel;
							cancelBtn.Action.AddDynamic(this, &URHQueueDataFactory::DeclineMatchInvite);

							popupManager->AddPopup(popupParams);
						}
					}));
				return;
			}
		}
	}

	// If fall through then decline
	pInvitedSession->Leave();
}