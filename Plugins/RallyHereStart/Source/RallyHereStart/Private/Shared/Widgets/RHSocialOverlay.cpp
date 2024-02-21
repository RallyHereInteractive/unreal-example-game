// Copyright 2022-2023 Rally Here Interactive, Inc. All Rights Reserved.

#include "RallyHereStart.h"
#include "Shared/Widgets/RHSocialOverlay.h"

#include "Shared/Social/RHDataSocialCategory.h"
#include "Shared/HUD/RHHUDCommon.h"
#include "Lobby/HUD/RHLobbyHUD.h"
#include "RHUIBlueprintFunctionLibrary.h"
#include "RH_GameInstanceSubsystem.h"

#include "Algo/BinarySearch.h"
#include "Algo/Transform.h"

namespace // PIMPL
{
	inline FText GetHeaderForFriendCategory(FRHSocialOverlaySectionInfo Section)
	{
		switch (Section.Section)
		{
		case ERHSocialOverlaySection::OnlineRHFriends:			return NSLOCTEXT("RHSocial", "OnlineFriendsMcts", "PLAYING EXAMPLE GAME");
		case ERHSocialOverlaySection::OfflineRHFriends:			return NSLOCTEXT("RHSocial", "OfflineFriendsMcts", "OFFLINE");
		case ERHSocialOverlaySection::OnlinePlatformFriends:	return NSLOCTEXT("RHSocial", "OnlineFriendsPortal", "AVAILABLE");
		case ERHSocialOverlaySection::SessionMembers:
			// Currently we only display party sessions here, but can expand if anyone wants to.
			if (Section.SubSection == "party")
			{
				return NSLOCTEXT("RHSocial", "PartyMembers", "PARTY");
			}
			else
			{
				FText::GetEmpty();
			}
		case ERHSocialOverlaySection::SessionInvitations:		return NSLOCTEXT("RHSocial", "SessionInvitesHeader", "SESSION INVITES");
		case ERHSocialOverlaySection::Blocked:					return NSLOCTEXT("RHSocial", "Blocked", "BLOCKED");
		case ERHSocialOverlaySection::Pending:					return NSLOCTEXT("RHSocial", "PendingFriends", "SENT REQUESTS");
		case ERHSocialOverlaySection::SearchResults:			return NSLOCTEXT("RHSocial", "SearchResults", "SEARCH RESULTS");
		case ERHSocialOverlaySection::FriendInvites:			return NSLOCTEXT("RHSocial", "FriendInvites", "INCOMING REQUESTS");
		case ERHSocialOverlaySection::RecentlyPlayed:			return NSLOCTEXT("RHSocial", "RecentlyPlayed", "RECENTLY PLAYED");
		case ERHSocialOverlaySection::SuggestedFriends:			return NSLOCTEXT("RHSocial", "SuggestedHeader", "SUGGESTED FRIENDS");

		case ERHSocialOverlaySection::MAX:
		case ERHSocialOverlaySection::Invalid:
		default:
			break;
		}
		return FText::GetEmpty();
	}

	TFunction<bool(URH_RHFriendAndPlatformFriend*, URH_RHFriendAndPlatformFriend*)> CreateSocialSessionSort(const FGuid& SessionLeaderUuid)
	{
		return [SessionLeaderUuid](URH_RHFriendAndPlatformFriend* A, URH_RHFriendAndPlatformFriend* B) -> bool
		{
			if (A == B) { return false; } // Fix for LowerBound

			const bool AIsLeader = A->GetRHPlayerUuid() == SessionLeaderUuid;
			const bool BIsLeader = B->GetRHPlayerUuid() == SessionLeaderUuid;
			if (AIsLeader || BIsLeader)
			{
				return AIsLeader;
			}
			return RHSocial::PlayerSortByName(A, B);
		};
	}

	TFunction<bool(URH_RHFriendAndPlatformFriend*, URH_RHFriendAndPlatformFriend*)> GetSortMethodForSocialSection(ERHSocialOverlaySection Section, ARHHUDCommon* RHHUDCommon = nullptr)
	{
		switch (Section)
		{
		case ERHSocialOverlaySection::SessionMembers:
			if (RHHUDCommon)
			{
				if (const URHPartyManager* PartyManager = RHHUDCommon->GetPartyManager())
				{
					TArray<FRH_PartyMemberData> PartyMembers = PartyManager->GetPartyMembers();
					for (FRH_PartyMemberData PartyMember : PartyMembers)
					{
						if (PartyMember.IsLeader)
						{
							return CreateSocialSessionSort(PartyMember.PlayerData->GetRHPlayerUuid());
						}
					}
				}
			}
			return &RHSocial::PlayerSortByName;
		case ERHSocialOverlaySection::SessionInvitations:
		case ERHSocialOverlaySection::OnlineRHFriends:
		case ERHSocialOverlaySection::OnlinePlatformFriends:
		case ERHSocialOverlaySection::OfflineRHFriends:
		case ERHSocialOverlaySection::Blocked:
		case ERHSocialOverlaySection::Pending:
		case ERHSocialOverlaySection::SearchResults:
		case ERHSocialOverlaySection::FriendInvites:
		case ERHSocialOverlaySection::RecentlyPlayed:
		case ERHSocialOverlaySection::SuggestedFriends:
			return &RHSocial::PlayerSortByName;

		case ERHSocialOverlaySection::Invalid:
		case ERHSocialOverlaySection::MAX:
		default:
			checkNoEntry();
			break;
		}

		return [](URH_RHFriendAndPlatformFriend*,URH_RHFriendAndPlatformFriend*) { return false; };
	}
}

