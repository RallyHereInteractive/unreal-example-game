// Copyright 2022-2023 Rally Here Interactive, Inc. All Rights Reserved.

#include "RallyHereStart.h"
#include "GameFramework/RHGameInstance.h"
#include "Shared/HUD/RHHUDCommon.h"
#include "Lobby/HUD/RHLobbyHUD.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/SlateBlueprintLibrary.h"
#include "Shared/Widgets/RHContextMenu.h"
#include "DataFactories/RHQueueDataFactory.h"
#include "Managers/RHPopupManager.h"
#include "RH_GameInstanceSubsystem.h"
#include "RHUIBlueprintFunctionLibrary.h"

URH_PlayerInfoSubsystem* URHContextMenu::GetRHPlayerInfoSubsystem() const
{
	ARHHUDCommon* Hud = MyHud.Get();
	if (!Hud)
	{
		UE_LOG(RallyHereStart, Error, TEXT("[%s] Failed to get outer Hud"), ANSI_TO_TCHAR(__FUNCTION__));
		return nullptr;
	}

	return Hud->GetPlayerInfoSubsystem();
}

URH_FriendSubsystem* URHContextMenu::GetRHFriendSubsystem() const
{
	ARHHUDCommon* Hud = MyHud.Get();
	if (!Hud)
	{
		UE_LOG(RallyHereStart, Error, TEXT("[%s] Failed to get outer Hud"), ANSI_TO_TCHAR(__FUNCTION__));
		return nullptr;
	}

	URH_LocalPlayerSubsystem* RHSS = Hud->GetLocalPlayerSubsystem();
	if (!RHSS)
	{
		UE_LOG(RallyHereStart, Error, TEXT("[%s] RallyHere failed to get subsystem for controller"), ANSI_TO_TCHAR(__FUNCTION__));
		return nullptr;
	}

	return RHSS->GetFriendSubsystem();
}

URHPartyManager* URHContextMenu::GetPartyManager() const
{
	if (MyHud.IsValid())
	{
		return MyHud->GetPartyManager();
	}
	else
	{
		UE_LOG(RallyHereStart, Warning, TEXT("URHContextMenu::GetPartyManager Warning: MyHud is not currently valid."));
	}
	return nullptr;
}

URHQueueDataFactory* URHContextMenu::GetQueueDataFactory() const
{
	if (MyHud.IsValid())
	{
		if (ARHLobbyHUD* RHLobbyHUD = Cast<ARHLobbyHUD>(MyHud))
		{
			return RHLobbyHUD->GetQueueDataFactory();
		}
		else
		{
			UE_LOG(RallyHereStart, Warning, TEXT("URHContextMenu::GetQueueDataFactory Warning: MyHud failed to cast to ARHLobbyHUD."));
		}
	}
	else
	{
		UE_LOG(RallyHereStart, Warning, TEXT("URHContextMenu::GetQueueDataFactory Warning: MyHud is not currently valid."));
	}
	return nullptr;
}

URHContextMenu::URHContextMenu(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	CurrentFriend = nullptr;
	ContextMenuButtons.Empty();
	CachedQueuedOrInMatch = -1;
	bCachedReportedPlayer = false;
}

void URHContextMenu::InitializeWidget_Implementation()
{
	Super::InitializeWidget_Implementation();

	if (URHQueueDataFactory* QueueDataFactory = GetQueueDataFactory())
	{
		QueueDataFactory->OnQueueStatusChange.AddDynamic(this, &URHContextMenu::HandleOnQueueStatusChange);
	}
}

void URHContextMenu::HandleOnQueueStatusChange(ERH_MatchStatus QueueStatus)
{
	SetOptionsActive();
}

void URHContextMenu::SetCurrentFriend(class URH_RHFriendAndPlatformFriend* Friend)
{
	if (Friend != nullptr)
	{
		CurrentFriend = Friend;
	}
}

void URHContextMenu::AddContextMenuButton(class URHContextMenuButton* ContextButton)
{
	if (ContextButton != nullptr)
	{
		ContextMenuButtons.Add(ContextButton);
	}
}

void URHContextMenu::RemoveContextMenuButton(class URHContextMenuButton* ContextButton)
{
	if (ContextButton != nullptr && ContextMenuButtons.Find(ContextButton))
	{
		ContextMenuButtons.Remove(ContextButton);
	}
}

void URHContextMenu::ClearAllContextMenuButton()
{
	ContextMenuButtons.Empty();
}

class URHContextMenuButton* URHContextMenu::GetContextButtonByEnum(EPlayerContextOptions ContextOption)
{
	for (class URHContextMenuButton* ContextButton : ContextMenuButtons)
	{
		if (ContextButton->GetContextOption() == ContextOption)
		{
			return ContextButton;
		}
	}

