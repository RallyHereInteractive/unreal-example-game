#include "RallyHereStart.h"
#include "Lobby/HUD/RHLobbyHUD.h"
#include "RH_LocalPlayerSessionSubsystem.h"
#include "RH_LocalPlayerSubsystem.h"
#include "RH_LocalPlayerLoginSubsystem.h"
#include "DataFactories/RHQueueDataFactory.h"
#include "Shared/HUD/RHHUDCommon.h"
#include "Managers/RHPopupManager.h"
#include "Managers/RHPartyManager.h"

URHPartyManager::URHPartyManager(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PartyMaxed = false;
	IsPendingLeave = false;
	IsKicked = false;
	MaxPartySize = 4;
	CurrentInviteMode = ERH_PartyInviteRightsMode::ERH_PIRM_AllMembers;
	RHSessionType = FString(TEXT("party"));
}

void URHPartyManager::Initialize(class ARHHUDCommon* InHud)
{
	MyHud = InHud;

	if (auto RHSS = GetPlayerSessionSubsystem())
	{
		RHSS->OnSessionUpdatedDelegate.AddUObject(this, &URHPartyManager::HandleSessionUpdated);
		RHSS->OnSessionAddedDelegate.AddUObject(this, &URHPartyManager::HandleSessionAdded);
		RHSS->OnSessionRemovedDelegate.AddUObject(this, &URHPartyManager::HandleSessionRemoved);
		RHSS->OnLoginPollSessionsCompleteDelegate.AddUObject(this, &URHPartyManager::HandleLoginPollSessionsComplete);
		RHSS->OnFailedToJoinPlatformSessionDelegate.AddUObject(this, &URHPartyManager::HandleFailedToJoinPlatformSession);
	}
	if (auto RHFS = GetFriendSubsystem())
	{
		RHFS->FriendUpdatedDelegate.AddUObject(this, &URHPartyManager::HandleFriendUpdate);
	}

	if (MyHud != nullptr)
	{
		MyHud->OnPreferredRegionUpdated.AddDynamic(this, &URHPartyManager::HandlePreferredRegionUpdated);
	}

	// bootstrap where we're initializing after login
	PostLogin();
}

void URHPartyManager::Uninitialize()
{
	if (auto RHSS = GetPlayerSessionSubsystem())
	{
		RHSS->OnSessionUpdatedDelegate.RemoveAll(this);
		RHSS->OnSessionAddedDelegate.RemoveAll(this);
		RHSS->OnSessionRemovedDelegate.RemoveAll(this);
		RHSS->OnLoginPollSessionsCompleteDelegate.RemoveAll(this);
	}
	if (auto RHFS = GetFriendSubsystem())
	{
		RHFS->FriendUpdatedDelegate.RemoveAll(this);
	}

	ForcePartyCleanUp();
}

void URHPartyManager::PostLogin()
{
	PartySession = nullptr;
	PartyMaxed = false;
	IsPendingLeave = false;
	IsKicked = false;
	PartyMembers.Empty();

	if (auto RHSS = GetPlayerSessionSubsystem())
	{
		RHSS->PollForUpdate(FRH_PollCompleteFunc::CreateWeakLambda(this, [this, RHSS](bool bSuccess, bool bResetTimer)
		{
			if (URH_JoinedSession* pSession = RHSS->GetFirstJoinedSessionByType(RHSessionType))
			{
				UpdateParty(pSession);
			}
		}));
	}
}

void URHPartyManager::PostLogoff()
{
	ForcePartyCleanUp(true);

	PartySession = nullptr;
	PartyMaxed = false;
	IsPendingLeave = false;
	IsKicked = false;

	PartyMembers.Empty();

	LastLoginPlayerGuid.Invalidate();
}

void URHPartyManager::HandleFriendUpdate(URH_RHFriendAndPlatformFriend* Friend)
{
	if (!UpdatePartyTimerHandle.IsValid() && MyHud != nullptr)
	{
		UpdatePartyTimerHandle = MyHud->GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]() { UpdateParty(PartySession); UpdatePartyTimerHandle.Invalidate(); }));
	}
}

void URHPartyManager::HandleLoginPollSessionsComplete(bool bSuccess)
{
	if (bSuccess)
	{
		UpdatePartyFromSubsystem();
	}

	LastLoginPlayerGuid = MyHud != nullptr ? MyHud->GetLocalPlayerUuid() : FGuid();
}

void URHPartyManager::HandleFailedToJoinPlatformSession(const FRH_ErrorInfo& ErrorInfo)
{
	const TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(ErrorInfo.ResponseContent);
	TSharedPtr<FJsonObject> JsonObject;
	if (FJsonSerializer::Deserialize(JsonReader, JsonObject))
	{
		FText InvitationErrorMessage = NSLOCTEXT("GeneralParty", "PortalPartyError", "An error occurred when attempting to join the party.");

		const FString ErrorCodeProperty = TEXT("error_code");
		const FString TeamFullErrorCode = TEXT("team_full");
		for (auto ValuesIt = JsonObject->Values.CreateIterator(); ValuesIt; ++ValuesIt)
		{
			if (ValuesIt->Key.Equals(ErrorCodeProperty, ESearchCase::IgnoreCase))
			{
				FString ErrorCode;
				if (ValuesIt->Value->TryGetString(ErrorCode))
				{
					if (ErrorCode.Equals(TeamFullErrorCode, ESearchCase::IgnoreCase))
					{
						InvitationErrorMessage = NSLOCTEXT("GeneralParty", "PortalPartyFull", "The party you are trying to join is currently full.");
					}
				}
			}
		}

		if (MyHud != nullptr)
		{
			if (auto* PopupManager = MyHud->GetPopupManager())
			{
				FRHPopupConfig PopupParams;
				PopupParams.Header = NSLOCTEXT("GeneralParty", "PortalSessionUpdate", "Party Session");
				PopupParams.Description = InvitationErrorMessage;
				PopupParams.IsImportant = true;
				PopupParams.CancelAction.AddDynamic(PopupManager, &URHPopupManager::OnPopupCanceled);

				auto& ConfirmBtn = (PopupParams.Buttons[PopupParams.Buttons.AddDefaulted()]);
				ConfirmBtn.Label = NSLOCTEXT("General", "Confirm", "Confirm");
				ConfirmBtn.Type = ERHPopupButtonType::Confirm;
				ConfirmBtn.Action.AddDynamic(PopupManager, &URHPopupManager::OnPopupCanceled);

				PopupManager->AddPopup(PopupParams);
			}
		}
	}
}

