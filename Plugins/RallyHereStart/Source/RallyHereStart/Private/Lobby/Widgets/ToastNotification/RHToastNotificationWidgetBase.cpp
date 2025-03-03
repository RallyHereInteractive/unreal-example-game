// Copyright 2022-2023 Rally Here Interactive, Inc. All Rights Reserved.

#include "RallyHereStart.h"
#include "Lobby/HUD/RHLobbyHUD.h"
#include "RH_GameInstanceSubsystem.h"

#include "Lobby/Widgets/ToastNotification/RHToastNotificationWidgetBase.h"

URHToastNotificationWidgetBase::URHToastNotificationWidgetBase(const FObjectInitializer& ObjectInitializer /*= FObjectInitializer::Get()*/)
    : Super(ObjectInitializer)
{
    MaxToastNotification = 5;
    CurrentToastCount = 0;
    IsBusy = false;
	PostMatchToasts.Empty();
}

void URHToastNotificationWidgetBase::InitializeWidget_Implementation()
{
    Super::InitializeWidget_Implementation();

	if (URH_FriendSubsystem* RHFS = GetFriendSubsystem())
	{
		RHFS->FriendUpdatedDelegate.AddUObject(this, &URHToastNotificationWidgetBase::OnFriendUpdated);
	}

    //Bind Party Events
    if (URHPartyManager* PartyManager = GetPartyManager())
    {
		PartyManager->OnPartyInvitationReceived.AddDynamic(this, &URHToastNotificationWidgetBase::HandlePartyInviteReceived);
		PartyManager->OnPartyInvitationError.AddDynamic(this, &URHToastNotificationWidgetBase::HandlePartyInviteError);
		PartyManager->OnPartyMemberRemoved.AddDynamic(this, &URHToastNotificationWidgetBase::HandlePartyMemberKick);
		PartyManager->OnPartyMemberLeft.AddDynamic(this, &URHToastNotificationWidgetBase::HandlePartyMemberLeft);
		PartyManager->OnPendingPartyMemberAccepted.AddDynamic(this, &URHToastNotificationWidgetBase::HandlePartyMemberAdded);
		PartyManager->OnPartyInvitationAccepted.AddDynamic(this, &URHToastNotificationWidgetBase::HandlePartyInviteAccepted);
		PartyManager->OnPartyInvitationRejected.AddDynamic(this, &URHToastNotificationWidgetBase::HandlePartyInviteRejected);
		PartyManager->OnPartyInvitationSent.AddDynamic(this, &URHToastNotificationWidgetBase::HandlePartyInviteSent);
		PartyManager->OnPartyMemberPromoted.AddDynamic(this, &URHToastNotificationWidgetBase::HandlePartyMemberPromoted);
		PartyManager->OnPartyLocalPlayerLeft.AddDynamic(this, &URHToastNotificationWidgetBase::HandlePartyLocalPlayerLeft);
		PartyManager->OnPartyDisbanded.AddDynamic(this, &URHToastNotificationWidgetBase::HandlePartyDisbanded);
    }
}

FGuid URHToastNotificationWidgetBase::GetLocalPlayerUuid() const
{
	if (MyHud != nullptr)
	{
		return MyHud->GetLocalPlayerUuid();
	}

	return FGuid();
}

// Queue Toast Notification
void URHToastNotificationWidgetBase::StoreToastQueue(FToastData ToastNotification)
{
	UWorld* pWorld = GetWorld();
	if (pWorld == nullptr || pWorld->bIsTearingDown)
	{
		// Do not add a toast widget on tear down.
		return;
	}

    ToastQueue.Push(ToastNotification);
    OnToastReceived.Broadcast();
}

bool URHToastNotificationWidgetBase::GetNext(FToastData& Toast)
{
    if (ToastQueue.Num() > 0)
    {
        Toast = ToastQueue[0];
        ToastQueue.RemoveAt(0);
        return true;
    }

    return false;
}