	return nullptr;
}

bool URHContextMenu::OnOptionSelected(EPlayerContextOptions ContextOption)
{
	if (CurrentFriend == nullptr)
	{
		return false;
	}

	if (CurrentFriend->GetRHPlayerUuid().IsValid())
	{
		return ExecuteSelectedOption(ContextOption);
	}
	
	CurrentFriend->GetRHPlayerUuidAsync(FRH_GetRHPlayerUuidDelegate::CreateLambda([this, ContextOption](bool bSuccess, const FGuid& PlayerUuid)
	{
		if (CurrentFriend && bSuccess && PlayerUuid == CurrentFriend->GetRHPlayerUuid())
		{
			const bool Success = ExecuteSelectedOption(ContextOption);
			OnContextOptionCompleted.Broadcast(Success);
		}
		else
		{
			OnContextOptionCompleted.Broadcast(false);
		}
	}));

	// Return false as we have to do an async action and do not know if it will succeed or not. if it succeeds; event is raised to close the context menu
	return false;
}

bool URHContextMenu::ExecuteSelectedOption(EPlayerContextOptions ContextOption)
{
	bool bWasSuccessful = false;

	if (CurrentFriend != nullptr)
	{
		switch(ContextOption)
		{
			case EPlayerContextOptions::AddFriend:
			case EPlayerContextOptions::AddRHFriend:
				bWasSuccessful = AttemptFriendAction(EFriendAction::AddFriend);
				break;
			case EPlayerContextOptions::PartyInvite:
				bWasSuccessful = AttemptInviteToParty();
				break;
			case EPlayerContextOptions::LobbySwapTeam:
				bWasSuccessful = AttemptQueueAction(EQueueDataFactoryAction::SwapPlayerCustomMatch);
				break;
			case EPlayerContextOptions::LobbyKickPlayer:
				bWasSuccessful = AttemptQueueAction(EQueueDataFactoryAction::KickFromCustomMatch);
				break;
			case EPlayerContextOptions::LobbyPromotePlayer:
				bWasSuccessful = AttemptQueueAction(EQueueDataFactoryAction::SwitchHostCustomMatch);
				break;
			case EPlayerContextOptions::PartyKick:
				bWasSuccessful = AttemptPartyAction(EPartyManagerAction::KickMember);
				break;
			case EPlayerContextOptions::ViewPlatformProfile:
				bWasSuccessful = CurrentFriend != nullptr && CurrentFriend->ViewPlatformProfile();
				break;
			case EPlayerContextOptions::RemoveFriend:
				bWasSuccessful = AttemptFriendAction(EFriendAction::RemoveFriend);
				break;
			case EPlayerContextOptions::CancelRequest:
				bWasSuccessful = AttemptFriendAction(EFriendAction::CancelFriendRequest);
				break;
			case EPlayerContextOptions::AcceptFriendRequest:
				bWasSuccessful = AttemptFriendAction(EFriendAction::AcceptFriendRequest);
				break;
			case EPlayerContextOptions::RejectFriendRequest:
				bWasSuccessful = AttemptFriendAction(EFriendAction::RejectFriendRequest);
				break;
			case EPlayerContextOptions::PromotePartyLeader:
				bWasSuccessful = AttemptPartyAction(EPartyManagerAction::PromoteToLeader);
				break;
			case EPlayerContextOptions::AcceptPartyInvite:
				bWasSuccessful = AttemptPartyAction(EPartyManagerAction::AcceptInvite);
				break;
			case EPlayerContextOptions::DeclinePartyInvite:
				bWasSuccessful = AttemptPartyAction(EPartyManagerAction::DenyInvite);
				break;
			case EPlayerContextOptions::LeaveParty:
				bWasSuccessful = AttemptPartyAction(EPartyManagerAction::LeaveParty);
				break;
			case EPlayerContextOptions::Mute:
				bWasSuccessful = AttemptMute(true);
				break;
			case EPlayerContextOptions::Unmute:
				bWasSuccessful = AttemptMute(false);
				break;
			case EPlayerContextOptions::ReportPlayer:
				bWasSuccessful = AttemptReportPlayer();
				break;
			case EPlayerContextOptions::IgnorePlayer:
			{
				bWasSuccessful = AttemptIgnorePlayer(true);

				if (bWasSuccessful && !IsMuted())
				{
					if (MenuContext == EPlayerContextMenuContext::CustomLobby || (IsPlayerInParty() && !IsPlayerPendingPartyInvite()) || MenuContext == EPlayerContextMenuContext::InGame)
					{
						// When we ignore a player go ahead and mute them too
						AttemptMute(true);
					}
				}
			}
				break;
			case EPlayerContextOptions::UnignorePlayer:
				bWasSuccessful = AttemptIgnorePlayer(false);
				break;
			default:
				bWasSuccessful = false;
				break;
		}
	}
	else
	{
		UE_LOG(RallyHereStart, Warning, TEXT("[%s] Warning: CurrentFriend is not currently valid."), ANSI_TO_TCHAR(__FUNCTION__));
	}

	return bWasSuccessful;
}