void URHPartyManager::UpdatePartyFromSubsystem()
{
	URH_JoinedSession* pSession = nullptr;
	if (URH_LocalPlayerSessionSubsystem* RHSS = GetPlayerSessionSubsystem())
	{
		pSession = RHSS->GetFirstJoinedSessionByType(RHSessionType);

		if (!pSession)
		{
			CreateSoloParty();
			return;
		}
	}

	UpdateParty(pSession);
}

void URHPartyManager::HandleSessionCreated(bool bSuccess, URH_SessionView* JoinedSession, const FRH_ErrorInfo& ErrorInfo)
{
	if (bSuccess)
	{
		UpdateParty(Cast<URH_JoinedSession>(JoinedSession));
	}
}

void URHPartyManager::UpdateParty(URH_JoinedSession* pSession)
{
	if (pSession != PartySession && pSession != nullptr && PartySession != nullptr)
	{
		UE_LOG(RallyHereStart, Log, TEXT("URHPartyManager::UpdateParty Going from one party session type to another - joined into multiple parties?"));
	}

	bool bNeedsToLeaveQueue = false;

	// Is the local player a member?
	const auto OldParty = PartySession;
	const bool bHadOldParty = OldParty != nullptr;
	PartySession = pSession;
	const bool bHasParty = PartySession != nullptr;

	if (bHadOldParty && !bHasParty)
	{
		UE_LOG(RallyHereStart, Log, TEXT("URHPartyManager::UpdateParty Going from IN-PARTY to NOT-IN-PARTY. Clearing Party info."));
		PartyMembers.Empty();
		PartyMaxed = false;
		bNeedsToLeaveQueue = true;

		// disable player watching, as we no longer care about the party members
		OldParty->SetWatchingPlayers(false);

		// leave the party if still in our owner object (this call can be due to it being removed, so do not leave twice)
		if (OldParty->GetSessionOwner() != nullptr && OldParty->GetSessionOwner()->GetSessionById(OldParty->GetSessionId()) != nullptr)
		{
			OldParty->Leave(false, "LeavingStaleParty");
		}

		OnPartyDataUpdated.Broadcast();

		// This might not be exactly what we want to do... we should either mark it as "left", "kicked", or "disbanded"
		if (IsPendingLeave)
		{
			IsPendingLeave = false;
			UE_LOG(RallyHereStart, Log, TEXT("URHPartyManager::UpdateParty Calling Handle Team leave for local player."));
			OnPartyLocalPlayerLeft.Broadcast();
		}
		else if (!IsKicked)
		{
			// We've left the party, but we weren't pending leave. The party must have been disbanded via someone else leaving.
			OnPartyDisbanded.Broadcast();
		}
		else
		{
			// Inform player that they were kicked through popup
			UIX_PlayerKickedFromParty();

			IsKicked = false;
		}
		
		CreateSoloParty();
	}
	else if (bHasParty && OldParty != PartySession)
	{
		UE_LOG(RallyHereStart, Log, TEXT("URHPartyManager::HandleJoinParty Joining into new party."));
		PartyMembers = GetNewPartyMemberData();

		// add player watching, so we get presence updates for our party
		if (OldParty != nullptr)
		{
			OldParty->SetWatchingPlayers(false);
			OldParty->Leave(false, "LeavingOldParty");
		}
		PartySession->SetWatchingPlayers(true);

		// reset pending states since we're in a party now
		IsPendingLeave = false;

		for (FRH_PartyMemberData PartyMember : PartyMembers)
		{
			OnNewPartyMemberAdded(&PartyMember);
		}

		UE_LOG(RallyHereStart, Log, TEXT("URHPartyManager::HandleJoinParty Broadcasting generic party update."));
		OnPartyDataUpdated.Broadcast();
		bNeedsToLeaveQueue = true;
	}
	else if (bHasParty && bHadOldParty && OldParty == PartySession)
	{
		UE_LOG(RallyHereStart, Verbose, TEXT("URHPartyManager::UpdateParty Receiving an update to the current party. "));

		auto& SessionData = PartySession->GetSessionData();

		TArray<FRH_PartyMemberData> CachedPartyMembers = PartyMembers;
		for (auto& CachedPartyMember : CachedPartyMembers)
		{
			const auto* RHPartyMember = pSession->GetSessionPlayer(CachedPartyMember.PlayerData->GetRHPlayerUuid());

			if (RHPartyMember == nullptr)
			{
				UE_LOG(RallyHereStart, Log, TEXT("URHPartyManager::UpdateParty removing party member with PlayerId: %s."), *CachedPartyMember.PlayerData->GetRHPlayerUuid().ToString());
				if (RemovePartyMemberById(CachedPartyMember.PlayerData->GetRHPlayerUuid()))
				{
					UE_LOG(RallyHereStart, Log, TEXT("URHPartyManager::UpdateParty Broadcasting party member removed for PlayerId: %s."), *CachedPartyMember.PlayerData->GetRHPlayerUuid().ToString());
					OnPartyMemberLeft.Broadcast(CachedPartyMember);
					bNeedsToLeaveQueue = true;
				}
				else
				{
					UE_LOG(RallyHereStart, Warning, TEXT("URHPartyManager::UpdateParty problem removing player with PlayerId: %s."), *CachedPartyMember.PlayerData->GetRHPlayerUuid().ToString());
				}
			}
			else
			{
				FRH_PartyMemberData RefreshedPartyMemberData;
				PopulatePartyMemberData(RHPartyMember, RefreshedPartyMemberData);
				if (!(CachedPartyMember == RefreshedPartyMemberData))
				{
					const bool bPendingMemberAccepted = CachedPartyMember.IsPending && !RefreshedPartyMemberData.IsPending;
					const bool bUpdated = UpdatePartyMember(RefreshedPartyMemberData);

					if (bPendingMemberAccepted)
					{
						UE_LOG(RallyHereStart, Log, TEXT("URHPartyManager::UpdateParty Broadcasting pending party member accepted for PlayerId: %s."), *RefreshedPartyMemberData.PlayerData->GetRHPlayerUuid().ToString());
						OnPendingPartyMemberAccepted.Broadcast(RefreshedPartyMemberData);
						bNeedsToLeaveQueue = true;
					}

					if (bUpdated)
					{
						// signal update of this party member
						UE_LOG(RallyHereStart, Log, TEXT("URHPartyManager::UpdateParty Broadcasting party member updated for PlayerId: %s."), *RefreshedPartyMemberData.PlayerData->GetRHPlayerUuid().ToString());
						OnPartyMemberDataUpdated.Broadcast(RefreshedPartyMemberData);
					}
					else
					{
						UE_LOG(RallyHereStart, Warning, TEXT("URHPartyManager::UpdateParty problem updating player with PlayerId: %s."), *RefreshedPartyMemberData.PlayerData->GetRHPlayerUuid().ToString());
					}

				}
			}
		}

		URH_LocalPlayerSubsystem* LPSS = GetLocalPlayerSubsystem();
		URH_FriendSubsystem* RHFS = GetFriendSubsystem();
		
		// Pick up any new pending members
		{
			for (const auto& SessionTeam : SessionData.Teams)
			{
				for (const auto& SessionPlayer : SessionTeam.Players)
				{
					if (!IsPlayerInParty(SessionPlayer.PlayerUuid))
					{
						FRH_PartyMemberData NewPartyMemberData;
						if (PopulatePartyMemberData(&SessionPlayer, NewPartyMemberData))
						{
							if (LPSS != nullptr && RHFS != nullptr)
							{
								RHFS->UpdateRecentPlayerForOSS(LPSS, SessionPlayer.PlayerUuid);
							}

							PartyMembers.Add(NewPartyMemberData);

							UE_LOG(RallyHereStart, Log, TEXT("URHPartyManager::UpdateParty Broadcasting update for new pending party member with PlayerId: %s."), *NewPartyMemberData.PlayerData->GetRHPlayerUuid().ToString());
							OnNewPartyMemberAdded(&NewPartyMemberData);
							OnPendingPartyMemberDataAdded.Broadcast(NewPartyMemberData);
							bNeedsToLeaveQueue = true;
						}
					}
				}
			}
		}

		IsPendingLeave = false;
	}
	else
	{
		UE_LOG(RallyHereStart, Verbose, TEXT("URHPartyManager::UpdateParty NOT-IN-PARTY and nothing to update."));
	}

	PartyMaxed = PartyMembers.Num() >= GetMaxPartyMembers();

	if (bNeedsToLeaveQueue)
	{
		LeaveQueue();
	}
}