bool RHSocial::PlayerSortByName(const URH_RHFriendAndPlatformFriend* A, const URH_RHFriendAndPlatformFriend* B)
{
	const FString DisplayNameA = A->GetLastKnownDisplayName();
	const FString DisplayNameB = B->GetLastKnownDisplayName();
	if (!DisplayNameA.IsEmpty() && !DisplayNameB.IsEmpty())
	{
		const int32 Diff = DisplayNameA.Compare(DisplayNameB, ESearchCase::IgnoreCase);
		if (Diff != 0)
		{
			return Diff < 0;
		}
	}

	// Absolute last resort
	return A->GetRHPlayerUuid() < B->GetRHPlayerUuid();
}

bool RHSocial::CategorySort(URHDataSocialCategory* A, URHDataSocialCategory* B)
{
	return A->BP_GetSectionValue() < B->BP_GetSectionValue();
}

void URHSocialOverlay::InitializeWidget_Implementation()
{
	Super::InitializeWidget_Implementation();

	HasInitialFriendsData = false;
	OSSFriendsListRefreshInterval = 30.0f;

	// Create all of the categories
	constexpr int32 Max = static_cast<int32>(ERHSocialOverlaySection::MAX);
	CategoriesList.Reserve(Max);
	for (int32 i = static_cast<int32>(ERHSocialOverlaySection::Invalid) + 1; i < Max; ++i)
	{
		const ERHSocialOverlaySection Section = static_cast<ERHSocialOverlaySection>(i);

		if (Section != ERHSocialOverlaySection::SessionMembers)
		{
			URHDataSocialCategory* Category = NewObject<URHDataSocialCategory>();
			Category->SetSectionValue(Section);
			Category->SetSortMethod(GetSortMethodForSocialSection(Section, MyHud.Get()));
			Category->SetHeaderText(GetHeaderForFriendCategory(FRHSocialOverlaySectionInfo(Section)));
			Category->SetOpenOnUpdate(true);
			CategoriesList.Add(Category);
		}
		else
		{
			for (auto SessionType : SessionsTypesToDisplay)
			{
				URHDataSocialCategory* Category = NewObject<URHDataSocialCategory>();
				Category->SetSectionValue(Section);
				Category->SetSectionSubtype(SessionType);
				Category->SetSortMethod(GetSortMethodForSocialSection(Section, MyHud.Get()));
				Category->SetHeaderText(GetHeaderForFriendCategory(FRHSocialOverlaySectionInfo(Section, SessionType)));
				Category->SetOpenOnUpdate(true);
				CategoriesList.Add(Category);
			}
		}
	}

	if (const auto FSS = GetRH_LocalPlayerFriendSubsystem())
	{
		FSS->FriendListUpdatedDelegate.AddUObject(this, &URHSocialOverlay::OnFriendsListChange);
		FSS->FriendUpdatedDelegate.AddUObject(this, &URHSocialOverlay::OnFriendDataChangeGeneric);
		FSS->BlockedPlayerUpdatedDelegate.AddUObject(this, &URHSocialOverlay::OnBlockedPlayerUpdated);
	}

	if (MyHud != nullptr)
	{
		if (URHPartyManager* PartyManager = MyHud->GetPartyManager())
		{
			PartyManager->OnPartyMemberStatusChanged.AddDynamic(this, &URHSocialOverlay::OnPartyMemberChanged);
		}
	}

	if (const URH_LocalPlayerSubsystem* RHSS = GetRH_LocalPlayerSubsystem())
	{
		const FAuthContextPtr AuthContext = RHSS->GetAuthContext();
		if (AuthContext.IsValid())
		{
			AuthContext->OnLoginUserChanged().AddUObject(this, &URHSocialOverlay::HandleLogInUserChanged);
			if (AuthContext->IsLoggedIn())
			{
				HandleLogInUserChanged();
			}
		}
	}

	RepopulateAll(); // do initial population even if we don't have friends in case we have other data
}