void URHToastNotificationWidgetBase::ShowToastNotification()
{
    if (!IsBusy && ToastQueue.Num() > 0 && CurrentToastCount < MaxToastNotification)
    {
        FToastData QueuedToastNotification;
        if (GetNext(QueuedToastNotification))
        {
            OnToastNotificationReceived(QueuedToastNotification);
        }
    }
}

// resets toast notification
void URHToastNotificationWidgetBase::ClearNotificationQueue()
{
    ToastQueue.Empty();
}

void URHToastNotificationWidgetBase::ClearPostMatchQueue()
{
	PostMatchToasts.Empty();
}

URH_FriendSubsystem* URHToastNotificationWidgetBase::GetFriendSubsystem()
{
	if (MyHud.IsValid())
	{
		if (URH_LocalPlayerSubsystem* RHSS = MyHud->GetLocalPlayerSubsystem())
		{
			return RHSS->GetFriendSubsystem();
		}
	}

	return nullptr;
}

URHPartyManager* URHToastNotificationWidgetBase::GetPartyManager()
{
    if (MyHud.IsValid())
    {
        return MyHud->GetPartyManager();
    }

    return nullptr;
}

void URHToastNotificationWidgetBase::NotifyToastShown()
{
	check(CurrentToastCount < MaxToastNotification);
	++CurrentToastCount;
}

void URHToastNotificationWidgetBase::NotifyToastHidden()
{
	if (ensure(CurrentToastCount > 0))
	{
		--CurrentToastCount;
		ShowToastNotification();
	}
}

void URHToastNotificationWidgetBase::OnFriendUpdated(URH_RHFriendAndPlatformFriend* UpdatedFriend)
{
	UpdatedFriend->AcknowledgeFriendUpdate();
	if (UpdatedFriend->GetPreviousStatus() == UpdatedFriend->GetStatus())
	{
		return;
	}

	URH_PlayerInfoSubsystem* PSS = MyHud->GetPlayerInfoSubsystem();
	if (!PSS)
	{
		return;
	}

	URH_PlayerInfo* FriendPlayerInfo = PSS->GetOrCreatePlayerInfo(UpdatedFriend->GetRHPlayerUuid());
	if (!FriendPlayerInfo)
	{
		return;
	}
	
	EToastCategory ToastCategory;
	FText ToastMessage;

	switch (UpdatedFriend->GetStatus())
	{
	case ERHAPI_FriendshipStatus::FriendRequestSent:
		if (UpdatedFriend->IsFirstRHFriendUpdate())
		{
			return;
		}
		ToastCategory = EToastCategory::ETOAST_FRIEND;
		ToastMessage = NSLOCTEXT("RHFriendlist", "AddSuccessMsg", "Friend request sent to {0}");
		break;
	case ERHAPI_FriendshipStatus::FriendRequestPending:
		ToastCategory = EToastCategory::ETOAST_FRIEND;
		ToastMessage = NSLOCTEXT("RHFriendlist", "ReceiveRequest", "You have received a friend request from {0}");
		break;
	case ERHAPI_FriendshipStatus::FriendRequestDeclinedByOther:
		ToastCategory = EToastCategory::ETOAST_ERROR;
		ToastMessage = NSLOCTEXT("RHFriendlist", "FriendRejected", "{0} declined your friend invite.");
		break;
	case ERHAPI_FriendshipStatus::Friends:
		if (UpdatedFriend->IsFirstRHFriendUpdate())
		{
			return;
		}
		ToastCategory = EToastCategory::ETOAST_FRIEND;
		ToastMessage = NSLOCTEXT("RHFriendlist", "FriendAdded", "You are now friends with {0}.");
		break;
	default:
		return;
	}

	FString FriendDisplayName;
	if (FriendPlayerInfo->GetLastKnownDisplayName(FriendDisplayName))
	{
		HandleGetDisplayNameResponse(true, FriendDisplayName, FriendPlayerInfo, ToastCategory, ToastMessage);
	}
	else
	{
		FriendPlayerInfo->GetLastKnownDisplayNameAsync(FTimespan(), false, ERHAPI_Platform::Anon,
			FRH_PlayerInfoGetDisplayNameDelegate::CreateUObject(this, &URHToastNotificationWidgetBase::HandleGetDisplayNameResponse,
				FriendPlayerInfo,
				ToastCategory,
				ToastMessage));
	}
}