void URHPartyManager::UpdatePartyInvites(TArray<URH_InvitedSession*> pSessions)
{
	if (MyHud == nullptr)
	{
		return;
	}

	FGuid LocalPlayerUuid = MyHud->GetLocalPlayerUuid();

	if (!LocalPlayerUuid.IsValid())
	{
		return;
	}

	bool bCommunicationEnabled = false;
	bool bCrossplayEnabled = false;
	if (URH_LocalPlayerSubsystem* LPSS = GetLocalPlayerSubsystem())
	{
		if (URH_LocalPlayerLoginSubsystem* LoginSS = LPSS->GetLoginSubsystem())
		{
			bCommunicationEnabled = LoginSS->IsCommunicationEnabled();
			bCrossplayEnabled = LoginSS->IsCrossplayEnabled();
		}
	}

	for (URH_InvitedSession* pSession : pSessions)
	{
		const FRHAPI_SessionPlayer* pSessionPlayer = pSession->GetSessionPlayer(LocalPlayerUuid);

		if (pSessionPlayer != nullptr)
		{
			const FGuid InviterId = pSessionPlayer->GetInvitingPlayerUuid(FGuid());

			// auto decline if communications are disabled
			if (!bCommunicationEnabled)
			{
				pSession->Leave("DeclineInviteCommDisabled");
				return;
			}

			//auto decline if blocked in RH
			if (InviterId.IsValid())
			{
				if (auto RHFS = GetFriendSubsystem())
				{
					if (RHFS->IsPlayerBlocked(InviterId))
					{
						pSession->Leave("DeclineInviteFromBlocked");
						continue;
					}
				}

				auto* Inviter = MyHud->GetOrCreatePlayerInfo(InviterId);
				if (Inviter != nullptr)
				{
					Inviter->GetLinkedPlatformInfo(FTimespan::FromMinutes(1), true, FRH_PlayerInfoGetPlatformsDelegate::CreateWeakLambda(this, [this, pSession, Inviter, bCrossplayEnabled](bool bSucess, const TArray<URH_PlayerPlatformInfo*>& PlatformInfos) {
						if (Inviter == nullptr)
						{
							return;
						}

						// auto decline if crossplay is disabled (on Xbox) and inviter is from another platform
						// #RHTODO - Crossplay - need to compare local player's AuthContext with remote player's platform info from their presence
						if (!bCrossplayEnabled)
						{
							auto* LocalPlayerInfo = MyHud->GetLocalPlayerInfo();
							if (LocalPlayerInfo != nullptr)
							{
								FAuthContextPtr LocalPlayerAuthContext = LocalPlayerInfo->GetAuthContext();
								FAuthContextPtr InviterAuthContext = Inviter->GetAuthContext();
								if (LocalPlayerAuthContext.IsValid() && InviterAuthContext.IsValid()
									&& LocalPlayerAuthContext->GetLoginResult().GetValue().GetPortalId() == InviterAuthContext->GetLoginResult().GetValue().GetPortalId())
								{
									pSession->Leave("AutoDeclineDueToCrossplay");

									// Get display name async for log
									Inviter->GetLastKnownDisplayNameAsync(FTimespan(), false, ERHAPI_Platform::Anon, FRH_PlayerInfoGetDisplayNameDelegate::CreateWeakLambda(this, [this, Inviter](bool bSuccess, const FString& DisplayName)
										{
											UE_LOG(RallyHereStart, Log, TEXT("URHPartyManager::UpdatePartyInvites Auto-declining party invite from %s because crossplay is disabled."), *(bSuccess ? DisplayName : Inviter->GetRHPlayerUuid().ToString()));
										}), MyHud->GetLocalPlayerSubsystem());
								}
							}
						}

						PartyInviter = Inviter;

						Inviter->GetLastKnownDisplayNameAsync(FTimespan(), false, ERHAPI_Platform::Anon, FRH_PlayerInfoGetDisplayNameDelegate::CreateWeakLambda(this, [this, Inviter](bool bSuccess, const FString& DisplayName)
							{
								UE_LOG(RallyHereStart, Log, TEXT("URHPartyManager::UpdatePartyInvites Broadcasting party invite from %s."), *(bSuccess ? DisplayName : Inviter->GetRHPlayerUuid().ToString()));
								OnPartyInvitationReceived.Broadcast(Inviter);
							}), MyHud->GetLocalPlayerSubsystem());
						}));
				}
				else
				{
					PartyInviter = nullptr;
					OnPartyInvitationReceived.Broadcast(nullptr);
				}
			}
			else
			{
				PartyInviter = nullptr;
				OnPartyInvitationReceived.Broadcast(nullptr);
			}
		}

		break;
	}
}