bool URHContextMenu::AttemptFriendAction(EFriendAction FriendAction)
{
	bool bWasSuccessful = false;
	if (CurrentFriend != nullptr)
	{
		if (CurrentFriend->GetRHPlayerUuid().IsValid())
		{
			bWasSuccessful = PerformFriendAction(FriendAction, CurrentFriend->GetRHPlayerUuid());
		}
		else if (CurrentFriend->GetPlayerAndPlatformInfo().PlayerPlatformId.IsValid())
		{
			if (const auto PSS = GetRHPlayerInfoSubsystem())
			{
				PSS->LookupPlayerByPlatformUserId(CurrentFriend->GetPlayerAndPlatformInfo().PlayerPlatformId, FRH_PlayerInfoLookupPlayerDelegate::CreateLambda([this, FriendAction](bool bSuccess, const TArray<URH_PlayerInfo*>& PlayerInfos)
					{
						if (bSuccess && PlayerInfos.IsValidIndex(0) && PlayerInfos[0] != nullptr)
						{
							PerformFriendAction(FriendAction, PlayerInfos[0]->GetRHPlayerUuid());
						}
					}));

				// Assume that we'll eventually succeed, because we don't know any better
				bWasSuccessful = true;
			}
		}
	}
	
	return bWasSuccessful;
}

bool URHContextMenu::PerformFriendAction(EFriendAction FriendAction, FGuid FriendUuid)
{
	bool bWasSuccessful = false;
	
	switch (FriendAction)
	{
	case EFriendAction::AddFriend:
		bWasSuccessful = AddRHFriend(FriendUuid);
		break;
	case EFriendAction::RemoveFriend:
		bWasSuccessful = PromptForFriendRemoval();
		break;
	case EFriendAction::CancelFriendRequest:
		bWasSuccessful = RemoveFriendRequest(FriendUuid);
		break;
	case EFriendAction::AcceptFriendRequest:
		bWasSuccessful = AddRHFriend(FriendUuid);
		break;
	case EFriendAction::RejectFriendRequest:
		bWasSuccessful = RemoveFriendRequest(FriendUuid);
		break;
	default:
		bWasSuccessful = false;
		break;
	}

	return bWasSuccessful;
}

bool URHContextMenu::PromptForFriendRemoval()
{
	if (MyHud.IsValid() && CurrentFriend != nullptr)
	{
		auto Handler = FRH_PlayerInfoGetDisplayNameDelegate::CreateWeakLambda(this, [this](bool bSuccess, const FString& DisplayName)
			{
				URHPopupManager* popup = MyHud->GetPopupManager();
				if (popup == nullptr)
				{
					UE_LOG(RallyHereStart, Error, TEXT("[%s] Need an implementation for PopupManager."), ANSI_TO_TCHAR(__FUNCTION__));
					return;
				}

				FText BuddyName = bSuccess ? FText::FromString(DisplayName) : FText::FromString(CurrentFriend->GetRHPlayerUuid().ToString());

				FRHPopupConfig removePopupParams;
				removePopupParams.Header = NSLOCTEXT("GeneralFriend", "RemoveFriend", "Remove Friend");
				removePopupParams.Description = FText::Format(NSLOCTEXT("GeneralFriend", "RemoveFriendMsg", "Are you sure you want to remove {0} from your friend list?"), BuddyName);
				removePopupParams.IsImportant = true;
				removePopupParams.CancelAction.AddDynamic(popup, &URHPopupManager::OnPopupCanceled);

				FRHPopupButtonConfig& removeConfirmBtn = (removePopupParams.Buttons[removePopupParams.Buttons.Add(FRHPopupButtonConfig())]);
				removeConfirmBtn.Label = NSLOCTEXT("General", "Confirm", "Confirm");
				removeConfirmBtn.Type = ERHPopupButtonType::Confirm;
				removeConfirmBtn.Action.AddDynamic(this, &URHContextMenu::RemoveRHFriend);
				FRHPopupButtonConfig& removeCancelBtn = (removePopupParams.Buttons[removePopupParams.Buttons.Add(FRHPopupButtonConfig())]);
				removeCancelBtn.Label = NSLOCTEXT("General", "Cancel", "Cancel");
				removeCancelBtn.Type = ERHPopupButtonType::Cancel;
				removeCancelBtn.Action.AddDynamic(popup, &URHPopupManager::OnPopupCanceled);

				popup->AddPopup(removePopupParams);
			});

		CurrentFriend->GetLastKnownDisplayNameAsync(FTimespan(), false, ERHAPI_Platform::Anon, Handler);
		return true;
	}

	return false;
}