// Party: Invitation Sent
void URHToastNotificationWidgetBase::HandlePartyInviteSent(URH_PlayerInfo* Invitee)
{
	if (Invitee != nullptr)
	{
		FString InviteeDisplayName;
		if (Invitee->GetLastKnownDisplayName(InviteeDisplayName))
		{
			HandleGetDisplayNameResponse(true, InviteeDisplayName, Invitee, EToastCategory::ETOAST_PARTY, NSLOCTEXT("GeneralParty", "PartyInviteSent", "You’ve sent a Party Invitation to {0}."));
		}
		else
		{
			Invitee->GetLastKnownDisplayNameAsync(FTimespan(), false, ERHAPI_Platform::Anon,
				FRH_PlayerInfoGetDisplayNameDelegate::CreateUObject(this, &URHToastNotificationWidgetBase::HandleGetDisplayNameResponse,
					Invitee,
					EToastCategory::ETOAST_PARTY,
					NSLOCTEXT("GeneralParty", "PartyInviteSent", "You’ve sent a Party Invitation to {0}.")));
		}
	}
}

// Party: Invite Received
void URHToastNotificationWidgetBase::HandlePartyInviteReceived(URH_PlayerInfo* PartyInviter)
{
    if (PartyInviter != nullptr)
    {
		FString InviterDisplayName;
		if (PartyInviter->GetLastKnownDisplayName(InviterDisplayName))
		{
			HandleGetDisplayNameResponse(true, InviterDisplayName, PartyInviter, EToastCategory::ETOAST_PARTY, NSLOCTEXT("RHFriendlist", "Invitation", "{0} has invited you to a party."));
		}
		else
		{
			PartyInviter->GetLastKnownDisplayNameAsync(FTimespan(), false, ERHAPI_Platform::Anon,
				FRH_PlayerInfoGetDisplayNameDelegate::CreateUObject(this, &URHToastNotificationWidgetBase::HandleGetDisplayNameResponse, 
					PartyInviter, 
					EToastCategory::ETOAST_PARTY, 
					NSLOCTEXT("RHFriendlist", "Invitation", "{0} has invited you to a party.")));
		}
    }
}

// Party: Invitation Failed
void URHToastNotificationWidgetBase::HandlePartyInviteError(FText ErrorMsg)
{
    if (!ErrorMsg.IsEmpty())
    {
        FToastData PartyInviteErrorToast;
        PartyInviteErrorToast.ToastCategory = EToastCategory::ETOAST_ERROR;
        PartyInviteErrorToast.Message = ErrorMsg;

        StoreToastQueue(PartyInviteErrorToast);
    }
}

// Party: Player Leaves Party
void URHToastNotificationWidgetBase::HandlePartyLocalPlayerLeft()
{
    FToastData PartyMemberLeftToast;
    PartyMemberLeftToast.ToastCategory = EToastCategory::ETOAST_INFO;
    PartyMemberLeftToast.Message = FText(NSLOCTEXT("GeneralParty", "PartyLeftMsgLocal", "You have left the party."));

    StoreToastQueue(PartyMemberLeftToast);
}

void URHToastNotificationWidgetBase::HandlePartyMemberKick(const FGuid& PlayerId)
{
    if (PlayerId == GetLocalPlayerUuid())
    {
        FToastData PartyLeftToast;
        PartyLeftToast.ToastCategory = EToastCategory::ETOAST_INFO;
        PartyLeftToast.Message = FText(NSLOCTEXT("GeneralParty", "PartyKickedMsgLocal", "You have been kicked from the party."));

        StoreToastQueue(PartyLeftToast);
    }
}