void URHSocialOverlay::UninitializeWidget_Implementation()
{
	Super::UninitializeWidget_Implementation();

	if (const auto FSS = GetRH_LocalPlayerFriendSubsystem())
	{
		FSS->FriendUpdatedDelegate.RemoveAll(this);
		FSS->FriendListUpdatedDelegate.RemoveAll(this);
		FSS->BlockedPlayerUpdatedDelegate.RemoveAll(this);
	}

	if (NextUpdateTimer.IsValid() && MyHud.IsValid())
	{
		if (const UWorld* World = MyHud->GetWorld())
		{
			World->GetTimerManager().ClearTimer(NextUpdateTimer);
			NextUpdateTimer.Invalidate();
		}
	}

	if (const URH_PlayerInfoSubsystem* PSS = GetRH_PlayerInfoSubsystem())
	{
		const FAuthContextPtr AuthContext = PSS->GetAuthContext();
		if (AuthContext.IsValid())
		{
			AuthContext->OnLoginUserChanged().RemoveAll(this);
		}
	}
}

void URHSocialOverlay::OnShown_Implementation()
{
	Super::OnShown_Implementation();

	if (MyHud.IsValid())
	{
		if (const UWorld* World = MyHud->GetWorld())
		{
			if (RequestOSSFriendsListTimerHandle.IsValid())
			{
				World->GetTimerManager().ClearTimer(RequestOSSFriendsListTimerHandle);
				RequestOSSFriendsListTimerHandle.Invalidate();
			}
			World->GetTimerManager().SetTimer(RequestOSSFriendsListTimerHandle, this, &URHSocialOverlay::RequestOSSFriendsList, OSSFriendsListRefreshInterval, true);
		}
	}
	RequestOSSFriendsList();
}

void URHSocialOverlay::OnHide_Implementation()
{
	if (RequestOSSFriendsListTimerHandle.IsValid() && MyHud.IsValid())
	{
		if (const UWorld* World = MyHud->GetWorld())
		{
			World->GetTimerManager().ClearTimer(RequestOSSFriendsListTimerHandle);
			RequestOSSFriendsListTimerHandle.Invalidate();
		}
	}

	if (TransitionOutSequencePlayer != nullptr)
	{
		TransitionOutSequencePlayer->OnSequenceFinishedPlaying().RemoveAll(this);
	}

	Super::OnHide_Implementation();
}

void URHSocialOverlay::RepopulateAll()
{
	if (!MyHud.IsValid())
	{
		UE_LOG(RallyHereStart, Warning, TEXT("RHSocialFriendsPanel::RepopulateAll failed to get hud"));
		return;
	}

	// Total repopulate, so clear existing
	for (auto* Entry : CategoriesList)
	{
		if (Entry->GetSectionValue<ERHSocialOverlaySection>() != ERHSocialOverlaySection::SearchResults)
		{
			if (Entry->Num() == 0)
			{
				Entry->SetOpenOnUpdate(true);
			}
			UnusedEntries.Append(Entry->Empty(Entry->Num()));
		}
	}

	PlayersToUpdate.Empty();
	PlayerCategoryMap.Empty();

	const auto PSS = GetRH_PlayerInfoSubsystem();

	if (const auto FSS = GetRH_LocalPlayerFriendSubsystem())
	{
		auto Friends = FSS->GetFriends();
		if (IsCrossplayEnabled())
		{
			for (URH_RHFriendAndPlatformFriend* Friend : Friends)
			{
				if (Friend->GetRHPlayerUuid().IsValid())
				{
					if (URH_PlayerInfo* PlayerInfo = PSS->GetOrCreatePlayerInfo(Friend->GetRHPlayerUuid()))
					{
						if (!PlayerInfo->GetPresence()) // No presence info, request presence
						{
							RequestPresenceForPlayer(PlayerInfo);
						}
						else
						{
							SortPlayerToSocialSection(Friend);
						}
					}
					else
					{
						SortPlayerToSocialSection(Friend);
					}
				}
				else // Platform friend without UUID
				{
					SortPlayerToSocialSection(Friend);
				}
			}

			for (const auto& Blocked : FSS->GetBlocked())
			{
				if (FSS->GetFriendByUuid(Blocked))
				{
					continue;
				}

				URH_PlayerInfo* PlayerInfo = nullptr; 

				if (PSS != nullptr)
				{
					PlayerInfo = PSS->GetOrCreatePlayerInfo(Blocked);
				}

				if (PlayerInfo == nullptr)
				{
					continue;
				}

				URH_RHFriendAndPlatformFriend* Friend = FSS->GetOrCreateFriend(PlayerInfo);

				// No presence info, request presence
				if (!PlayerInfo->GetPresence())
				{
					RequestPresenceForPlayer(PlayerInfo);
				}
				else
				{
					SortPlayerToSocialSection(Friend);
				}
			}
		}
		else
		{
			if (auto* FriendCategory = GetCategory(ERHSocialOverlaySection::OnlinePlatformFriends))
			{
				FriendCategory->Empty(Friends.Num() / 2);

				for (const auto& Friend : Friends)
				{
					SortPlayerToSocialSection(Friend);
				}
			}
		}

		if (PSS != nullptr)
		{
			if (const auto LPSS = GetRH_LocalPlayerSessionSubsystem())
			{
				for (auto SessionType : SessionsTypesToDisplay)
				{
					if (const auto SessionView = LPSS->GetFirstSessionByType(SessionType))
					{
						if (SessionView->GetSessionPlayerCount() > 1)
						{
							URHDataSocialCategory* SessionCategoryFind = GetCategory(FRHSocialOverlaySectionInfo(ERHSocialOverlaySection::SessionMembers, SessionType));
							if (SessionCategoryFind != nullptr)
							{
								auto& Session = SessionView->GetSessionData();

								for (auto& Team : Session.Teams)
								{
									for (auto& Player : Team.Players)
									{
										if (URH_PlayerInfo* PlayerInfo = PSS->GetOrCreatePlayerInfo(Player.PlayerUuid))
										{
											if (URH_RHFriendAndPlatformFriend* Friend = FSS->GetOrCreateFriend(PlayerInfo))
											{
												if (auto* Container = MakePlayerContainer())
												{
													Container->SetFriend(Friend);
													SessionCategoryFind->Add(Container);
													PlayerCategoryMap.Emplace(Friend, FRHSocialOverlaySectionInfo(ERHSocialOverlaySection::SessionMembers, SessionType));
												}
											}
										}
									}
								}
							}
						}
					}
				}
			}
		}
	}
	else
	{
		UE_LOG(RallyHereStart, Error, TEXT("RHSocialFriendsPanel::RepopulateAll Failed to get friend data"));
	}

	for (auto* Category : CategoriesList)
	{
		// Only update empty categories if we weren't empty before, if we're marked to open it's bc we were empty
		if (Category->Num() == 0 && Category->ShouldOpenOnUpdate())
		{
			continue;
		}
		Category->OnPlayersUpdated.Broadcast(Category, Category->GetPlayerList()); // Dumbly dispatch
	}

	OnDataChanged.Broadcast(TArray<FRHSocialOverlaySectionInfo>());
}