bool URHContextMenu::AddRHFriend(const FGuid& PlayerUuid) const
{
	if (auto RhSS = GetRHFriendSubsystem())
	{
		if (RhSS->AddFriend(PlayerUuid))
		{
			return true;
		}

		UE_LOG(RallyHereStart, Error, TEXT("[%s] Failed to Add friend"), ANSI_TO_TCHAR(__FUNCTION__));
	}
	return false;
}

bool URHContextMenu::RemoveFriendRequest(const FGuid& PlayerUuid) const
{
	if (auto RhSS = GetRHFriendSubsystem())
	{
		if (RhSS->RemoveFriend(PlayerUuid))
		{
			return true;
		}

		UE_LOG(RallyHereStart, Error, TEXT("[URHContextMenu::RemoveRHFriend] Failed to Remove friend"));
	}
	return false;
}

void URHContextMenu::RemoveRHFriend()
{
	RemoveFriendRequest(CurrentFriend->GetRHPlayerUuid());
}

bool URHContextMenu::AttemptPartyAction(EPartyManagerAction PartyAction)
{
	bool bWasSuccessful = false;

	if (CurrentFriend != nullptr)
	{
		if (URHPartyManager* PartyManager = GetPartyManager())
		{
			switch(PartyAction)
			{
				case EPartyManagerAction::KickMember:
					PartyManager->UIX_KickMemberFromParty(CurrentFriend->GetRHPlayerUuid());
					bWasSuccessful = true;
					break;
				case EPartyManagerAction::PromoteToLeader:
					PartyManager->UIX_PromoteMemberToLeader(CurrentFriend->GetRHPlayerUuid());
					bWasSuccessful = true;
					break;
				case EPartyManagerAction::AcceptInvite:
					PartyManager->UIX_AcceptPartyInvitation();
					bWasSuccessful = true;
					break;
				case EPartyManagerAction::DenyInvite:
					PartyManager->UIX_DenyPartyInvitation();
					bWasSuccessful = true;
					break;
				case EPartyManagerAction::LeaveParty:
					PartyManager->UIX_LeaveParty();
					bWasSuccessful = true;
					break;
				default:
					bWasSuccessful = false;
					break;
			}
		}
	}

	return bWasSuccessful;
}

bool URHContextMenu::AttemptQueueAction(EQueueDataFactoryAction QueueAction)
{
	bool bWasSuccessful = false;

	if (CurrentFriend != nullptr)
	{
		if (URHQueueDataFactory* QueueDataFactory = GetQueueDataFactory())
		{
			// For the team swap action, might need a better way
			int32 CurrentTeam = QueueDataFactory->GetPlayerTeamId(CurrentFriend->GetRHPlayerUuid());
			int32 TeamToSwapTo = CurrentTeam == 0 ? 1 : 0;

			switch(QueueAction)
			{
				case EQueueDataFactoryAction::SwapPlayerCustomMatch:
					QueueDataFactory->SetPlayerTeamCustomMatch(CurrentFriend->GetRHPlayerUuid(), TeamToSwapTo);
					bWasSuccessful = true;
					break;
				case EQueueDataFactoryAction::KickFromCustomMatch:
					QueueDataFactory->KickFromCustomMatch(CurrentFriend->GetRHPlayerUuid());
					bWasSuccessful = true;
					break;
				case EQueueDataFactoryAction::SwitchHostCustomMatch:
					QueueDataFactory->PromoteToCustomMatchHost(CurrentFriend->GetRHPlayerUuid());
					bWasSuccessful = true;
					break;
				default:
					bWasSuccessful = false;
					break;
			}
		}
	}

	return bWasSuccessful;
}

bool URHContextMenu::AttemptInviteToParty()
{
	if (URHPartyManager* PartyManager = GetPartyManager())
	{
		if (CurrentFriend != nullptr)
		{
			PartyManager->UIX_InviteMemberToParty(CurrentFriend->GetRHPlayerUuid());
			return true;
		}
	}
	return false;
}