void URHPartyManager::HandleSessionUpdated(URH_SessionView* pSession)
{
	if (pSession->GetSessionType() == RHSessionType)
	{
		if (pSession->IsA(URH_InvitedSession::StaticClass()))
		{
			UpdatePartyInvites(GetSessionInvites());
		}
		else if (pSession->IsA(URH_JoinedSession::StaticClass()))
		{
			UpdateParty(Cast<URH_JoinedSession>(pSession));
		}

		// If an update happened that reduced our party to 0, send out an update for us the remaining player.
		for (auto& Team : pSession->GetSessionData().Teams)
		{
			if (Team.Players.Num() == 1)
			{
				for (auto& Player : Team.Players)
				{
					OnPartyMemberStatusChanged.Broadcast(Player.PlayerUuid);
				}
			}
		}
	}
}

void URHPartyManager::HandleSessionAdded(URH_SessionView* pSession)
{
	if (pSession->GetSessionType() == RHSessionType)
	{
		if (pSession->IsA(URH_InvitedSession::StaticClass()))
		{
			UpdatePartyInvites(GetSessionInvites());
		}
		else if (pSession->IsA(URH_JoinedSession::StaticClass()))
		{
			// Broadcast all members of party changed, then listen for future updates
			for (auto& Team : pSession->GetSessionData().Teams)
			{
				for (auto& Player : Team.Players)
				{
					OnPartyMemberStatusChanged.Broadcast(Player.PlayerUuid);
				}
			}

			pSession->OnSessionMemberStateChangedDelegate.AddUObject(this, &URHPartyManager::HandlePartyMemberStateChanged);

			UpdateParty(Cast<URH_JoinedSession>(pSession));
		}
	}
}
void URHPartyManager::HandleSessionRemoved(URH_SessionView* pSession)
{
	if (pSession->GetSessionType() == RHSessionType)
	{
		if (pSession->IsA(URH_InvitedSession::StaticClass()))
		{
			UpdatePartyInvites(GetSessionInvites());
		}
		else if (pSession->IsA(URH_JoinedSession::StaticClass()) && pSession == PartySession)
		{
			UpdateParty(nullptr);
		}

		// Broadcast all members of party changed, as we will no longer be listening for change updates
		for (auto& Team : pSession->GetSessionData().Teams)
		{
			for (auto& Player : Team.Players)
			{
				OnPartyMemberStatusChanged.Broadcast(Player.PlayerUuid);
			}
		}

		pSession->OnSessionMemberStateChangedDelegate.RemoveAll(this);
	}
}

void URHPartyManager::HandlePartyMemberStateChanged(URH_SessionView* UpdatedSession, const FRH_SessionMemberStatusState& OldStatus, const FRH_SessionMemberStatusState& NewStatus)
{
	// For now always broadcast the updates no matter what ended up changing
	OnPartyMemberStatusChanged.Broadcast(NewStatus.PlayerUuid);
}

TArray<URH_InvitedSession*> URHPartyManager::GetSessionInvites() const
{
	TArray<URH_InvitedSession*> Invites;

	auto* RHSS = GetPlayerSessionSubsystem();
	if (RHSS != nullptr)
	{
		Invites = RHSS->GetInvitedSessionsByType(RHSessionType);
	}

	return Invites;
}

bool URHPartyManager::UpdatePartyMember(FRH_PartyMemberData& PartyMemberData)
{
	for (auto& PartyMember : PartyMembers)
	{
		// compares using player id
		if (PartyMember.PlayerData->GetRHPlayerUuid() == PartyMemberData.PlayerData->GetRHPlayerUuid())
		{
			PartyMember = PartyMemberData;
			return true;
		}
	}
	return false;
}

bool URHPartyManager::RemovePartyMemberById(const FGuid& PlayerId)
{
	bool bReturnValue = false;
	for (int32 i = PartyMembers.Num() - 1; i >= 0; i--)
	{
		const FRH_PartyMemberData& PartyMember = PartyMembers[i];
		if (PartyMember.PlayerData->GetRHPlayerUuid() == PlayerId)
		{
			bReturnValue = true;
			PartyMembers.RemoveAtSwap(i);
		}
	}
	return bReturnValue;
}

bool URHPartyManager::PopulatePartyMemberData(const struct FRHAPI_SessionPlayer* RHPartyMember, FRH_PartyMemberData& PartyMemberData)
{
	if (RHPartyMember == nullptr || MyHud == nullptr) return false;

	if (PartySession)
	{
		PartyMemberData.PlayerData = MyHud->GetOrCreatePlayerInfo(RHPartyMember->PlayerUuid);
		PartyMemberData.IsLeader = RHPartyMember->Status == ERHAPI_SessionPlayerStatus::Leader;
		PartyMemberData.IsPending = RHPartyMember->Status == ERHAPI_SessionPlayerStatus::Invited;
		if (PartyMemberData.PlayerData->GetPresence() != nullptr)
		{
			auto Status = PartyMemberData.PlayerData->GetPresence()->Status;
			PartyMemberData.Online = Status == ERHAPI_OnlineStatus::Online || Status == ERHAPI_OnlineStatus::Away;
		}
		else
		{
			PartyMemberData.Online = true;
		}

		PartyMemberData.CanInvite = PartyMemberData.IsLeader;

		PartyMemberData.IsFriend = CheckPartyMemberIsFriend(RHPartyMember->PlayerUuid);
		PartyMemberData.IsReady = true;
	}
	return true;

}