FRHSocialOverlaySectionInfo URHSocialOverlay::GetSectionForSocialFriend(const URH_RHFriendAndPlatformFriend* Friend)
{
	if (Friend == nullptr)
	{
		UE_LOG(RallyHereStart, Warning, TEXT("GetSectionForSocialFriend called with invalid friend data"));
		return ERHSocialOverlaySection::Invalid;
	}

	bool IsInvitedToSession = false;

	if (const auto LPSS = GetRH_LocalPlayerSessionSubsystem())
	{
		for (auto SessionType : SessionsTypesToDisplay)
		{
			if (const auto SessionView = LPSS->GetFirstSessionByType(SessionType))
			{
				const URHDataSocialCategory* SessionCategoryFind = GetCategory(FRHSocialOverlaySectionInfo(ERHSocialOverlaySection::SessionMembers, SessionType));
				if (SessionCategoryFind != nullptr)
				{
					auto& Session = SessionView->GetSessionData();

					for (auto& Team : Session.Teams)
					{
						for (auto& Player : Team.Players)
						{
							// Only display parties if we have more than 1 player in them.
							if (Team.Players.Num() > 1)
							{
								if (Player.PlayerUuid == Friend->GetRHPlayerUuid())
								{
									return FRHSocialOverlaySectionInfo(ERHSocialOverlaySection::SessionMembers, SessionType);
								}

								const auto InvitingPlayer = Player.GetInvitingPlayerUuidOrNull();
								if (InvitingPlayer && InvitingPlayer->IsValid() && *InvitingPlayer == Friend->GetRHPlayerUuid())
								{
									IsInvitedToSession = true;
								}
							}
						}
					}
				}
			}
		}
	}

	if (const auto FSS = GetRH_LocalPlayerFriendSubsystem())
	{
		if (FSS->IsPlayerBlocked(Friend->GetRHPlayerUuid()))
		{
			return ERHSocialOverlaySection::Blocked;
		}
	}

	if (IsInvitedToSession)
	{
		return ERHSocialOverlaySection::SessionInvitations;
	}

	bool bIsPortalFriend = false, bIsOnlinePortalFriend = false, bIsPlayingThisGame = false;
	if (const URH_PlatformFriend* PortalFriend = Friend->GetPlatformFriend(URHUIBlueprintFunctionLibrary::GetLoggedInPlatformId(MyHud.Get())))
	{
		if (PortalFriend->IsFriend())
		{
			bIsPortalFriend = true;
			bIsOnlinePortalFriend = PortalFriend->IsOnline();
			bIsPlayingThisGame = PortalFriend->IsPlayingThisGame();
		}
	}

	if (Friend->AreRHFriends())
	{
		if (const auto PSS = GetRH_PlayerInfoSubsystem())
		{
			if (URH_PlayerInfo* PlayerInfo = PSS->GetPlayerInfo(Friend->GetRHPlayerUuid()))
			{
				if (const URH_PlayerPresence* FriendPresence = PlayerInfo->GetPresence())
				{
					if ((FriendPresence->Status == ERHAPI_OnlineStatus::Online && !bIsOnlinePortalFriend) || bIsPlayingThisGame)
					{
						return ERHSocialOverlaySection::OnlineRHFriends;
					}
				}
			}
		}
	}

	if (bIsOnlinePortalFriend)
	{
		return ERHSocialOverlaySection::OnlinePlatformFriends;
	}

	if (Friend->AreRHFriends())
	{
		return ERHSocialOverlaySection::OfflineRHFriends;
	}

	if (Friend->RHFriendRequestSent())
	{
		return ERHSocialOverlaySection::Pending;
	}

	if (Friend->RhPendingFriendRequest())
	{
		return ERHSocialOverlaySection::FriendInvites;
	}

	if (bIsPortalFriend && !Friend->HaveRhRelationship())
	{
		return ERHSocialOverlaySection::SuggestedFriends;
	}

	UE_LOG(RallyHereStart, Warning, TEXT("GetSectionForSocialFriend unhandled case for player %s"), *Friend->GetRHPlayerUuid().ToString());
	return ERHSocialOverlaySection::Invalid;
}