bool URHContextMenu::AttemptMute(bool bMute)
{
	if (CurrentFriend != nullptr)
	{
		if (MyHud.IsValid())
		{
			MyHud->MutePlayer(CurrentFriend->GetRHPlayerUuid(), bMute);
			return true;
		}
	}
	return false;
}

bool URHContextMenu::AttemptReportPlayer()
{
	OnReportPlayer.Broadcast(CurrentFriend);
	return true;
}

bool URHContextMenu::AttemptIgnorePlayer(bool bIgnore)
{

	if (CurrentFriend != nullptr)
	{
		if (auto RhSS = GetRHFriendSubsystem())
		{
			if (bIgnore)
			{
				return RhSS->BlockPlayer(CurrentFriend->GetRHPlayerUuid());
			}
			else
			{
				return RhSS->UnblockPlayer(CurrentFriend->GetRHPlayerUuid());
			}
		}
	}

	return false;
}

FVector2D URHContextMenu::SetMenuPosition(class URHWidget* WidgetToMove, FMargin WidgetPadding, EViewSide side, float menuWidth, float menuHeight)
{
	FVector2D MoveTo;

	const FGeometry& WidgetGeo = WidgetToMove->GetCachedGeometry();
	FVector2D WidgetPixelPosition, WidgetViewportPosition, AbsoluteRectMin, AbsoluteRectMax;
	FVector2D ViewportSize = UWidgetLayoutLibrary::GetViewportSize(this);

	FVector2D WidgetLocalSize = USlateBlueprintLibrary::GetLocalSize(WidgetGeo);
	FVector2D WidgetAbsoluteMin = USlateBlueprintLibrary::LocalToAbsolute(WidgetGeo, FVector2D(0,0));
	FVector2D WidgetAbsoluteMax = USlateBlueprintLibrary::LocalToAbsolute(WidgetGeo, WidgetLocalSize);

	USlateBlueprintLibrary::LocalToViewport(WidgetToMove, WidgetGeo, FVector2D(0.5,0), WidgetPixelPosition, WidgetViewportPosition);
	AbsoluteRectMin = FVector2D(WidgetAbsoluteMin.X - WidgetPadding.Left, WidgetAbsoluteMin.Y - WidgetPadding.Top);
	AbsoluteRectMax = FVector2D(WidgetAbsoluteMax.X + WidgetPadding.Right, WidgetAbsoluteMax.Y + WidgetPadding.Bottom);

	if (side == EViewSide::None)
	{
		MenuViewSide = (WidgetViewportPosition.X < (ViewportSize.X/2)) ? EViewSide::Right : EViewSide::Left;
	}
	else
	{
		MenuViewSide = side;
	}

	return CalcMenuPosition(AbsoluteRectMin, AbsoluteRectMax, menuWidth, menuHeight);
}

FVector2D URHContextMenu::CalcMenuPosition(FVector2D WidgetAbsoluteMin, FVector2D WidgetAbsoluteMax, float menuWidth, float menuHeight)
{
	const FGeometry& ScreenGeo = this->GetTickSpaceGeometry();
	FVector2D ScreenLocalSize = USlateBlueprintLibrary::GetLocalSize(ScreenGeo);
	FVector2D AbsoluteCoordinate = FVector2D(0, 0), AbsoluteToLocal = FVector2D(0, 0);
	float PositionX = 0.0f, PositionY = 0.0f;
	float PosMin = 10.f;
	float PosXMax = (ScreenLocalSize.X - menuWidth) - PosMin;
	float PosYMax = (ScreenLocalSize.Y - menuHeight) - 140.0f;

	if (MenuViewSide == EViewSide::Left)
	{
		AbsoluteCoordinate = FVector2D(WidgetAbsoluteMin.X, WidgetAbsoluteMin.Y);
	}
	else if (MenuViewSide == EViewSide::Right)
	{
		AbsoluteCoordinate = FVector2D(WidgetAbsoluteMax.X, WidgetAbsoluteMin.Y);
	}

	AbsoluteToLocal = USlateBlueprintLibrary::AbsoluteToLocal(ScreenGeo, AbsoluteCoordinate);

	if (MenuViewSide == EViewSide::Left)
	{
		PositionX = FMath::Clamp((AbsoluteToLocal.X - menuWidth), PosMin, PosXMax);
	}
	else if (MenuViewSide == EViewSide::Right)
	{
		PositionX = FMath::Clamp(AbsoluteToLocal.X, PosMin, (ScreenLocalSize.X - menuWidth));
	}

	float PosYValue, LocalSizeY, RepositionYValue;
	LocalSizeY = AbsoluteToLocal.Y + menuHeight;
	RepositionYValue = AbsoluteToLocal.Y - (ScreenLocalSize.Y - LocalSizeY);
	PosYValue = (LocalSizeY > ScreenLocalSize.Y) ? RepositionYValue : AbsoluteToLocal.Y;
	PositionY = FMath::Clamp(PosYValue, PosMin, PosYMax);

	return FVector2D(PositionX, PositionY);
}

