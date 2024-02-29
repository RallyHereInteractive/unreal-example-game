#include "RallyHereStart.h"
#include "Lobby/HUD/RHLobbyHUD.h"
#include "RH_GameInstanceSubsystem.h"
#include "Lobby/Widgets/RHMassInviteModal.h"

URHMassInviteModal::URHMassInviteModal(const FObjectInitializer& ObjectInitializer)
	: URHWidget(ObjectInitializer),
	RouteData(nullptr)
{ }

void URHMassInviteModal::RequestFriends(FRHInviteReceivePlayers OnReceivePlayers)
{
	if (!RouteData.IsValid())
	{
		UE_LOG(RallyHereStart, Error, TEXT("RHMassInviteModal::RequestFriends no route data"));
		return;
	}

	if (!ensure(OnReceivePlayers.IsBound()))
	{
		UE_LOG(RallyHereStart, Error, TEXT("RHMassInviteModal::RequestFriends no callback bound to receive players"));
		return;
	}
	
	FRHInviteShouldShowPlayer& ShouldShowDel = RouteData.Get()->OnShouldShow;

	if (!ensure(ShouldShowDel.IsBound()))
	{
		UE_LOG(RallyHereStart, Error, TEXT("RHMassInviteModal::RequestFriends no callback bound to determine who to show"));
		OnReceivePlayers.Execute({});
		return;
	}

	const auto* LobbyHud = Cast<ARHLobbyHUD>(MyHud.Get());
	if (LobbyHud == nullptr)
	{
		OnReceivePlayers.Execute({});
		return;
	}

	if (const auto FSS = GetRHFriendSubsystem())
	{
		const auto* RHPI = GetRHLocalPlayerSubsystem()->GetPlayerInfoSubsystem();
		
		FriendResults.Empty();
		for (const auto Friend : FSS->GetFriends())
		{
			if (Friend->AreFriends())
			{
				if (ShouldShowDel.Execute(Friend))
				{
					FriendResults.Add(Friend);
				}
				else if (RHPI)
				{
					const auto PlayerUuid = Friend->GetPlayerAndPlatformInfo().PlayerUuid;
					if (const URH_PlayerInfo* PlayerInfo = RHPI->GetPlayerInfo(PlayerUuid))
					{
						PlayerInfo->GetPresence()->RequestUpdate(false, FRH_OnRequestPlayerInfoSubobjectDelegate::CreateWeakLambda(this, [this, PlayerUuid, ShouldShowDel, OnReceivePlayers](bool bSuccess, URH_PlayerInfoSubobject* pPlayerPresence)
							{
								if (const auto FSS = GetRHFriendSubsystem())
								{
									URH_RHFriendAndPlatformFriend* Friend = FSS->GetFriendByUuid(PlayerUuid); 
									if (ShouldShowDel.Execute(Friend))
									{
										FriendResults.Add(Friend);
										OnReceivePlayers.Execute(FriendResults);
									}
								}
							}));
					}
				}
			}
		}

		OnReceivePlayers.Execute(FriendResults);
		return;
	}

	OnReceivePlayers.Execute({});
}