void URHToastNotificationWidgetBase::HandlePartyMemberLeftGeneric()
{
    FToastData PartyLeftToast;
    PartyLeftToast.ToastCategory = EToastCategory::ETOAST_INFO;
    PartyLeftToast.Message = FText(NSLOCTEXT("GeneralParty", "PartyLeftMsgGeneric", "A player has left the party."));

    StoreToastQueue(PartyLeftToast);
}

void URHToastNotificationWidgetBase::HandlePartyMemberLeft(FRH_PartyMemberData PartyMemberData)
{
	URH_PlayerInfo* MemberPlayerInfo = PartyMemberData.PlayerData;
 	if (MemberPlayerInfo == nullptr || PartyMemberData.IsPending)
 	{
 		return;
 	}

	FString MemberDisplayName;
	if (MemberPlayerInfo->GetLastKnownDisplayName(MemberDisplayName))
	{
		HandleGetDisplayNameResponse(true, MemberDisplayName, MemberPlayerInfo, EToastCategory::ETOAST_INFO, NSLOCTEXT("GeneralParty", "PartyLeftMsgOther", "{0} has left the party."));
	}
	else
	{
		MemberPlayerInfo->GetLastKnownDisplayNameAsync(FTimespan(), false, ERHAPI_Platform::Anon,
			FRH_PlayerInfoGetDisplayNameDelegate::CreateUObject(this, &URHToastNotificationWidgetBase::HandleGetDisplayNameResponse, 
				MemberPlayerInfo, 
				EToastCategory::ETOAST_INFO, 
				NSLOCTEXT("GeneralParty", "PartyLeftMsgOther", "{0} has left the party.")));
	}
}
 
void URHToastNotificationWidgetBase::HandlePartyDisbanded()
{
 	FToastData PartyDisbandedToast;
 	PartyDisbandedToast.ToastCategory = EToastCategory::ETOAST_INFO;
 	PartyDisbandedToast.Message = FText(NSLOCTEXT("GeneralParty", "PartyDisbandedMsg", "The party has been disbanded."));
 
 	StoreToastQueue(PartyDisbandedToast);
}

// Party: Player Member Promoted
void URHToastNotificationWidgetBase::HandlePartyMemberPromoted(const FGuid& PlayerId)
{
    if (PlayerId.IsValid() && GetPartyManager())
    {
        FRH_PartyMemberData PromotedPartyMember = GetPartyManager()->GetPartyMemberByID(PlayerId);

        if (PlayerId == GetLocalPlayerUuid())
        {
			FToastData LocalPlayerPromotedToast;
			LocalPlayerPromotedToast.ToastCategory = EToastCategory::ETOAST_INFO;
			LocalPlayerPromotedToast.Message = FText(NSLOCTEXT("GeneralParty", "PromotePlayer", "You have been promoted to party leader"));
			StoreToastQueue(LocalPlayerPromotedToast);
        }
        else
        {
			URH_PlayerInfo* MemberPlayerInfo = PromotedPartyMember.PlayerData;
            if (MemberPlayerInfo != nullptr)
            {
				FString MemberDisplayName;
				if (MemberPlayerInfo->GetLastKnownDisplayName(MemberDisplayName))
				{
					HandleGetDisplayNameResponse(true, MemberDisplayName, MemberPlayerInfo, EToastCategory::ETOAST_INFO, NSLOCTEXT("GeneralParty", "PartyMemberPromoted", "{0} has been promoted to party leader."));
				}
				else
				{
					MemberPlayerInfo->GetLastKnownDisplayNameAsync(FTimespan(), false, ERHAPI_Platform::Anon,
						FRH_PlayerInfoGetDisplayNameDelegate::CreateUObject(this, &URHToastNotificationWidgetBase::HandleGetDisplayNameResponse, 
							MemberPlayerInfo, 
							EToastCategory::ETOAST_INFO, 
							NSLOCTEXT("GeneralParty", "PartyMemberPromoted", "{0} has been promoted to party leader.")));
				}
            }
        }
    }
}