void URHContextMenu::SetOptionsActive()
{
	bool bContextOptionsUpdated = false;
	if (URHQueueDataFactory* QueueDataFactory = GetQueueDataFactory())
	{
		const bool bIsInQueueOrMatch = QueueDataFactory->GetCurrentQueueMatchState() >= ERH_MatchStatus::Queued;
		const int32 IsInQueueOrMatch = bIsInQueueOrMatch ? 1 : 0;
		if (CachedQueuedOrInMatch == -1 || IsInQueueOrMatch != CachedQueuedOrInMatch)
		{
			CachedQueuedOrInMatch = IsInQueueOrMatch;
			SetContextButtonActive(EPlayerContextOptions::PartyInvite, !bIsInQueueOrMatch);
			bContextOptionsUpdated = true;
		}
	}

	const bool bHasReportedPlayer = HasReportedPlayer();
	if (bHasReportedPlayer != bCachedReportedPlayer)
	{
		bCachedReportedPlayer = bHasReportedPlayer;
		SetContextButtonActive(EPlayerContextOptions::ReportPlayer, !bHasReportedPlayer);
		bContextOptionsUpdated = true;
	}

	if (bContextOptionsUpdated)
	{
		OnContextOptionsUpdated.Broadcast();
	}
}

void URHContextMenu::SetContextButtonActive(EPlayerContextOptions ContextOption, bool IsActive)
{
	URHContextMenuButton* ContextButton = GetContextButtonByEnum(ContextOption);
	if (ContextButton != nullptr)
	{
		ContextButton->HandleActive(IsActive);
	}
}

void URHContextMenu::SetOptionsVisibility()
{
	if (ContextMenuButtons.Num() > 0)
	{
		for (class URHContextMenuButton* const ContextMenuButton : ContextMenuButtons)
		{
			SetContextButtonVisibility(ContextMenuButton);
		}

		SetOptionsActive();
	}
}