TArray<FRH_PartyMemberData> URHPartyManager::GetNewPartyMemberData()
{
	TArray<FRH_PartyMemberData> NewPartyMembers;

	URH_LocalPlayerSubsystem* LPSS = GetLocalPlayerSubsystem();
	URH_FriendSubsystem* RHFS = GetFriendSubsystem();
	
	if (PartySession != nullptr)
	{
		for (auto& Team : PartySession->GetSessionData().Teams)
		{
			for (auto& Player : Team.Players)
			{
				FRH_PartyMemberData PartyMemberData;

				if (PopulatePartyMemberData(&Player, PartyMemberData))
				{
					if (LPSS != nullptr && RHFS != nullptr)
					{
						RHFS->UpdateRecentPlayerForOSS(LPSS, Player.PlayerUuid);
					}

					NewPartyMembers.Add(PartyMemberData);
				}
			}
		}
	}
	return NewPartyMembers;
}

bool URHPartyManager::CheckPartyMemberIsFriend(const FGuid& PlayerId)
{
	if (auto RHFS = GetFriendSubsystem())
	{
		if (auto Friend = RHFS->GetFriendByUuid(PlayerId))
		{
			if (Friend->AreFriends())
			{
				return true;
			}
		}
	}

	return false;
}

bool URHPartyManager::CheckPartyMemberIsLeader(const FGuid& PlayerId) const
{
	if (PartySession != nullptr)
	{
		auto* Player = PartySession->GetSessionPlayer(PlayerId);
		if (Player != nullptr)
		{
			return Player->Status == ERHAPI_SessionPlayerStatus::Leader;
		}
	}
	return false;
}

bool URHPartyManager::IsLeader() const
{
	if (MyHud != nullptr)
	{
		return CheckPartyMemberIsLeader(MyHud->GetLocalPlayerUuid());
	}

	return false;
}

bool URHPartyManager::GetPartyLeader(FRHAPI_SessionPlayer& OutPlayer) const
{
	if (PartySession)
	{
		FRHAPI_Session SessionData = PartySession->GetSessionData();

		for (FRHAPI_SessionTeam Team : SessionData.GetTeams())
		{
			for (FRHAPI_SessionPlayer Player : Team.GetPlayers())
			{
				if (Player.GetStatus() == ERHAPI_SessionPlayerStatus::Leader)
				{
					OutPlayer = Player;
					return true;
				}
			}
		}
	}
	return false;
}

bool URHPartyManager::HasInvitePrivileges(const FGuid& PlayerId) const
{
	return CheckPartyMemberIsLeader(PlayerId);
}

bool URHPartyManager::IsPlayerInParty(const FGuid& PlayerId)
{
	// NOTE: should actually be checking mcts, not the data layer here
	for (auto PartyMemberData : PartyMembers)
	{
		if (PartyMemberData.PlayerData->GetRHPlayerUuid() == PlayerId)
		{
			// player id found amongst party members!
			return true;
		}
	}
	return false;
}

FRH_PartyMemberData URHPartyManager::GetPartyMemberByID(const FGuid& PlayerID)
{
	FRH_PartyMemberData PlayerPartyMemberData;
	if (PlayerID.IsValid())
	{
		for (auto PartyMemberData : PartyMembers)
		{
			if (PartyMemberData.PlayerData->GetRHPlayerUuid() == PlayerID)
			{
				PlayerPartyMemberData = PartyMemberData;
			}
		}
	}

	return PlayerPartyMemberData;
}

bool URHPartyManager::IsInParty() const
{
	return PartySession != nullptr && GetPartyMemberCount() > 1;
}

void URHPartyManager::SetPartyInfo(const FString& Key, const FString& Value)
{
	if (PartySession != nullptr && IsLeader())
	{
		auto Info = PartySession->GetSessionUpdateInfoDefaults();
		Info.CustomData_Optional.Add(Key, Value);
		Info.CustomData_IsSet = true;
		PartySession->UpdateSessionInfo(Info);
	}
}

FString URHPartyManager::GetPartyInfo(const FString& Key) const
{
	FString Value;
	if (PartySession != nullptr)
	{
		PartySession->GetCustomDataValue(Key, Value);
	}

	return Value;
}

void URHPartyManager::OnNewPartyMemberAdded(FRH_PartyMemberData* NewPartyMemberData)
{
	if (NewPartyMemberData != nullptr && NewPartyMemberData->PlayerData != nullptr)
	{
		/* #RHTODO - Voice
		if (NewPartyMemberData->PlayerData->IsIgnored())
		{
			//
			//if (pcomGetVoice() != nullptr && pcomGetVoice()->IsInitialized())
			//{
			//	pcomGetVoice()->MutePlayer(NewPartyMemberData->PlayerData->GetPlayerNetId(), true);
			//}
		}
		*/
	}
}