URH_LocalPlayerSubsystem* URHSocialOverlay::GetRH_LocalPlayerSubsystem() const
{
	if (!MyHud.IsValid())
	{
		return nullptr;
	}

	const APlayerController* PC = MyHud->GetOwningPlayerController();
	if (!PC)
	{
		UE_LOG(RallyHereStart, Error, TEXT("[%s] Failed to get owning player controller"), ANSI_TO_TCHAR(__FUNCTION__));
		return nullptr;
	}

	const ULocalPlayer* LocalPlayer = PC->GetLocalPlayer();
	if (!LocalPlayer)
	{
		UE_LOG(RallyHereStart, Error, TEXT("[%s] Failed to get local player for controller"), ANSI_TO_TCHAR(__FUNCTION__));
		return nullptr;
	}

	return LocalPlayer->GetSubsystem<URH_LocalPlayerSubsystem>();
}

URH_FriendSubsystem* URHSocialOverlay::GetRH_LocalPlayerFriendSubsystem() const
{
	const URH_LocalPlayerSubsystem* RHSS = GetRH_LocalPlayerSubsystem();
	if (!RHSS)
	{
		UE_LOG(RallyHereStart, Error, TEXT("[%s] RallyHere failed to get friend subsystem for controller"), ANSI_TO_TCHAR(__FUNCTION__));
		return nullptr;
	}

	return RHSS->GetFriendSubsystem();
}

URH_LocalPlayerSessionSubsystem* URHSocialOverlay::GetRH_LocalPlayerSessionSubsystem() const
{
	const URH_LocalPlayerSubsystem* RHSS = GetRH_LocalPlayerSubsystem();
	if (!RHSS)
	{
		UE_LOG(RallyHereStart, Error, TEXT("[%s] RallyHere failed to get friend subsystem for controller"), ANSI_TO_TCHAR(__FUNCTION__));
		return nullptr;
	}

	return RHSS->GetSessionSubsystem();
}

URH_PlayerInfoSubsystem* URHSocialOverlay::GetRH_PlayerInfoSubsystem() const
{
	const URH_LocalPlayerSubsystem* RHSS = GetRH_LocalPlayerSubsystem();
	if (!RHSS)
	{
		UE_LOG(RallyHereStart, Error, TEXT("[%s] RallyHere failed to get friend subsystem for controller"), ANSI_TO_TCHAR(__FUNCTION__));
		return nullptr;
	}

	return RHSS->GetPlayerInfoSubsystem();
}

void URHSocialOverlay::SchedulePlayersUpdate()
{
	if (MyHud.IsValid() && !NextUpdateTimer.IsValid())
	{
		HandleUpdatePlayers(); // update the first player update immediately

		if (UWorld* World = MyHud->GetWorld())
		{
			World->GetTimerManager().SetTimer(NextUpdateTimer, this, &URHSocialOverlay::HandleUpdatePlayers, 0.15, false);
		}
	}
}

void URHSocialOverlay::EnqueuePlayerUpdate(URH_RHFriendAndPlatformFriend* Friend)
{
	if (!Friend)
	{
		return;
	}

	bool bFound = false;
	for (TWeakObjectPtr<URH_RHFriendAndPlatformFriend>& UpdatingFriend : PlayersToUpdate)
	{
		if (UpdatingFriend.IsValid() && UpdatingFriend.Get() == Friend)
		{
			bFound = true;
			break;
		}
	}

	if (!bFound)
	{
		PlayersToUpdate.Add(Friend);
	}
}