void URHContextMenu::SetContextButtonVisibility(class URHContextMenuButton* ContextButton)
{
	bool bIsVisible = false;
	EPlayerContextOptions ContextOption = ContextButton->GetContextOption();

	if (CurrentFriend != nullptr)
	{
		if (ContextOption == EPlayerContextOptions::LobbyKickPlayer ||
			ContextOption == EPlayerContextOptions::LobbyPromotePlayer ||
			ContextOption == EPlayerContextOptions::LobbySwapTeam)
		{
			bIsVisible = IsLobbyOptionVisible(ContextOption);
		}
		else
		{
			// For use in case Mute and Unmute
			const bool bCanMuteInParty = IsPlayerInParty() && !IsPlayerPendingPartyInvite();
			const bool bBlockedInPlatform = IsBlockedByPlatform();
			switch(ContextOption)
			{
				case EPlayerContextOptions::PartyInvite:
					bIsVisible = CanInvitePlayer() &&
						!IsPlayerInParty() &&
						IsPlayerOnline() &&
						!IsPartyFull() &&
						!IsLocalPlayer() &&
						!IsIgnored() && 
						!IsBlockedByRH() &&
						!bBlockedInPlatform &&
						MenuContext != EPlayerContextMenuContext::InGame &&
						MenuContext != EPlayerContextMenuContext::CustomLobby;
					break;
				case EPlayerContextOptions::PartyKick:
					bIsVisible = IsPlayerInParty() &&
						IsPartyLeader() &&
						!IsLocalPlayer() &&
						MenuContext != EPlayerContextMenuContext::InGame;
					break;
				case EPlayerContextOptions::AddRHFriend:
					bIsVisible = IsCrossplayEnabled() &&
						!IsRHFriend() &&
						!IsRequestingFriend() &&
						!IsPendingFriend() &&
						!IsLocalPlayer() &&
						!IsIgnored() &&
						!IsBlockedByRH() &&
						!bBlockedInPlatform &&
						MenuContext != EPlayerContextMenuContext::InGame;
					break;
				case EPlayerContextOptions::RemoveFriend:
					bIsVisible = IsRHFriend() &&
						IsCrossplayEnabled() &&
						!IsPendingFriend() &&
						!IsLocalPlayer() &&
						!IsIgnored() &&
						!IsBlockedByRH() &&
						!bBlockedInPlatform &&
						MenuContext != EPlayerContextMenuContext::InGame;
					break;
				case EPlayerContextOptions::CancelRequest:
					bIsVisible = IsPendingFriend() && MenuContext != EPlayerContextMenuContext::InGame;
					break;
				case EPlayerContextOptions::AcceptFriendRequest:
				case EPlayerContextOptions::RejectFriendRequest:
					bIsVisible = IsRequestingFriend() && MenuContext != EPlayerContextMenuContext::InGame;
					break;
				case EPlayerContextOptions::PromotePartyLeader:
					bIsVisible = IsPartyLeader() &&
						IsPlayerInParty() &&
						!IsPlayerPendingPartyInvite() &&
						!IsLocalPlayer() &&
						MenuContext != EPlayerContextMenuContext::InGame;
					break;
				case EPlayerContextOptions::LeaveParty:
					bIsVisible = IsLocalPlayer() && 
						IsInParty() && 
						MenuContext != EPlayerContextMenuContext::InGame;
					break;
				case EPlayerContextOptions::AcceptPartyInvite:
				case EPlayerContextOptions::DeclinePartyInvite:
					bIsVisible = HasPlayerInvited() && MenuContext != EPlayerContextMenuContext::InGame;
					break;
				case EPlayerContextOptions::ViewPlatformProfile:
					bIsVisible = CurrentFriend->CanViewPlatformProfile();
					break;
				case EPlayerContextOptions::Mute:
					bIsVisible = (((MenuContext == EPlayerContextMenuContext::CustomLobby || MenuContext == EPlayerContextMenuContext::InGame) && IsInVoiceChannel()) || bCanMuteInParty) &&
						!IsMuted() &&
						!IsLocalPlayer();
					break;
				case EPlayerContextOptions::Unmute:
					bIsVisible = (((MenuContext == EPlayerContextMenuContext::CustomLobby || MenuContext == EPlayerContextMenuContext::InGame) && IsInVoiceChannel()) || bCanMuteInParty) &&
						IsMuted() &&
						!IsLocalPlayer();
					break;
				case EPlayerContextOptions::ReportPlayer:
					bIsVisible = bAllowReportPlayer && !IsLocalPlayer();
					break;
				case EPlayerContextOptions::IgnorePlayer:
					bIsVisible = !IsIgnored() && !IsBlockedByRH() && !bBlockedInPlatform && !IsLocalPlayer();
					break;
				case EPlayerContextOptions::UnignorePlayer:
					bIsVisible = IsIgnored() && IsBlockedByRH() && !bBlockedInPlatform && !IsLocalPlayer();
					break;
				default:
					break;
			}
		}

		ContextButton->HandleVisibility(bIsVisible);
	}
}

bool URHContextMenu::IsLobbyOptionVisible(EPlayerContextOptions ContextOption)
{
	FGuid PlayerId = CurrentFriend->GetRHPlayerUuid();

	if (MenuContext == EPlayerContextMenuContext::CustomLobby)
	{
		if (URHQueueDataFactory* QueueDataFactory = GetQueueDataFactory())
		{
			if (ContextOption == EPlayerContextOptions::LobbyKickPlayer)
			{
				return QueueDataFactory->CanLocalPlayerKickCustomLobbyPlayer(PlayerId);
			}

			if (ContextOption == EPlayerContextOptions::LobbySwapTeam)
			{
				return QueueDataFactory->CanLocalPlayerControlCustomLobbyPlayer(PlayerId);
			}

			if (ContextOption == EPlayerContextOptions::LobbyPromotePlayer)
			{
				return QueueDataFactory->CanLocalPlayerPromoteCustomLobbyPlayer(PlayerId);
			}
		}
	}

	return false;
}

bool URHContextMenu::IsInParty()
{
	if (URHPartyManager* PartyPartyManager = GetPartyManager())
	{
		return PartyPartyManager->IsInParty();
	}
	return false;
}

bool URHContextMenu::IsPartyFull()
{
	if (URHPartyManager* PartyManager = GetPartyManager())
	{
		return PartyManager->IsInParty() && PartyManager->IsPartyMaxed();
	}
	return false;
}

bool URHContextMenu::HasPlayerInvited()
{
	if (CurrentFriend != nullptr)
	{
		if (const auto PSS = GetRHPlayerInfoSubsystem())
		{
			if (URH_PlayerInfo* PlayerInfo = PSS->GetPlayerInfo(CurrentFriend->GetRHPlayerUuid()))
			{
				if (URHPartyManager* PartyManager = GetPartyManager())
				{
					return PartyManager->GetPartyInviter() == PlayerInfo;
				}
			}
		}
	}

	return false;
}