void URHPartyManager::UIX_InviteMemberToParty(const FGuid& PlayerUuid)
{
	auto HandleInviteSentDelegate = FRH_OnSessionUpdatedDelegate::CreateWeakLambda(this, [this, PlayerUuid](bool bSuccess, URH_SessionView* pUpdatedSession, const FRH_ErrorInfo& ErrorInfo)
		{
			auto Player = MyHud->GetOrCreatePlayerInfo(PlayerUuid);
			if (!bSuccess)
			{
				// Handle generic error
				Player->GetLastKnownDisplayNameAsync(FTimespan(), false, ERHAPI_Platform::Anon, FRH_PlayerInfoGetDisplayNameDelegate::CreateWeakLambda(this, [this, Player](bool bSuccess, const FString& DisplayName)
					{
						FString PlayerName = bSuccess ? DisplayName : Player->GetRHPlayerUuid().ToString();
						FText InvitationError = FText::Format(NSLOCTEXT("RHFriendList", "InvitationError", "Could not invite {0} to party."), FText::FromString(PlayerName));
						OnPartyInvitationError.Broadcast(InvitationError);

						UE_LOG(RallyHereStart, Warning, TEXT("Generic Error handled for UIX_InviteMemberToParty - Could not invite player %s to party."), *PlayerName);

					}), MyHud->GetLocalPlayerSubsystem());
			}
			else
			{
				OnPartyInvitationSent.Broadcast(Player);
			}
		});

	if (PartySession != nullptr && MyHud != nullptr)
	{
		auto* PlayerInfo = MyHud->GetOrCreatePlayerInfo(PlayerUuid);

		auto LocalPlayerUuid = MyHud->GetLocalPlayerUuid();
		const auto* LocalSessionPlayer = PartySession->GetSessionPlayer(LocalPlayerUuid);

		if (PlayerInfo && LocalSessionPlayer != nullptr && HasInvitePrivileges(LocalPlayerUuid))
		{
			bool IsPlatformCrossplayEnabled = false;
			if (URH_LocalPlayerSubsystem* LPSS = GetLocalPlayerSubsystem())
			{
				if (URH_LocalPlayerLoginSubsystem* LoginSS = LPSS->GetLoginSubsystem())
				{
					IsPlatformCrossplayEnabled = LoginSS->IsCrossplayEnabled();
				}
			}

			if (!IsPlatformCrossplayEnabled)
			{
				PlayerInfo->GetLinkedPlatformInfo(FTimespan::FromMinutes(1), false, FRH_PlayerInfoGetPlatformsDelegate::CreateWeakLambda(this, [this, PlayerUuid, HandleInviteSentDelegate, PlayerInfo](bool bSucess, const TArray<URH_PlayerPlatformInfo*>& PlatformInfos)
					{
						TWeakObjectPtr<URH_FriendSubsystem> RHFS = GetFriendSubsystem();
						if (RHFS.IsValid())
						{
							for (auto* PlatformInfo : PlatformInfos)
							{
								auto* Friend = RHFS->GetFriendByPlatformId(PlatformInfo->PlayerPlatformId);

								if (Friend != nullptr && Friend->HavePlatformRelationship())
								{
									if (PartySession != nullptr)
									{
										PartySession->InvitePlayer(PlayerUuid, 0, TMap<FString, FString>(), HandleInviteSentDelegate);
									}
								}
							}
						}
					}));
			}
			else
			{
				PartySession->InvitePlayer(PlayerUuid, 0, TMap<FString, FString>(), HandleInviteSentDelegate);
			}

		}
	}
}

void URHPartyManager::UIX_AcceptPartyInvitation()
{
	TArray<URH_InvitedSession*> InvitedSessions = GetSessionInvites();

	auto* InvitedSession = InvitedSessions.Num() > 0 ? InvitedSessions[0] : nullptr;

	if (InvitedSession)
	{
		UE_LOG(RallyHereStart, Log, TEXT("Accepting pending invite."));
		OnPartyInvitationAccepted.Broadcast();

		//reset party inviter
		PartyInviter = nullptr;

		InvitedSession->Join(FRH_OnSessionUpdatedDelegate::CreateUObject(this, &URHPartyManager::HandleSessionUpdate));
	}
}

void URHPartyManager::UIX_DenyPartyInvitation()
{
	TArray<URH_InvitedSession*> InvitedSessions = GetSessionInvites();

	auto* InvitedSession = InvitedSessions.Num() > 0 ? InvitedSessions[0] : nullptr;

	if (InvitedSession)
	{
		UE_LOG(RallyHereStart, Log, TEXT("Denying pending invite."));
		OnPartyInvitationRejected.Broadcast();

		//reset party inviter
		PartyInviter = nullptr;

		InvitedSession->Leave("DenyPartyInvite");
	}
}

void URHPartyManager::UIX_PromoteMemberToLeader(const FGuid& PlayerId)
{
	if (PartySession != nullptr && IsPlayerInParty(PlayerId) && IsLeader())
	{
		UE_LOG(RallyHereStart, Log, TEXT("Setting Player %s to leader."), *PlayerId.ToString());

		auto Handler = FRH_PlayerInfoGetDisplayNameDelegate::CreateWeakLambda(this, [this, PlayerId](bool bSuccess, const FString& DisplayName)
			{
				FText NameToUse = bSuccess ? FText::FromString(DisplayName) : FText::FromString(PlayerId.ToString());
				URHPopupManager* popup = MyHud->GetPopupManager();

				FRHPopupConfig promotePopupParams;
				promotePopupParams.Header = NSLOCTEXT("GeneralParty", "PromoteMemberTitle", "Promote Party Member");
				promotePopupParams.Description = FText::Format(NSLOCTEXT("GeneralParty", "PromoteMemberMsg", "Are you sure you want to promote {0} to party leader?"), NameToUse);
				promotePopupParams.IsImportant = true;
				promotePopupParams.CancelAction.AddDynamic(popup, &URHPopupManager::OnPopupCanceled);

				FRHPopupButtonConfig& promoteConfirmBtn = (promotePopupParams.Buttons[promotePopupParams.Buttons.Add(FRHPopupButtonConfig())]);
				promoteConfirmBtn.Label = NSLOCTEXT("General", "Confirm", "Confirm");
				promoteConfirmBtn.Type = ERHPopupButtonType::Confirm;
				promoteConfirmBtn.Action.AddDynamic(this, &URHPartyManager::PartyPromoteResponse);
				FRHPopupButtonConfig& promoteCancelBtn = (promotePopupParams.Buttons[promotePopupParams.Buttons.Add(FRHPopupButtonConfig())]);
				promoteCancelBtn.Label = NSLOCTEXT("General", "Cancel", "Cancel");
				promoteCancelBtn.Type = ERHPopupButtonType::Cancel;
				promoteCancelBtn.Action.AddDynamic(popup, &URHPopupManager::OnPopupCanceled);

				popup->AddPopup(promotePopupParams);
			});

		MemberPromotedId = PlayerId;
		URH_PlayerInfo* PlayerInfo = GetPartyMemberByID(PlayerId).PlayerData;
		PlayerInfo->GetLastKnownDisplayNameAsync(FTimespan(), false, ERHAPI_Platform::Anon, Handler, MyHud != nullptr ? MyHud->GetLocalPlayerSubsystem() : nullptr);
		return;
	}

	UE_LOG(RallyHereStart, Warning, TEXT("Could not set player %s to leader."), *PlayerId.ToString());
	return;
}