URH_LocalPlayerSubsystem* URHMassInviteModal::GetRHLocalPlayerSubsystem() const
{
	if (!MyHud.IsValid())
	{
		UE_LOG(RallyHereStart, Error, TEXT("[%s] Failed to get outer Hud"), ANSI_TO_TCHAR(__FUNCTION__));
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

	return LocalPlayer->GetSubsystem<URH_LocalPlayerSubsystem>();
}

URH_FriendSubsystem* URHMassInviteModal::GetRHFriendSubsystem() const
{
	URH_LocalPlayerSubsystem* RHSS = GetRHLocalPlayerSubsystem();
	if (!RHSS)
	{
		UE_LOG(RallyHereStart, Error, TEXT("[%s] RallyHere failed to get subsystem for controller"), ANSI_TO_TCHAR(__FUNCTION__));
		return nullptr;
	}

	return RHSS->GetFriendSubsystem();
}

bool URHMassInviteModal::UpdateRouteData()
{
	RouteData.Reset();
	if (MyHud == nullptr)
	{
		return false;
	}

	auto* ViewManager = MyHud->GetViewManager();
	if (ViewManager == nullptr)
	{
		return false;
	}

	UObject* Data = nullptr;
	if (ViewManager->GetPendingRouteData("MassInvite", Data))
	{
		RouteData = Cast<URHDataMassInviteBase>(Data);
	}
	return RouteData.IsValid();
}

bool URHMassInviteModal::GetShouldSelect(URH_RHFriendAndPlatformFriend* Friend)
{
	if (RouteData.IsValid())
	{
		if (auto* IndividualData = Cast<URHDataIndividualInviteSetup>(RouteData.Get()))
		{
			if (IndividualData->OnGetIsSelected.IsBound())
			{
				return IndividualData->OnGetIsSelected.Execute(Friend);
			}
		}
		else
		{
			return SelectedFriends.Contains(Friend);
		}
	}
	return false;
}

ERHInviteSelectResult URHMassInviteModal::SelectPlayer(URH_RHFriendAndPlatformFriend* Friend)
{
	if (RouteData.IsValid())
	{
		if (auto* IndividualData = Cast<URHDataIndividualInviteSetup>(RouteData.Get()))
		{
			if (IndividualData->OnSelect.IsBound())
			{
				return IndividualData->OnSelect.Execute(Friend);
			}
		}
		else if (auto* BatchData = Cast<URHDataBatchInviteSetup>(RouteData.Get()))
		{
			if (!SelectedFriends.Contains(Friend))
			{
				SelectedFriends.Add(Friend);
				return ERHInviteSelectResult::Selected;
			}
			else
			{
				SelectedFriends.Remove(Friend);
				return ERHInviteSelectResult::Deselected;
			}
		}
	}
	return ERHInviteSelectResult::NoChange;
}

void URHMassInviteModal::CloseScreen(ERHInviteCloseAction CloseAction)
{
	RemoveViewRoute("MassInvite");

	if (CloseAction == ERHInviteCloseAction::Submit)
	{
		if (RouteData.IsValid())
		{
			auto* BatchData = Cast<URHDataBatchInviteSetup>(RouteData.Get());
			if (BatchData != nullptr && BatchData->OnSelect.IsBound())
			{
				BatchData->OnSelect.Execute(SelectedFriends);
			}
		}
	}
	SelectedFriends.Empty();

	if (RouteData.IsValid())
	{
		RouteData.Get()->OnClose.ExecuteIfBound();
		RouteData.Reset();
	}

	ActiveSearchTerm = FText::GetEmpty();
}

void URHMassInviteModal::DoSearch(FText SearchTerm)
{
	if (SearchTerm.EqualTo(ActiveSearchTerm) || SearchTerm.EqualTo(FText::GetEmpty())) // prevent dupes/empty searches
	{
		return;
	}
	ActiveSearchTerm = SearchTerm;
	SearchResult.Empty();

	auto* RHLP = GetRHLocalPlayerSubsystem();
	if (RHLP != nullptr)
	{
		auto* RHPI = RHLP->GetPlayerInfoSubsystem();
		if (RHPI != nullptr)
		{
			// DO LOOK UP
			RHPI->LookupPlayer(SearchTerm.ToString(), FRH_PlayerInfoLookupPlayerDelegate::CreateWeakLambda(this, [this, SearchTerm](bool bSuccess, const TArray<URH_PlayerInfo*>& PlayerInfos) -> void
				{
					// Only display results if the results are for the last executed search
					if (ActiveSearchTerm.EqualTo(SearchTerm))
					{
						if (URH_FriendSubsystem* FSS = GetRHFriendSubsystem())
						{
							for (URH_PlayerInfo* PlayerInfo : PlayerInfos)
							{
								if (URH_PlayerPresence* pPresence = PlayerInfo->GetPresence())
								{
									PlayerInfo->GetPresence()->RequestUpdate(true, FRH_OnRequestPlayerInfoSubobjectDelegate::CreateWeakLambda(this, [this](bool bSuccess, URH_PlayerInfoSubobject* pPlayerPresence)
										{
											CheckAndAddPlayerToSearchResult(pPlayerPresence->GetPlayerInfo());
										}));
								}
							}
						}
					}
				}));
		}
	}

	OnSearchResultUpdated(SearchResult);
}

void URHMassInviteModal::CheckAndAddPlayerToSearchResult(URH_PlayerInfo* PlayerInfo)
{
	if (RouteData.IsValid())
	{
		FRHInviteShouldShowPlayer& ShouldShowDel = RouteData.Get()->OnShouldShow;
		if (!ensure(ShouldShowDel.IsBound()))
		{
			UE_LOG(RallyHereStart, Error, TEXT("URHMassInviteModal::CheckAndAddPlayerToSearchResult no callback bound to determine who to show"));
			return;
		}
		
		if (URH_FriendSubsystem* FSS = GetRHFriendSubsystem())
		{
			if (URH_RHFriendAndPlatformFriend* Friend = FSS->GetOrCreateFriend(PlayerInfo))
			{
				if (ShouldShowDel.Execute(Friend))
				{
					SearchResult.Add(Friend);
					OnSearchResultUpdated(SearchResult);
				}
			}
		}
	}
}

URHDataMassInviteBase::URHDataMassInviteBase(const FObjectInitializer& ObjectInitializer)
	: UObject(ObjectInitializer),
	Title(), ButtonLabel(),
	OnShouldShow(), OnClose()
{
}

/**** URHDataIndividualInviteSetup Route Data Item ****/

URHDataIndividualInviteSetup::URHDataIndividualInviteSetup(const FObjectInitializer& ObjectInitializer)
	: URHDataMassInviteBase(ObjectInitializer),
	OnGetIsSelected(), OnSelect()
{ }

URHDataIndividualInviteSetup* URHDataIndividualInviteSetup::SetCallbacks(FRHInviteGetIsSelected GetIsSelected, FRHInviteSelect Select, FRHInviteShouldShowPlayer ShouldShowPlayer, FRHInviteCancel Close)
{
	OnGetIsSelected = GetIsSelected;
	OnSelect = Select;
	OnShouldShow = ShouldShowPlayer;
	OnClose = Close;
	return this;
}


/**** URHBatchInviteSetup Route Data Item ****/

URHDataBatchInviteSetup::URHDataBatchInviteSetup(const FObjectInitializer& ObjectInitializer)
	: URHDataMassInviteBase(ObjectInitializer),
	OnSelect()
{}

URHDataBatchInviteSetup* URHDataBatchInviteSetup::SetCallbacks(FRHBatchSelect Select, FRHInviteShouldShowPlayer ShouldShowPlayer, FRHInviteCancel Cancel)
{
	OnSelect = Select;
	OnShouldShow = ShouldShowPlayer;
	OnClose = Cancel;
	return this;
}