bool URHContextMenu::IsPartyLeader()
{
	if (URHPartyManager* PartyManager = GetPartyManager())
	{
		return PartyManager->IsLeader();
	}
	return false;
}

bool URHContextMenu::CanInvitePlayer()
{
	if (URHPartyManager* PartyManager = GetPartyManager())
	{
		if (MyHud != nullptr)
		{
			return PartyManager->HasInvitePrivileges(MyHud->GetLocalPlayerUuid());
		}
	}
	return false;
}

bool URHContextMenu::IsPlayerPendingPartyInvite()
{
	if (URHPartyManager* PartyManager = GetPartyManager())
	{
		if (MyHud != nullptr)
		{
			return PartyManager->GetPartyMemberByID(MyHud->GetLocalPlayerUuid()).IsPending;
		}
	}
	return false;
}

bool URHContextMenu::IsPlayerInParty()
{
	if (URHPartyManager* PartyManager = GetPartyManager())
	{
		if (CurrentFriend != nullptr)
		{
			return PartyManager->IsPlayerInParty(CurrentFriend->GetRHPlayerUuid());
		}
	}
	return false;
}

bool URHContextMenu::IsPlayerOnline()
{
	if (const auto PSS = GetRHPlayerInfoSubsystem())
	{
		if (URH_PlayerInfo* PlayerInfo = PSS->GetPlayerInfo(CurrentFriend->GetRHPlayerUuid()))
		{
			return URHUIBlueprintFunctionLibrary::GetFriendOnlineStatus(this, CurrentFriend, MyHud->GetLocalPlayerSubsystem(), false, false) == ERHPlayerOnlineStatus::FGS_Online;
		}
	}

	return false;
}

bool URHContextMenu::IsLocalPlayer()
{
	if (MyHud != nullptr)
	{
		if (CurrentFriend != nullptr)
		{
			return (MyHud->GetLocalPlayerUuid() == CurrentFriend->GetRHPlayerUuid());
		}
	}

	return false;
}

bool URHContextMenu::IsCrossplayEnabled()
{
	if (MyHud.IsValid())
	{
		return MyHud->IsPlatformCrossplayEnabled();
	}
	return false;
}

bool URHContextMenu::IsFriend()
{
	if (CurrentFriend != nullptr)
	{
		return CurrentFriend->ArePlatformFriends();
	}
	return false;
}

bool URHContextMenu::IsRHFriend()
{
	if (CurrentFriend != nullptr)
	{
		return CurrentFriend->AreRHFriends();
	}
	return false;
}

bool URHContextMenu::IsRequestingFriend()
{
	if (CurrentFriend != nullptr)
	{
		return CurrentFriend->RhPendingFriendRequest();
	}
	return false;
}

bool URHContextMenu::IsPendingFriend()
{
	if (CurrentFriend != nullptr)
	{
		return CurrentFriend->RHFriendRequestSent();
	}
	return false;
}

bool URHContextMenu::IsMuted()
{
	// #RHTODO - Voice
	return false;
	//return (CurrentFriend != nullptr) ? CurrentFriend->IsMuted() : false;
}

bool URHContextMenu::IsInVoiceChannel()
{
	// #RHTODO - Voice
	return false;
	//return (CurrentFriend != nullptr) ? CurrentFriend->IsInVoiceChannel() : false;
}

bool URHContextMenu::IsIgnored()
{
	if (CurrentFriend != nullptr)
	{
		if (auto RhSS = GetRHFriendSubsystem())
		{
			return RhSS->IsFriendBlocked(CurrentFriend);
		}
	}
	return false;
}

bool URHContextMenu::IsBlockedByRH()
{
	if (CurrentFriend != nullptr)
	{
		if (auto RhSS = GetRHFriendSubsystem())	
		{
			return RhSS->IsPlayerRhBlocked(CurrentFriend->GetRHPlayerUuid());
		}
	}
	return false;
}

bool URHContextMenu::IsBlockedByPlatform()
{
	if (CurrentFriend != nullptr)
	{
		if (auto RhSS = GetRHFriendSubsystem())
		{
			return RhSS->IsFriendPlatformBlocked(CurrentFriend);
		}
	}
	return false;
}

bool URHContextMenu::HasReportedPlayer()
{
	// #RHTODO - Report Player
	return false;
}

/*
* Context Menu Button
*/
URHContextMenuButton::URHContextMenuButton(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	ContextOption = EPlayerContextOptions::None;
}

void URHContextMenuButton::SetContextOption(EPlayerContextOptions Context)
{
	ContextOption = Context;
}

void URHContextMenuButton::HandleVisibility_Implementation(bool isVisibility)
{

}

void URHContextMenuButton::HandleActive_Implementation(bool isActive)
{

}