void URHPartyManager::UIX_LeaveParty()
{
	if (PartySession != nullptr)
	{
		UE_LOG(RallyHereStart, Log, TEXT("Leaving party."));

		URHPopupManager* popup = MyHud->GetPopupManager();

		FRHPopupConfig leavePopupParams;
		leavePopupParams.Header = NSLOCTEXT("GeneralParty", "LeavePartyTitle", "Leave Party");
		leavePopupParams.Description = NSLOCTEXT("GeneralParty", "LeavePartyMsg", "Are you sure you want to leave the party?");
		leavePopupParams.IsImportant = true;
		leavePopupParams.CancelAction.AddDynamic(popup, &URHPopupManager::OnPopupCanceled);

		FRHPopupButtonConfig& leaveConfirmBtn = (leavePopupParams.Buttons[leavePopupParams.Buttons.Add(FRHPopupButtonConfig())]);
		leaveConfirmBtn.Label = NSLOCTEXT("General", "Confirm", "Confirm");
		leaveConfirmBtn.Type = ERHPopupButtonType::Confirm;
		leaveConfirmBtn.Action.AddDynamic(this, &URHPartyManager::PartyLeaveResponse);
		FRHPopupButtonConfig& leaveCancelBtn = (leavePopupParams.Buttons[leavePopupParams.Buttons.Add(FRHPopupButtonConfig())]);
		leaveCancelBtn.Label = NSLOCTEXT("General", "Cancel", "Cancel");
		leaveCancelBtn.Type = ERHPopupButtonType::Cancel;
		leaveCancelBtn.Action.AddDynamic(popup, &URHPopupManager::OnPopupCanceled);

		popup->AddPopup(leavePopupParams);
	}
	UE_LOG(RallyHereStart, Warning, TEXT("Could not leave party."));
	return;
}

void URHPartyManager::UIX_KickMemberFromParty(const FGuid& PlayerId)
{
	if (PartySession != nullptr && IsPlayerInParty(PlayerId) && IsLeader())
	{
		UE_LOG(RallyHereStart, Log, TEXT("Kicking player %s."), *PlayerId.ToString());

		auto Handler = FRH_PlayerInfoGetDisplayNameDelegate::CreateWeakLambda(this, [this, PlayerId](bool bSuccess, const FString& DisplayName)
			{
				FText NameToUse = bSuccess ? FText::FromString(DisplayName) : FText::FromString(PlayerId.ToString());
				URHPopupManager* popup = MyHud->GetPopupManager();

				FRHPopupConfig kickPopupParams;
				kickPopupParams.Header = NSLOCTEXT("GeneralParty", "KickWarningTitle", "Kick Party Member");
				kickPopupParams.Description = FText::Format(NSLOCTEXT("GeneralParty", "KickWarningMsg", "Are you sure you want to kick {0} from the party?"), NameToUse);

				kickPopupParams.IsImportant = true;
				kickPopupParams.CancelAction.AddDynamic(popup, &URHPopupManager::OnPopupCanceled);

				FRHPopupButtonConfig& kickConfirmBtn = (kickPopupParams.Buttons[kickPopupParams.Buttons.Add(FRHPopupButtonConfig())]);
				kickConfirmBtn.Label = NSLOCTEXT("General", "Confirm", "Confirm");
				kickConfirmBtn.Type = ERHPopupButtonType::Confirm;
				kickConfirmBtn.Action.AddDynamic(this, &URHPartyManager::PartyKickResponse);
				FRHPopupButtonConfig& kickCancelBtn = (kickPopupParams.Buttons[kickPopupParams.Buttons.Add(FRHPopupButtonConfig())]);
				kickCancelBtn.Label = NSLOCTEXT("General", "Cancel", "Cancel");
				kickCancelBtn.Type = ERHPopupButtonType::Cancel;
				kickCancelBtn.Action.AddDynamic(popup, &URHPopupManager::OnPopupCanceled);

				popup->AddPopup(kickPopupParams);
			});

		MemberKickId = PlayerId;
		URH_PlayerInfo* PlayerInfo = GetPartyMemberByID(PlayerId).PlayerData;
		PlayerInfo->GetLastKnownDisplayNameAsync(FTimespan(), false, ERHAPI_Platform::Anon, Handler, MyHud != nullptr ? MyHud->GetLocalPlayerSubsystem() : nullptr);
		return;
	}
	UE_LOG(RallyHereStart, Warning, TEXT("Could not kick player %s."), *PlayerId.ToString());
	return;
}

void URHPartyManager::UIX_PlayerKickedFromParty()
{
	URHPopupManager* popup = MyHud->GetPopupManager();

	FRHPopupConfig kickPopupParams;
	kickPopupParams.Header = NSLOCTEXT("GeneralParty", "PartyKickedTitle", "Kicked from Party");
	kickPopupParams.Description = NSLOCTEXT("GeneralParty", "PartyKickedMsgLocal", "You have been kicked from the party.");

	kickPopupParams.IsImportant = true;
	kickPopupParams.CancelAction.AddDynamic(popup, &URHPopupManager::OnPopupCanceled);

	FRHPopupButtonConfig& kickConfirmBtn = (kickPopupParams.Buttons[kickPopupParams.Buttons.Add(FRHPopupButtonConfig())]);
	kickConfirmBtn.Label = NSLOCTEXT("General", "Okay", "Okay");
	kickConfirmBtn.Type = ERHPopupButtonType::Confirm;
	kickConfirmBtn.Action.AddDynamic(popup, &URHPopupManager::OnPopupCanceled);

	kickPopupParams.TopImageTextureName = "CustomKick";

	popup->AddPopup(kickPopupParams);
}

const FString SelectedQueueIdKey = "SelectedQueueId";

void URHPartyManager::SetSelectedQueueId(const FString& QueueId)
{
	SetPartyInfo(SelectedQueueIdKey, QueueId);
}

FString URHPartyManager::GetSelectedQueueId() const
{
	return GetPartyInfo(SelectedQueueIdKey);
}

void URHPartyManager::PartyKickResponse()
{
	if (PartySession && IsLeader())
	{
		PartySession->KickPlayer(MemberKickId);
	}

	MemberKickId.Invalidate();
	return;
}

void URHPartyManager::PartyLeaveResponse()
{
	if (PartySession != nullptr)
	{
		LeaveQueue();

		IsPendingLeave = true;
		PartySession->Leave(false, "LeaveParty");
		
		CreateSoloParty();
	}

	return;
}

void URHPartyManager::PartyPromoteResponse()
{
	if (PartySession != nullptr)
	{
		PartySession->SetLeader(MemberPromotedId);

		MemberPromotedId.Invalidate();
	}
	return;
}

void URHPartyManager::ForcePartyCleanUp(bool ForceLeave /*= false*/)
{
	// attempt to deny a lingering pending invites
	this->UIX_DenyPartyInvitation();

	if (ForceLeave)
	{
		// leave any party we might be in
		if (PartySession != nullptr)
		{
			PartySession->Leave(false, "ForcePartyCleanup");
		}
	}
}