void URHSocialOverlay::TryChangePartyLeaderAndReSort(const FGuid& PlayerUuid)
{
	if (PlayerUuid != OldPartyLeaderUuid)
	{
		OldPartyLeaderUuid = PlayerUuid;
		if (auto* Category = GetCategory(FRHSocialOverlaySectionInfo(ERHSocialOverlaySection::SessionMembers, "party")))
		{
			Category->SetSortMethod(CreateSocialSessionSort(PlayerUuid));
			OnDataChanged.Broadcast({FRHSocialOverlaySectionInfo(ERHSocialOverlaySection::SessionMembers, "party")});
		}
	}
}

void URHSocialOverlay::HandleUpdatePlayers()
{
	if (!IsValid(this) || !MyHud.IsValid())
	{
		return;
	}
	
	const UWorld* MyWorld = MyHud->GetWorld();
	if (MyWorld != nullptr)
	{
		if (NextUpdateTimer.IsValid())
		{
			MyWorld->GetTimerManager().ClearTimer(NextUpdateTimer);
		}
		NextUpdateTimer.Invalidate();
	}

	if (PlayersToUpdate.Num() == 0)
	{
		return;
	}

	const auto FSS = GetRH_LocalPlayerFriendSubsystem();

	TSet<FRHSocialOverlaySectionInfo> SectionsChanged;

	for (TWeakObjectPtr<URH_RHFriendAndPlatformFriend>& Friend : PlayersToUpdate)
	{
		if (Friend.IsValid())
		{
			FRHSocialOverlaySectionInfo NewSection = GetSectionForSocialFriend(Friend.Get());
			FRHSocialOverlaySectionInfo OldSection = FRHSocialOverlaySectionInfo(ERHSocialOverlaySection::Invalid);
			if (const auto* Entry = PlayerCategoryMap.Find(Friend))
			{
				OldSection = *Entry;
			}
			
			if (NewSection.Section != ERHSocialOverlaySection::Pending)
			{
				if (Friend->AwaitingFriendshipResponse())
				{
					if (AddPlayerToSection(Friend.Get(), FRHSocialOverlaySectionInfo(ERHSocialOverlaySection::Pending)))
					{
						SectionsChanged.Add(FRHSocialOverlaySectionInfo(ERHSocialOverlaySection::Pending));
					}
					SectionsChanged.Add(NewSection);
				}
				else if (URHDataSocialCategory* Category = GetCategory(ERHSocialOverlaySection::Pending))
				{
					if (Category->Remove(Friend.Get()))
					{
						SectionsChanged.Add(FRHSocialOverlaySectionInfo(ERHSocialOverlaySection::Pending));
					}
					SectionsChanged.Add(NewSection);
				}
			}

			if (NewSection.Section != ERHSocialOverlaySection::FriendInvites)
			{
				if (Friend->OtherIsAwaitingFriendshipResponse())
				{
					if (AddPlayerToSection(Friend.Get(), FRHSocialOverlaySectionInfo(ERHSocialOverlaySection::FriendInvites)))
					{
						SectionsChanged.Add(FRHSocialOverlaySectionInfo(ERHSocialOverlaySection::FriendInvites));
					}
					SectionsChanged.Add(NewSection);
				}
				else if (URHDataSocialCategory* Category = GetCategory(ERHSocialOverlaySection::FriendInvites))
				{
					if (Category->Remove(Friend.Get()))
					{
						SectionsChanged.Add(FRHSocialOverlaySectionInfo(ERHSocialOverlaySection::FriendInvites));
					}
					SectionsChanged.Add(NewSection);
				}
			}

			// We only handle category changes- nothing currently needs sort changing here
			if (OldSection == NewSection)
			{
				continue;
			}

			URHDataSocialPlayer* ToRecycle = nullptr;
			if (OldSection.Section != ERHSocialOverlaySection::Invalid)
			{
				if (URHDataSocialCategory* OldCategory = GetCategory(OldSection))
				{
					ToRecycle = OldCategory->Remove(Friend.Get());
				}
				SectionsChanged.Add(OldSection);
			}

			if (NewSection.Section != ERHSocialOverlaySection::Invalid)
			{
				if (URHDataSocialCategory* NewCategory = GetCategory(NewSection))
				{
					if (NewCategory->Num() == 0)
					{
						NewCategory->SetOpenOnUpdate(true);
					}

					if (ToRecycle != nullptr)
					{
						NewCategory->Add(ToRecycle);
						//Broadcast to ensure the Player Info is updated even though the Container is being reused.
						ToRecycle->OnRhDataUpdate.Broadcast(ToRecycle->GetFriend());
					}
					else if (auto* NewContainer = MakePlayerContainer())
					{
						NewContainer->SetFriend(Friend.Get());
						NewCategory->Add(NewContainer);
					}
				}
				PlayerCategoryMap.Emplace(Friend, NewSection);
				SectionsChanged.Add(NewSection);
			}
			else if (ToRecycle != nullptr)
			{
				PlayerCategoryMap.Remove(Friend);
				ToRecycle->SetFriend(nullptr);
				UnusedEntries.Add(ToRecycle);
			}
		}
	}
	PlayersToUpdate.Empty();

	for (const FRHSocialOverlaySectionInfo& Section : SectionsChanged)
	{
		if (auto* Category = GetCategory(Section))
		{
			Category->OnPlayersUpdated.Broadcast(Category, Category->GetPlayerList());
		}
	}

	OnDataChanged.Broadcast(SectionsChanged.Array());
}