// Party: Player Joins Party
void URHToastNotificationWidgetBase::HandlePartyMemberAdded(FRH_PartyMemberData PartyMemberData)
{
    if (PartyMemberData.PlayerData == nullptr)
    {
        UE_LOG(RallyHereStart, Warning, TEXT("No valid PlayerData on provided PartyMemberData struct."));
        return;
    }

    if (GetPartyManager() != nullptr)
    {
        if (GetPartyManager()->GetPartyMembers().Num() > 1)
        {
			if (PartyMemberData.PlayerData->GetRHPlayerUuid() == GetLocalPlayerUuid())
            {
				FToastData LocalPlayerJoinToast;
				LocalPlayerJoinToast.ToastCategory = EToastCategory::ETOAST_PARTY;
				LocalPlayerJoinToast.Message = FText(NSLOCTEXT("GeneralParty", "JoinParty", "You have joined the Party."));
				StoreToastQueue(LocalPlayerJoinToast);
            }
            else
            {
				URH_PlayerInfo* MemberPlayerInfo = PartyMemberData.PlayerData;
				if (MemberPlayerInfo != nullptr)
				{
					FString MemberDisplayName;
					if (MemberPlayerInfo->GetLastKnownDisplayName(MemberDisplayName))
					{
						HandleGetDisplayNameResponse(true, MemberDisplayName, MemberPlayerInfo, EToastCategory::ETOAST_PARTY, NSLOCTEXT("GeneralParty", "PlayerJoinsParty", "{0} has joined the Party."));
					}
					else
					{
						MemberPlayerInfo->GetLastKnownDisplayNameAsync(FTimespan(), false, ERHAPI_Platform::Anon,
							FRH_PlayerInfoGetDisplayNameDelegate::CreateUObject(this, &URHToastNotificationWidgetBase::HandleGetDisplayNameResponse, 
								MemberPlayerInfo, 
								EToastCategory::ETOAST_PARTY, 
								NSLOCTEXT("GeneralParty", "PlayerJoinsParty", "{0} has joined the Party.")));
					}
				}
            }
        }
    }
}
void URHToastNotificationWidgetBase::HandlePartyInviteAccepted()
{
    FToastData PartyAcceptedToast;
    PartyAcceptedToast.ToastCategory = EToastCategory::ETOAST_PARTY;
    PartyAcceptedToast.Message = FText(NSLOCTEXT("GeneralParty", "JoinParty", "You have joined the Party."));

    StoreToastQueue(PartyAcceptedToast);
}

// Party: Player Declines Party Invite
void URHToastNotificationWidgetBase::HandlePartyInviteRejected()
{
    FToastData PartyRejectedToast;
    PartyRejectedToast.ToastCategory = EToastCategory::ETOAST_INFO;
    PartyRejectedToast.Message = FText(NSLOCTEXT("GeneralParty", "RejectedParty", "You have declined the party invitation."));

    StoreToastQueue(PartyRejectedToast);
}

void URHToastNotificationWidgetBase::HandleGetDisplayNameResponse(bool bSuccess, const FString& DisplayName, URH_PlayerInfo* PartyInviter, EToastCategory ToastCategory, FText Message)
{
	FString NameToUse = FString();
	if (bSuccess)
	{
		NameToUse = DisplayName;
	}
	else
	{
		if (PartyInviter == nullptr)
		{
			return;
		}

		NameToUse = PartyInviter->GetRHPlayerUuid().ToString();
	}

	FToastData PartyInviteToast;
	PartyInviteToast.ToastCategory = ToastCategory;
	PartyInviteToast.Message = FText::Format(Message, FText::FromString(DisplayName));
	StoreToastQueue(PartyInviteToast);
}