void URHPartyManager::HandleSessionUpdate(bool bSuccess, URH_SessionView* pSession, const FRH_ErrorInfo& ErrorInfo)
{
	FText SessionUpdateMessage = FText::GetEmpty();
	if (!bSuccess)
	{
		SessionUpdateMessage = NSLOCTEXT("GeneralParty", "PortalSessionFailure", "Failed to join Party Session.");
	}

	if (MyHud != nullptr && !SessionUpdateMessage.IsEmpty())
	{		
		URHPopupManager* popup = MyHud->GetPopupManager();

		FRHPopupConfig promotePopupParams;
		promotePopupParams.Header = NSLOCTEXT("GeneralParty", "PortalSessionUpdate", "Party Session");
		promotePopupParams.Description = SessionUpdateMessage;
		promotePopupParams.IsImportant = true;
		promotePopupParams.CancelAction.AddDynamic(popup, &URHPopupManager::OnPopupCanceled);

		FRHPopupButtonConfig& promoteConfirmBtn = (promotePopupParams.Buttons[promotePopupParams.Buttons.Add(FRHPopupButtonConfig())]);
		promoteConfirmBtn.Label = NSLOCTEXT("General", "Confirm", "Confirm");
		promoteConfirmBtn.Type = ERHPopupButtonType::Confirm;
		promoteConfirmBtn.Action.AddDynamic(popup, &URHPopupManager::OnPopupCanceled);

		popup->AddPopup(promotePopupParams);
	}
}

void URHPartyManager::LeaveQueue()
{
	/* RHOTODO: WTF M8
	if (ARHLobbyHud* LobbyHud = Cast<ARHLobbyHud>(MyHud))
	{
		if (auto* QueueDataFactory = LobbyHud->GetQueueDataFactory())
		{
			QueueDataFactory->LeaveQueue();
		}
	}
	*/
}

URH_LocalPlayerSubsystem* URHPartyManager::GetLocalPlayerSubsystem() const
{
	if (!MyHud)
	{
		return nullptr;
	}

	return MyHud->GetLocalPlayerSubsystem();
}

URH_LocalPlayerSessionSubsystem* URHPartyManager::GetPlayerSessionSubsystem() const
{
	URH_LocalPlayerSubsystem* RHSS = GetLocalPlayerSubsystem();

	if (RHSS != nullptr)
	{
		return RHSS->GetSessionSubsystem();
	}
	return nullptr;
}

URH_FriendSubsystem* URHPartyManager::GetFriendSubsystem() const
{
	URH_LocalPlayerSubsystem* RHSS = GetLocalPlayerSubsystem();

	if (RHSS != nullptr)
	{
		return RHSS->GetFriendSubsystem();
	}
	return nullptr;
}

void URHPartyManager::CreateSoloParty()
{
	if (URH_LocalPlayerSessionSubsystem* RHSS = GetPlayerSessionSubsystem())
	{
		if (RHSS->GetFirstJoinedSessionByType(RHSessionType) == nullptr)
		{
			FString CurrentRegionId;
			if (MyHud && MyHud->GetPreferredRegionId(CurrentRegionId))
			{
				FRHAPI_CreateOrJoinRequest Params = {};
				Params.SetSessionType(RHSessionType);
				Params.SetRegionId(CurrentRegionId);
				Params.SetClientVersion(URH_JoinedSession::GetClientVersionForSession());
				RHSS->CreateOrJoinSessionByType(Params, FRH_OnSessionUpdatedDelegate::CreateUObject(this, &URHPartyManager::HandleSessionCreated));
			}
			else
			{
				UE_LOG(RallyHereStart, Warning, TEXT("UpdateParty - URHPartyManager::CreateSoloParty failed to get preferred region"));
			}
		}
		else
		{
			UE_LOG(RallyHereStart, Warning, TEXT("UpdateParty - URHPartyManager::CreateSoloParty failed because party already exists"));
		}
	}
}

void URHPartyManager::HandlePreferredRegionUpdated()
{
	if (PartySession)
	{
		// Only update session region if player is in solo party or is leader
		if (!IsInParty() || (IsInParty() && IsLeader()))
		{
			FString CurrentRegionId;
			if (MyHud && MyHud->GetPreferredRegionId(CurrentRegionId))
			{
				if (CurrentRegionId != PartySession->GetSessionData().GetRegionId())
				{
					FRHAPI_SessionUpdate UpdateInfo = PartySession->GetSessionUpdateInfoDefaults();
					UpdateInfo.SetRegionId(CurrentRegionId);

					PartySession->UpdateSessionInfo(UpdateInfo, FRH_OnSessionUpdatedDelegate::CreateUObject(this, &URHPartyManager::HandleUpdateSessionRegionIdResponse));
				}
			}
		}
	}
	else if (MyHud != nullptr && LastLoginPlayerGuid == MyHud->GetLocalPlayerUuid())
	{
		// If we don't have a party session and LoginPollSessions has already completed,
		// we must have failed to create a solo party due to preferred region not being available
		CreateSoloParty();
	}
}

void URHPartyManager::HandleUpdateSessionRegionIdResponse(bool bSuccess, URH_SessionView* pSession, const FRH_ErrorInfo& ErrorInfo)
{
	if (!bSuccess)
	{
		UE_LOG(RallyHereStart, Warning, TEXT("URHPartyManager Updating session's region id failed."));

		// If is party leader, switch back region setting. If solo, leave current session, create new one with new region.
		if (IsInParty())
		{
			if (IsLeader())
			{
				if (PartySession)
				{
					MyHud->SetPreferredRegionId(PartySession->GetSessionData().GetRegionId());

					URHPopupManager* popup = MyHud->GetPopupManager();
					FRHPopupConfig promotePopupParams;
					promotePopupParams.Header = FText::FromString("*NO LOC* Failed to Update Region");
					promotePopupParams.Description = FText::FromString("*NO LOC* Region setting will be changed back");
					promotePopupParams.IsImportant = true;
					promotePopupParams.CancelAction.AddDynamic(popup, &URHPopupManager::OnPopupCanceled);
					popup->AddPopup(promotePopupParams);
				}
			}
		}
		else
		{
			PartyLeaveResponse();
		}
	}
}