void URHSocialOverlay::OnFriendsListChange(const TArray<URH_RHFriendAndPlatformFriend*>& UpdatedFriends)
{
	if (!HasInitialFriendsData)
	{
		OnFriendsReceived();
		HasInitialFriendsData = true;
	}

	RepopulateAll();
}

void URHSocialOverlay::OnFriendDataChangeGeneric(URH_RHFriendAndPlatformFriend* Friend)
{
	if (Friend->GetRHPlayerUuid().IsValid())
	{
		if (!Friend->OtherIsAwaitingFriendshipResponse())
		{
			EnqueuePlayerUpdate(Friend);
			SchedulePlayersUpdate();
			return;
		}

		bool friendBlocked = false;
		if (URH_FriendSubsystem* FSS = GetRH_LocalPlayerFriendSubsystem())
		{
			for (const auto& Blocked : FSS->GetBlocked())
			{
				if (Friend->GetRHPlayerUuid() == Blocked)
				{
					friendBlocked = true;
					break;
				}
			}
		}

		if (!friendBlocked)
		{
			EnqueuePlayerUpdate(Friend);
			SchedulePlayersUpdate();
		}
	} 
	else if (const auto PlatformFriend = Friend->GetPlatformFriendAtIndex(0))
	{
		EnqueuePlayerUpdate(Friend);
		SchedulePlayersUpdate();
	}
}

void URHSocialOverlay::OnPartyMemberChanged(const FGuid& PlayerUuid)
{
	if (const auto FSS = GetRH_LocalPlayerFriendSubsystem())
	{
		if (const auto PSS = GetRH_PlayerInfoSubsystem())
		{
			if (URH_PlayerInfo* PlayerInfo = PSS->GetPlayerInfo(PlayerUuid))
			{
				EnqueuePlayerUpdate(FSS->GetOrCreateFriend(PlayerInfo));
				SchedulePlayersUpdate();
			}
		}
	}
}

void URHSocialOverlay::OnBlockedPlayerUpdated(const FGuid& PlayerUuid, bool Blocked)
{
	if (const auto FSS = GetRH_LocalPlayerFriendSubsystem())
	{
		if (const auto PSS = GetRH_PlayerInfoSubsystem())
		{
			if (URH_PlayerInfo* PlayerInfo = PSS->GetPlayerInfo(PlayerUuid))
			{
				EnqueuePlayerUpdate(FSS->GetOrCreateFriend(PlayerInfo));
				SchedulePlayersUpdate();
			}
		}
	}
}

bool URHSocialOverlay::AddPlayerToSection(URH_RHFriendAndPlatformFriend* Friend, FRHSocialOverlaySectionInfo Section)
{
	// Skip over ourselves on placing in sections
	if (Friend->GetRHPlayerUuid() == MyHud->GetLocalPlayerUuid())
	{
		return false;
	}

	if (URHDataSocialCategory* Category = GetCategory(Section))
	{
		if (!Category->Contains(Friend))
		{
			if (auto* Container = MakePlayerContainer())
			{
				Container->SetFriend(Friend);
				Category->Add(Container);
				return true;
			}
		}
	}
	return false;
}

URHDataSocialPlayer* URHSocialOverlay::MakePlayerContainer()
{
	if (UnusedEntries.Num() > 0) { return UnusedEntries.Pop(); }
	return NewObject<URHDataSocialPlayer>(MyHud.Get());
}

void URHSocialOverlay::PlayTransition(class UWidgetAnimation* Animation, bool TransitionOut)
{
	if (TransitionOut)
	{
		TransitionOutSequencePlayer = PlayAnimationReverse(Animation);
		if (TransitionOutSequencePlayer != nullptr)
		{
			if (TransitionOutSequencePlayer->GetPlaybackStatus() == EMovieScenePlayerStatus::Stopped)
			{
				OnCloseTransitionComplete(*TransitionOutSequencePlayer);
			}
			else
			{
				TransitionOutSequencePlayer->OnSequenceFinishedPlaying().AddUObject(this, &URHSocialOverlay::OnCloseTransitionComplete);
			}
		}
	}
	else
	{
		PlayAnimationForward(Animation);
	}
}

void URHSocialOverlay::OnCloseTransitionComplete(UUMGSequencePlayer& SeqInfo)
{
	TransitionOutSequencePlayer = nullptr;
	HideWidget();
	CallOnHideSequenceFinished();
}

TArray<URHDataSocialCategory*> URHSocialOverlay::GetCategories(TArray<ERHSocialOverlaySection> Categories) const
{
	TArray<URHDataSocialCategory*> Result;
	Algo::Sort(Categories, [](const ERHSocialOverlaySection& A, const ERHSocialOverlaySection& B)
	{
		return static_cast<int32>(A) < static_cast<int32>(B);
	});

	int32 j = 0;
	for (int32 i = 0; i < Categories.Num() && j < CategoriesList.Num(); ++i)
	{
		for (; j < CategoriesList.Num(); ++j)
		{
			if (Categories[i] == CategoriesList[j]->GetSectionValue<ERHSocialOverlaySection>())
			{
				Result.Add(CategoriesList[j]);
				break;
			}
		}
	}
	return Result;
}

URHDataSocialCategory* URHSocialOverlay::GetCategory(FRHSocialOverlaySectionInfo Section) const
{
	for (auto* Entry : CategoriesList)
	{
		if (Entry->GetSectionValue<ERHSocialOverlaySection>() == Section.Section && Entry->GetSectionSubtype() == Section.SubSection)
		{
			return Entry;
		}
	}
	return nullptr;
}

void URHSocialOverlay::RequestPresenceForPlayer(const URH_PlayerInfo* PlayerInfo)
{
	if (PlayerInfo != nullptr && PlayerInfo->GetPresence() != nullptr)
	{
		PlayerInfo->GetPresence()->RequestUpdate(false, FRH_OnRequestPlayerInfoSubobjectDelegate::CreateUObject(this, &URHSocialOverlay::HandleGetPresence));
	}
}

void URHSocialOverlay::HandleGetPresence(bool bSuccessful, URH_PlayerInfoSubobject* PresenceInfo)
{
	if (bSuccessful)
	{
		if (URH_FriendSubsystem* FSS = GetRH_LocalPlayerFriendSubsystem())
		{
			// Check for this UUID in Friends
			URH_RHFriendAndPlatformFriend* Friend = FSS->GetFriendByUuid(PresenceInfo->PlayerUuid);
			if (Friend != nullptr)
			{
				SortPlayerToSocialSection(Friend);
			}
			// Check for this UUID in Blocked
			else if (FSS->IsPlayerBlocked(PresenceInfo->PlayerUuid))
			{
				
				SortPlayerToSocialSection(FSS->GetOrCreateFriend(PresenceInfo->GetPlayerInfo()));
			}
		}
	}
}

void URHSocialOverlay::SortPlayerToSocialSection(URH_RHFriendAndPlatformFriend* Friend)
{
	if (IsCrossplayEnabled())
	{
		FRHSocialOverlaySectionInfo SectionValue = GetSectionForSocialFriend(Friend);
		if (SectionValue.Section != ERHSocialOverlaySection::SessionMembers) // We do sessions logic later
		{
			AddPlayerToSection(Friend, SectionValue);
			PlayerCategoryMap.Emplace(Friend, SectionValue);
		}

		if (Friend->RHFriendRequestSent())
		{
			AddPlayerToSection(Friend, FRHSocialOverlaySectionInfo(ERHSocialOverlaySection::Pending));
		}

		if (Friend->RhPendingFriendRequest())
		{
			AddPlayerToSection(Friend, FRHSocialOverlaySectionInfo(ERHSocialOverlaySection::FriendInvites));
		}
	}
	else
	{
		FRHSocialOverlaySectionInfo SectionValue = GetSectionForSocialFriend(Friend);
		if (SectionValue.Section == ERHSocialOverlaySection::OnlinePlatformFriends) // Platform friends only here
		{
			AddPlayerToSection(Friend, SectionValue);
			PlayerCategoryMap.Emplace(Friend, SectionValue);
		}
	}
}

void URHSocialOverlay::HandleLogInUserChanged()
{
	if (URH_FriendSubsystem* FSS = GetRH_LocalPlayerFriendSubsystem())
	{
		if (!FSS->FetchFriendsList())
		{
			UE_LOG(RallyHereStart, Warning, TEXT("URHSocialOverlay::HandleLogInUserChanged failed to fetch friends"));
		}

		if (!FSS->FetchBlockedList())
		{
			UE_LOG(RallyHereStart, Warning, TEXT("URHSocialOverlay::HandleLogInUserChanged failed to fetch friends"));
		}

		if (FSS->HasFriendsCached())
		{
			OnFriendsReceived();
		}

		RequestOSSFriendsList();
	}
}

void URHSocialOverlay::RequestOSSFriendsList() const
{
	if (URH_FriendSubsystem* FSS = GetRH_LocalPlayerFriendSubsystem())
	{
		FSS->OSSReadFriendsList();
	}
}

bool URHSocialOverlay::IsCrossplayEnabled() const
{
	if (MyHud.IsValid())
	{
		return MyHud->IsPlatformCrossplayEnabled();
	}
	return false;
}